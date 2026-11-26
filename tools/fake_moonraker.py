#!/usr/bin/env python3
"""A simulated printer behind Moonraker's API, for running the screen
without one. Standard library only.

It prints the gcode files in tools/sim_gcodes (heating, homing, moving,
extruding, labelled objects, tool changes) on a clock --speed times faster
than real time, with an AFC or Happy Hare filament changer, spoolman, print
history, job queue, power devices and the COSMOS updater.

Nothing is destructive: files live in a per-session copy under --session-dir;
reboot, shutdown, service restarts and updates act on the simulation only.

    python3 tools/fake_moonraker.py [--port 7125] [--backend afc|hh|none]
    python3 tools/fake_moonraker.py --help

./sim.sh runs it together with fake_wpa_supplicant.py and a local build.
The code is in tools/moonsim/.
"""
import argparse
import asyncio
import json
import mimetypes
import os
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moonsim import state as S, files, klipper as K, mmu, moonraker as M, ws  # noqa: E402
from moonsim import testbench  # noqa: E402,F401  (registers the SIM_* gcode)
from moonsim.state import RpcError, GcodeError  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
NEXT_ID = [1730367000]


# ------------------------------------------------------------------ websocket + http
async def client_handler(conn):
    NEXT_ID[0] += 1
    S.CLIENTS[conn] = {"subs": {}, "id": NEXT_ID[0], "info": None}
    print("client connected: %s" % (conn.remote_address,), flush=True)
    try:
        async for raw in conn:
            asyncio.create_task(answer(conn, raw))
    finally:
        S.CLIENTS.pop(conn, None)
        print("client disconnected", flush=True)


async def answer(ws, raw):
    try:
        req = json.loads(raw)
    except json.JSONDecodeError:
        return
    method, rid = req.get("method", ""), req.get("id")
    params = req.get("params", {})
    if method != "printer.gcode.script":
        print("rpc: %s" % method, flush=True)
    else:
        print("gcode: %s" % params.get("script", ""), flush=True)
    # klipper and moonraker take a moment; instant replies hide UI races
    await asyncio.sleep(S.ARGS.latency)
    reply = {"jsonrpc": "2.0", "id": rid}
    try:
        if not isinstance(params, dict):
            raise RpcError("Invalid params:", -32602)
        reply["result"] = await M.handle(ws, method, params)
    except GcodeError as e:
        await S.console("!! " + e.message)
        reply["error"] = {"code": 400, "message": e.message}
    except RpcError as e:
        print("  !! %s" % e.message, flush=True)
        reply["error"] = {"code": e.code, "message": e.message}
    except Exception as e:  # moonraker answers anything else with a 500
        import traceback
        traceback.print_exc()
        reply["error"] = {"code": 500, "message": str(e)}
    if rid is not None and ws in S.CLIENTS:
        await S.send(ws, json.dumps(reply))


async def http_reply(path):
    """The http side: file downloads, the way moonraker serves /server/files.
    None lets a websocket upgrade through."""
    if not S.HOST["up"]:
        return 503, [], b"host is down (sim)\n"
    if path.startswith("/websocket"):
        return None
    url = urllib.parse.urlparse(path)
    parts = urllib.parse.unquote(url.path).strip("/").split("/", 3)
    if parts[:2] == ["server", "files"] and len(parts) == 4 and parts[2] in ("gcodes", "config"):
        root = os.path.abspath(files.root() if parts[2] == "gcodes"
                               else os.path.join(S.ARGS.session_dir, "config"))
        full = os.path.abspath(os.path.join(root, parts[3]))
        if os.path.commonpath([root, full]) == root and os.path.isfile(full):
            with open(full, "rb") as f:
                body = f.read()
            ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
            return 200, [("Content-Type", ctype)], body
        return 404, [("Content-Type", "application/json")], \
            json.dumps({"error": {"code": 404, "message": "File does not exist"}}).encode()
    return 404, [], b"not found\n"


# ------------------------------------------------------------------ ui config
COMMANDS = {"factory_reset_cmd": "factory-reset", "gui_restart_cmd": "restart-gui",
            "switch_to_stock_cmd": "switch-to-stock", "support_zip_cmd": "support-zip",
            "restart_wifi_cmd": "restart-wifi"}


