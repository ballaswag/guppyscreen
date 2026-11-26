#!/usr/bin/env python3
"""End-to-end check of the fake moonraker: starts one, drives it over the
websocket the way the UI does, and checks every answer against what moonraker
and klipper give.

    python3 tools/sim_smoke.py            # exit status 0 when everything passed
"""
import asyncio
import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from moonsim import ws  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 7191
FAILS = []


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what, flush=True)
    if not cond:
        FAILS.append(what)


class Client:
    def __init__(self, ws):
        self.ws, self.n, self.waiting, self.notes, self.state = ws, 0, {}, [], {}
        self.reader = asyncio.create_task(self.read())

    async def read(self):
        async for raw in self.ws:
            msg = json.loads(raw)
            if "id" in msg and msg["id"] in self.waiting:
                self.waiting.pop(msg["id"]).set_result(msg)
            elif "method" in msg:
                self.notes.append(msg)
                if msg["method"] == "notify_status_update":
                    for k, v in msg["params"][0].items():
                        self.state.setdefault(k, {}).update(v)

    async def call(self, method, params=None):
        self.n += 1
        fut = asyncio.get_event_loop().create_future()
        self.waiting[self.n] = fut
        await self.ws.send(json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": self.n}))
        return await asyncio.wait_for(fut, 120)

    async def gcode(self, script):
        return await self.call("printer.gcode.script", {"script": script})

    async def until(self, pred, timeout=60):
        end = time.time() + timeout
        while time.time() < end:
            if pred():
                return True
            await asyncio.sleep(0.1)
        return False

    def notes_of(self, method):
        return [n for n in self.notes if n["method"] == method]


async def connect():
    for _ in range(100):
        try:
            return Client(await ws.connect(PORT))
        except OSError:
            await asyncio.sleep(0.1)
    raise SystemExit("mock never came up")


async def run():
    c = await connect()
    ps = lambda: c.state.get("print_stats", {})
    await c.until(lambda: c.notes_of("notify_klippy_ready"), 5)
    info = (await c.call("printer.info"))["result"]
    check(info["state"] == "ready", "printer.info state ready")
    objs = (await c.call("printer.objects.list"))["result"]["objects"]
    sub = (await c.call("printer.objects.subscribe",
                        {"objects": {o: None for o in objs if not o.startswith("gcode_macro ")}}))["result"]["status"]
    c.state.update(json.loads(json.dumps(sub)))
    check("settings" in sub["configfile"] and sub["configfile"]["settings"]["extruder"]["max_temp"] == 300.0,
          "configfile.settings carries extruder max_temp")
    check(sub["toolhead"]["extruder"] == "extruder", "toolhead.extruder reported")
    check("led chamber_light" in sub and sub["led chamber_light"]["color_data"] == [[0, 0, 0, 0]], "rgb led present")
    check("extruder1" in sub, "second extruder present")

    # --- pins, leds, heaters: klipper's own rules
    await c.gcode("SET_PIN PIN=LED VALUE=1")
    await c.until(lambda: c.state["output_pin LED"]["value"] == 1.0, 3)
    check(c.state["output_pin LED"]["value"] == 1.0, "LED pin VALUE=1 reads back 1.0")
    r = await c.gcode("SET_PIN PIN=LED VALUE=0.5")
    check(r.get("error", {}).get("message") == "Invalid pin value", "non-pwm pin refuses 0.5")
    await c.gcode("SET_LED LED=chamber_light RED=1 WHITE=0.5")
    await c.until(lambda: c.state["led chamber_light"]["color_data"] == [[1.0, 0.0, 0.0, 0.5]], 3)
    check(c.state["led chamber_light"]["color_data"] == [[1.0, 0.0, 0.0, 0.5]], "SET_LED sets color_data")
    r = await c.gcode("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=400")
    check(r.get("error", {}).get("message") == "Requested temperature (400.0) out of range (0.0:300.0)",
          "heater out of range message")
    r = await c.gcode("FOO_BAR")
    check(r.get("result") == "ok", "unknown command is not an error")
    check(any(n["params"][0] == '// Unknown command:"FOO_BAR"' for n in c.notes_of("notify_gcode_response")),
          "unknown command answered on the console")
    r = await c.gcode("G1 X10")
    check("Must home axis first" in r.get("error", {}).get("message", ""), "moving unhomed refused")

    # --- AFC message queue
    afc = c.state["AFC"]
    check(afc["message"]["message"] == "first message", "seeded AFC message shows")
    await c.gcode("SIM_MESSAGE TEXT=second TYPE=error")
    await c.gcode("AFC_CLEAR_MESSAGE")
    await c.until(lambda: c.state["AFC"]["message"]["message"] == "second", 3)
    check(c.state["AFC"]["message"] == {"message": "second", "type": "error"}, "AFC_CLEAR_MESSAGE pops to the next")
    await c.gcode("AFC_CLEAR_MESSAGE")
    await c.until(lambda: c.state["AFC"]["message"]["message"] == "", 3)
    check(c.state["AFC"]["message"]["message"] == "", "queue empties")
    check(c.state["AFC"]["multiple_tool_mapping"] is True, "multiple_tool_mapping published")

    # --- AFC's raw HTML stays intact over the websocket and in console history
    start = len(c.notes)
    await c.gcode("SIM_AFC_CONSOLE")
    output = [n["params"][0] for n in c.notes[start:]
              if n["method"] == "notify_gcode_response"]
    check('lane1 tool cmd: T0  <span class=success--text>LOCKED</span>'
          '<span class=success--text> AND LOADED</span>'
          '<span class=primary--text> in ToolHead</span>' in output,
          "afc: PREP sends raw adjacent colour spans and space-padded tool")
    check('lane4 tool cmd: T3  EMPTY READY FOR SPOOL' in output,
          "afc: PREP reports the empty lane")
    check(any(s.startswith('<span class=success--text>R  _____     ____\n')
              and s.endswith('  CANVAS_1\n') for s in output),
          "afc: multiline logo stays in one raw response with spaces intact")
    check(output[-2:] == ['<span class=warning--text>WARNING: '
                         'Ram sensor enabled, confirm tool is loaded</span>',
                         'Filament loaded to nozzle (Δt:0.125s, t:1.500)'],
          "afc: console samples include warning HTML and UTF-8 delta timing")
    store = (await c.call("server.gcode_store", {"count": 20}))["result"]["gcode_store"]
    check([r["message"] for r in store if r["type"] == "response"][-len(output):] == output,
          "afc: console history preserves the same raw responses")
    check(c.state["AFC"]["message"]["message"] == "",
          "afc: console samples leave the message queue empty")

    # --- files and metadata
    directory = (await c.call("server.files.get_directory", {"path": "gcodes"}))["result"]
    check(all(d["dirname"] != "usb" for d in directory["dirs"]), "USB is absent by default")
    listing = (await c.call("server.files.list", {"root": "gcodes"}))["result"]
    paths = {item["path"] for item in listing}
    check(len(paths) >= 24, "file library has several screens of tiles")
    check({'Small parts tray_PLA_5m20s.gcode', 'spare_parts/hinge "left".gcode',
           'spare_parts/knob.gcode', 'spare_parts/blue/knob.gcode'} <= paths,
          "file library includes spaces, quotes and nested duplicate basenames")
    for name, material, seconds in (("cable_clip_PETG_6m32s.gcode", "PETG", 392),
                                     ("hex_knob_PLA_4m32s.gcode", "PLA", 272)):
        sample = (await c.call("server.files.metadata", {"filename": name}))["result"]
        check(sample["filament_type"] == material and sample["estimated_time"] == seconds,
              "Orca-style sample filename matches its material and duration: " + name)
    previews = []
    for name in ("spare_parts/knob.gcode", "spare_parts/blue/knob.gcode"):
        sample = (await c.call("server.files.metadata", {"filename": name}))["result"]
        thumb_path = os.path.join("/tmp/grumpy-sim-%d/gcodes" % PORT,
                                  os.path.dirname(name), sample["thumbnails"][-1]["relative_path"])
        with open(thumb_path, "rb") as f:
            previews.append(f.read())
    check(previews[0] != previews[1], "same-named files in different folders have distinct previews")
    r = await c.call("server.files.metadata", {"filename": "nope.gcode"})
    check(r.get("error") == {"code": -32601, "message": "Metadata not available for <nope.gcode>"},
          "missing metadata error")
    meta = (await c.call("server.files.metadata", {"filename": "quick_cube.gcode"}))["result"]
    check(meta["slicer"] == "OrcaSlicer" and meta["estimated_time"] > 0 and len(meta["thumbnails"]) == 2,
          "metadata parsed from the file (slicer, time, thumbnails)")
    thumb = meta["thumbnails"][1]["relative_path"]
    check(os.path.exists("/tmp/grumpy-sim-%d/gcodes/%s" % (PORT, thumb)), "thumbnail extracted to .thumbs")

    # --- a whole print
    await c.call("printer.print.start", {"filename": "quick_cube.gcode"})
    check(await c.until(lambda: ps().get("state") == "printing", 10), "print starts")
    r = await c.call("server.files.delete_file", {"path": "gcodes/quick_cube.gcode"})
    check(r.get("error") == {"code": 403, "message": "File currently in use"}, "printing file cannot be deleted")
    check(await c.until(lambda: c.state["exclude_object"]["objects"], 30), "exclude_object objects defined by the file")
    await c.call("printer.print.pause")
    check(await c.until(lambda: ps().get("state") == "paused", 10), "pause")
    await c.call("printer.print.resume")
    check(await c.until(lambda: ps().get("state") == "printing", 10), "resume")
    check(await c.until(lambda: ps().get("state") == "complete", 240), "print completes")
    check(ps().get("filament_used", 0) > 100, "filament used counted (%.0f mm)" % ps().get("filament_used", 0))
    check(c.state["print_stats"]["info"]["current_layer"] == c.state["print_stats"]["info"]["total_layer"],
          "layers counted to the last")
    hist = (await c.call("server.history.list", {"limit": 1}))["result"]["jobs"][0]
    check(hist["filename"] == "quick_cube.gcode" and hist["status"] == "completed", "history records the job")
    r = await c.call("server.files.delete_file", {"path": "gcodes/quick_cube.gcode"})
    check(r.get("result", {}).get("action") == "delete_file", "delete after the print")
    check(await c.until(lambda: c.notes_of("notify_filelist_changed"), 3), "filelist change notified")
    check(not os.path.exists("/tmp/grumpy-sim-%d/gcodes/quick_cube.gcode" % PORT)
          and os.path.exists(os.path.join(HERE, "sim_gcodes", "quick_cube.gcode")),
          "delete only touched the session copy")

    # --- a print that pauses itself, and one that errors
    await c.call("printer.print.start", {"filename": "pause_at_layer_6.gcode"})
    check(await c.until(lambda: ps().get("state") == "paused", 120), "PAUSE in the file pauses the print")
    await c.call("printer.print.cancel")
    check(await c.until(lambda: ps().get("state") == "cancelled", 10), "cancel")
    await c.call("printer.print.start", {"filename": "error_out_of_range.gcode"})
    check(await c.until(lambda: ps().get("state") == "error", 120), "out-of-range move ends in error")
    check("Move out of range" in ps().get("message", ""), "error message kept in print_stats")

    # --- update manager
    st = (await c.call("machine.update.status"))["result"]
    cos = st["version_info"]["cosmos"]
    check(cos["version"] == "v1.4.0" and cos["remote_version"] == "v1.5.0", "cosmos update available")
    r = await c.call("machine.update.client", {"name": "cosmos"})
    ups = c.notes_of("notify_update_response")
    check(r.get("result") == "ok" and ups and ups[-1]["params"][0]["complete"], "update streams to complete")

    # --- machine, power, services
    pw = (await c.call("machine.device_power.devices"))["result"]["devices"]
    check({d["device"] for d in pw} == {"printer", "chamber_light"}, "power devices listed")
    await c.call("machine.device_power.post_device", {"device": "chamber_light", "action": "on"})
    check(c.notes_of("notify_power_changed"), "power change notified")
    check(await c.until(lambda: c.notes_of("notify_proc_stat_update"), 3), "proc stats stream")
    r = await c.call("machine.services.restart", {"service": "nope"})
    check(r.get("error", {}).get("message") == "Service 'nope' not allowed", "unknown service refused")
    await c.call("machine.services.restart", {"service": "klipper"})
    check(await c.until(lambda: c.notes_of("notify_klippy_disconnected"), 5), "klipper restart disconnects")
    check(await c.until(lambda: len(c.notes_of("notify_klippy_ready")) >= 2, 10), "and comes back ready")
    await c.ws.close()


async def subscribed():
    c = await connect()
    await c.until(lambda: c.notes_of("notify_klippy_ready"), 5)
    objs = (await c.call("printer.objects.list"))["result"]["objects"]
    sub = (await c.call("printer.objects.subscribe", {"objects": {o: None for o in objs}}))["result"]["status"]
    c.state.update(json.loads(json.dumps(sub)))
    return c


async def run_afc_colour():
    c = await subscribed()
    await c.call("printer.print.start", {"filename": "keychain_4colour.gcode"})
    seen = set()
    end = time.time() + 240
    while time.time() < end and c.state["print_stats"].get("state") != "complete":
        seen.add(c.state["AFC"]["current_load"])
        await asyncio.sleep(0.05)
    check(c.state["print_stats"]["state"] == "complete", "afc: 4-colour print completes")
    check(len(seen - {None}) >= 4, "afc: T0..T3 loaded four lanes (%s)" % sorted(x for x in seen if x))
    await c.ws.close()


async def run_afc_empty():
    c = await subscribed()
    await c.call("printer.print.start", {"filename": "keychain_4colour.gcode"})
    check(await c.until(lambda: c.state["print_stats"].get("state") == "paused", 240),
          "afc: T3 on the empty lane4 pauses the print")
    check(c.state["AFC"]["error_state"] and c.state["AFC_lane lane4"]["status"] == "Error",
          "afc: lane failure flagged")
    await c.ws.close()


async def run_hh_console():
    c = await subscribed()
    before = json.loads(json.dumps(c.state["mmu"]))
    start = len(c.notes)
    await c.gcode("SIM_HH_CONSOLE")
    output = [n["params"][0] for n in c.notes[start:]
              if n["method"] == "notify_gcode_response"]
    with open(os.path.join(HERE, "moonsim/hh_console_samples.json"), encoding="utf-8") as f:
        samples = json.load(f)["samples"]
    expected = ["// " + line for messages in samples.values()
                for message in messages for line in message.split("\n")]
    check(output == expected, "hh: HTML, Unicode and spaces survive websocket responses byte for byte")
    check(any('\u00a0' in s for s in output), "hh: bypass row retains non-breaking space")
    check(any('<b><span style="color:#87CEEB">UNLOADED</span></b>' in s for s in output),
          "hh: bold and nested inline colour spans reach the console")
    store = (await c.call("server.gcode_store", {"count": 50}))["result"]["gcode_store"]
    check([r["message"] for r in store if r["type"] == "response"][-len(output):] == expected,
          "hh: history retains the same raw formatting")
    check(c.state["mmu"] == before, "hh: samples leave MMU state unchanged")
    await c.ws.close()


async def run_hh():
    await run_hh_console()
    c = await subscribed()
    tools, states = set(), set()
    await c.call("printer.print.start", {"filename": "stripes_2colour.gcode"})
    end = time.time() + 240
    while time.time() < end and c.state["print_stats"].get("state") != "complete":
        tools.add(c.state["mmu"]["tool"])
        states.add(c.state["mmu"]["print_state"])
        await asyncio.sleep(0.05)
    states.add(c.state["mmu"]["print_state"])
    check(c.state["print_stats"]["state"] == "complete", "hh: 2-colour print completes")
    check({0, 1} <= tools, "hh: both tools loaded")
    check({"started", "printing", "complete"} <= states, "hh: print_state started/printing/complete (%s)" % sorted(states))
    await c.gcode("MMU_GATE_MAP GATE=1 TEMP=245")
    await c.until(lambda: c.state["mmu"]["gate_temperature"][1] == 245, 3)
    check(c.state["mmu"]["gate_temperature"][1] == 245, "hh: gate_temperature editable")
    await c.ws.close()


async def run_none():
    c = await subscribed()
    check("AFC" not in c.state and "mmu" not in c.state, "none: no changer objects")
    await c.gcode("T1")
    await c.until(lambda: c.state["toolhead"]["extruder"] == "extruder1", 3)
    check(c.state["toolhead"]["extruder"] == "extruder1", "none: T1 activates extruder1")
    await c.ws.close()


async def run_usb():
    c = await connect()
    directory = (await c.call("server.files.get_directory", {"path": "gcodes"}))["result"]
    check(any(d["dirname"] == "usb" for d in directory["dirs"]), "USB appears at the G-code root")
    listing = (await c.call("server.files.list", {"root": "gcodes"}))["result"]
    sample = "cable_clip_PETG_6m32s.gcode"
    path = "usb/" + sample
    check({sample, path} <= {item["path"] for item in listing}, "USB has a separate copy of the print")
    meta = (await c.call("server.files.metadata", {"filename": path}))["result"]
    thumb = meta["thumbnails"][-1]["relative_path"]
    check(os.path.isfile(os.path.join("/tmp/grumpy-sim-%d/gcodes/usb" % PORT, thumb)),
          "USB preview is extracted next to the file")
    await c.ws.close()


def with_mock(flags, coro):
    session = "/tmp/grumpy-sim-%d" % PORT
    mock = subprocess.Popen([sys.executable, "-u", os.path.join(HERE, "fake_moonraker.py"), "--port", str(PORT),
                             "--speed", "400", "--latency", "0"] + flags,
                            stdout=open(session + ".log", "a"), stderr=subprocess.STDOUT)
    try:
        asyncio.run(coro())
    finally:
        mock.terminate()
        mock.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hh-console-only", action="store_true", help="only check Happy Hare console captures")
    args = parser.parse_args()
    open("/tmp/grumpy-sim-%d.log" % PORT, "w").close()
    if args.hh_console_only:
        with_mock(["--backend", "hh"], run_hh_console)
        sys.exit(1 if FAILS else 0)
    with_mock(["--extruders", "2", "--rgb-led", "--afc-multi-map", "--message", "first message",
               "--update", "ok"], run)
    print("-- afc multicolour")
    with_mock(["--lanes", "10"], run_afc_colour)
    with_mock(["--lanes", "4"], run_afc_empty)
    print("-- happy hare")
    with_mock(["--backend", "hh", "--lanes", "4"], run_hh)
    print("-- no changer, two extruders")
    with_mock(["--backend", "none", "--extruders", "2"], run_none)
    print("-- simulated USB drive")
    with_mock(["--usb"], run_usb)
    print("\n%d failed" % len(FAILS) if FAILS else "\nall passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