def extra_ui_sections():
    """The UI only shows hardware its config names; name what this session adds."""
    out = []
    for i, name in enumerate(K.extruder_names()[1:], 1):
        out.append(('monitored_sensor "%s"' % name, {"display_name": "Extruder %d" % i,
                                                     "color": [15, 13, 16][(i - 1) % 3], "controllable": "true"}))
    if "heater_generic chamber_heater" in K.CONFIG:
        out.append(('monitored_sensor "heater_generic chamber_heater"',
                    {"display_name": "Chamber heater", "color": 8, "controllable": "true"}))
    if "heater_fan hotend_fan" in K.CONFIG:
        out.append(('fan "heater_fan hotend_fan"', {"display_name": "Hotend"}))
        out.append(('fan "controller_fan mcu_fan"', {"display_name": "Electronics"}))
    if "led chamber_light" in K.CONFIG:
        out.append(('led "led chamber_light"', {"display_name": "Chamber light", "pwm": "true"}))
    return out


def write_ui_config(out_path, base_path):
    """grumpyscreen.cfg for this session: our port, sim stand-ins
    for every [commands] entry (so nothing touches the host), and sections for
    the extra hardware."""
    with open(base_path) as f:
        lines = f.read().split("\n")
    port = S.ARGS.port
    thumbs = os.path.join(S.ARGS.session_dir, "thumbs") if S.ARGS.http_thumbnails else ""
    if thumbs:
        os.makedirs(thumbs, exist_ok=True)
    section, out, seen = "", [], set()
    for line in lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1]
            seen.add(section)
        elif ":" in s and not s.startswith(("#", ";")):
            key = s.split(":", 1)[0].strip()
            if section == "moonraker" and key == "port":
                line = "port: %d" % port
            elif section == "moonraker" and key == "thumbnail_path":
                line = "thumbnail_path: %s" % thumbs
            elif section == "commands" and key in COMMANDS:
                line = "%s: python3 tools/sim_hook.py %s --port %d" % (key, COMMANDS[key], port)
        out.append(line)
    for name, opts in extra_ui_sections():
        if name not in seen:
            out.append("")
            out.append("[%s]" % name)
            out += ["%s: %s" % kv for kv in opts.items()]
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w") as f:
        f.write("\n".join(out) + "\n")
    print("ui config written to %s" % out_path, flush=True)


# ------------------------------------------------------------------ main
async def main():
    a = S.ARGS
    os.makedirs(os.path.join(a.session_dir, "logs"), exist_ok=True)
    os.makedirs(os.path.join(a.session_dir, "config"), exist_ok=True)
    files.setup()
    mmu.build(a)
    M.HISTORY.seed(a.history)
    M.build_updates()
    M.build_power()
    K.build_config()
    K.build_objects(keep=mmu.objects())
    if a.ui_config:
        write_ui_config(a.ui_config, a.ui_config_base)
    if a.queue:
        await M.QUEUE.post(a.queue)
    for coro in (K.physics(), S.status_tick(), M.stats_loop()):
        asyncio.create_task(coro)

    async def boot():
        if a.klippy == "disconnected":
            await M.service_state("klipper", "inactive", "dead")
            S.KLIPPY.update(state="disconnected", message="Klippy Disconnected")
            return
        await asyncio.sleep(a.boot_time)
        await K.set_klippy("ready", "Printer is ready")
        if a.klippy == "shutdown":
            await K.shutdown("Heater extruder not heating at expected rate")
        elif a.printing:
            await M.run_gcode('SDCARD_PRINT_FILE FILENAME="%s"' % a.printing)
    asyncio.create_task(boot())
    server = await ws.serve(client_handler, http_reply, a.host, a.port)
    print("fake moonraker on ws://%s:%d/websocket  session %s" % (a.host, a.port, a.session_dir), flush=True)
    async with server:
        await server.serve_forever()


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description="A simulated printer behind Moonraker's API.",
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    g = ap.add_argument_group("server")
    g.add_argument("--port", type=int, default=7125, help="websocket/http port")
    g.add_argument("--host", default="127.0.0.1", help="address to listen on")
    g.add_argument("--session-dir", help="this session's sandbox (default /tmp/grumpy-sim-<port>); "
                                         "its gcodes/ copy is recreated on every start")
    g.add_argument("--gcodes", default=files.SAMPLE_GCODES,
                   help="library of test prints copied into the session")
    g.add_argument("--usb", action="store_true",
                   help="start with a simulated USB drive containing sample G-code files")
    g.add_argument("--latency", type=float, default=0.2, help="seconds before each rpc reply")
    g.add_argument("--ui-config", help="write a grumpyscreen.cfg for this session here")
    g.add_argument("--ui-config-base", default=os.path.join(HERE, "..", "grumpyscreen.cfg"),
                   help="config the --ui-config one is derived from")
    g.add_argument("--http-thumbnails", action="store_true",
                   help="UI config sets thumbnail_path, so the UI downloads thumbnails over http "
                        "instead of reading the gcode root")

    g = ap.add_argument_group("printer")
    g.add_argument("--extruders", type=int, default=1, choices=range(1, 5), help="extruder, extruder1, ...")
    g.add_argument("--fans", type=int, default=1, help="how many of grumpyscreen.cfg's fans exist (1-%d)" % len(K.FAN_IDS))
    g.add_argument("--leds", type=int, default=1, help="how many of grumpyscreen.cfg's output_pin leds exist (0-%d)" % len(K.LED_IDS))
    g.add_argument("--rgb-led", action="store_true", help="add [led chamber_light] (colour_data, SET_LED)")
    g.add_argument("--heater-fans", action="store_true", help="add heater_fan hotend_fan + controller_fan mcu_fan")
    g.add_argument("--chamber-heater", action="store_true", help="add heater_generic chamber_heater")
    g.add_argument("--speed", type=float, default=20.0, help="print clock speed-up over real time")
    g.add_argument("--idle-timeout", type=float, default=600.0, help="klipper idle_timeout seconds")
    g.add_argument("--klippy", choices=["ready", "shutdown", "disconnected"], default="ready",
                   help="klippy state after boot")
    g.add_argument("--boot-time", type=float, default=1.0, help="seconds klippy spends in startup at launch")
    g.add_argument("--printing", nargs="?", const="quick_cube.gcode", default=None, metavar="FILE",
                   help="start printing FILE (default quick_cube.gcode) once ready")

    g = ap.add_argument_group("filament changer")
    g.add_argument("--backend", choices=["afc", "hh", "none"], default="afc",
                   help="afc: AFC lane objects; hh: Happy Hare mmu object; none: no changer")
    g.add_argument("--lanes", type=int, default=4, help="lanes / gates (1-16)")
    g.add_argument("--afc-multi-map", action="store_true", help="AFC enable_multiple_mapping (two tools on one lane)")
    g.add_argument("--spoolman", choices=["off", "readonly", "push", "pull"], default="push",
                   help="'off' drops moonraker's spoolman component; for hh, 'pull' makes Happy Hare own "
                        "colour/material (the panel greys them out)")
    g.add_argument("--spoolman-lanes", default="", help="afc: comma-separated lane numbers given a spoolman spool")
    g.add_argument("--message", default="", help="afc: seed the AFC message queue")
    g.add_argument("--message-type", choices=["warning", "error"], default="warning", help="afc: type of --message")

    g = ap.add_argument_group("moonraker extras")
    g.add_argument("--update", default="ok",
                   help="COSMOS update check: none (up to date), ok (update available), fail (the install "
                        "fails), refuse (refused as while printing), nightly (git commits behind), invalid "
                        "(repo can't be updated), busy, missing (no COSMOS updater). A comma-separated list "
                        "moves to the next mode on each refresh")
    g.add_argument("--history", type=int, default=6, help="past jobs seeded into print history")
    g.add_argument("--queue", default="", help="comma-separated files seeded into the job queue")
    g.add_argument("--no-power-devices", action="store_true", help="no machine.device_power devices")
    g.add_argument("--reboot-time", type=float, default=8.0, help="seconds a reboot keeps the host down")
    g.add_argument("--shutdown-time", type=float, default=15.0,
                   help="seconds a shutdown keeps the host down before the sim brings it back")
    a = ap.parse_args(argv)
    a.session_dir = a.session_dir or "/tmp/grumpy-sim-%d" % a.port
    return a


if __name__ == "__main__":
    S.ARGS = parse_args()
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
