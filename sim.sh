#!/bin/bash
# Runs a local build against the fake printer:
#
#   ./build.sh sdl --small        # a local build, once
#   ./sim.sh
#
# Starts tools/fake_moonraker.py and tools/fake_wpa_supplicant.py, then the
# screen with a config made for this session. Everything happens in
# /tmp/grumpy-sim-$SIM_PORT (logs too); nothing outside it is touched.
#
#   SIM_BACKEND=hh SIM_LANES=8 ./sim.sh
#   SIM_USB=1 ./sim.sh
#   SIM_CFG_OVERRIDE=/path/to/grumpyscreen.ini ./sim.sh
#   SIM_ARGS="--extruders 2 --rgb-led --update nightly" ./sim.sh
#   python3 tools/fake_moonraker.py --help      # every SIM_ARGS flag
set -e
cd "$(dirname "$0")"
PORT="${SIM_PORT:-7125}"
SESSION=/tmp/grumpy-sim-$PORT

if [ -z "${SIM_BUILD:-}" ]; then
    for d in build/sdl-small build/sdl build; do
        [ -x "$d/bin/grumpyscreen" ] && SIM_BUILD=$d && break
    done
fi
BIN="${SIM_BUILD:-build/sdl-small}/bin/grumpyscreen"
[ -x "$BIN" ] || { echo "ERROR: no build at $BIN; build one (./build.sh sdl --small) or set SIM_BUILD"; exit 1; }

ARGS=(--port "$PORT" --lanes "${SIM_LANES:-4}" --backend "${SIM_BACKEND:-afc}"
      --fans "${SIM_FANS:-1}" --leds "${SIM_LEDS:-1}" --spoolman "${SIM_SPOOLMAN:-push}")
if [ "${SIM_USB:-0}" = 1 ]; then
    ARGS+=(--usb)
fi
case "${SIM_PRINTING:-0}" in
    0) ;;
    1) ARGS+=(--printing) ;;
    *) ARGS+=(--printing "$SIM_PRINTING") ;;
esac
# shellcheck disable=SC2206
ARGS+=(${SIM_ARGS:-})

# --- stop a previous run on this port (SIM_KEEP=1 leaves other UIs running,
# for side-by-side runs on different ports)
[ "${SIM_KEEP:-0}" = 1 ] || pkill -f "$BIN" 2>/dev/null || true
pkill -f "fake_moonraker.py --port $PORT " 2>/dev/null || true
pkill -f "fake_wpa_supplicant.py --dir $SESSION/" 2>/dev/null || true
for _ in 1 2 3 4 5; do
    ss -ltn 2>/dev/null | grep -q ":$PORT " || break
    sleep 1
done
if ss -ltn 2>/dev/null | grep -q ":$PORT "; then echo "ERROR: port $PORT is in use"; exit 1; fi

mkdir -p "$SESSION"
SIM_CFG=$SESSION/grumpyscreen.cfg
rm -f "$SIM_CFG"
cleanup() { kill "$MOCK_PID" ${WPA_PID:-} 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# the fake moonraker also writes this session's UI config; SIM_CFG_SRC picks
# the config it starts from
python3 -u tools/fake_moonraker.py "${ARGS[@]}" --ui-config "$SIM_CFG" \
    --ui-config-base "${SIM_CFG_SRC:-grumpyscreen.cfg}" > "$SESSION/moonraker.log" 2>&1 &
MOCK_PID=$!

if [ "${SIM_WIFI:-1}" = 1 ]; then
    # the wifi panel reads its IP off the interface the socket is named after
    IFACE=$(ip -o route show default 2>/dev/null | awk '{for (i = 1; i < NF; i++) if ($i == "dev") {print $(i + 1); exit}}')
    python3 -u tools/fake_wpa_supplicant.py --dir "$SESSION/wpa" --iface "${IFACE:-eth0}" \
        ${SIM_WIFI_ARGS:-} > "$SESSION/wpa.log" 2>&1 &
    WPA_PID=$!
    export WPA_SUPPLICANT_SOCKET="$SESSION/wpa"
fi

for _ in $(seq 1 100); do
    [ -f "$SIM_CFG" ] && python3 -c "import socket; socket.create_connection(('127.0.0.1', $PORT), 0.2).close()" 2>/dev/null && break
    kill -0 "$MOCK_PID" 2>/dev/null || { cat "$SESSION/moonraker.log"; exit 1; }
    sleep 0.1
done
echo "[sim] moonraker on $PORT (${ARGS[*]}), session $SESSION"
SCREEN_LOG="$SESSION/screen-$(date +%Y%m%d-%H%M%S)-$$.log"
echo "[sim] screen output: $SCREEN_LOG"

# settings the UI saves stay in the session; a GUI restart (settings, factory
# reset, shutdown) brings it straight back
export CONFIG_OVERRIDE_FILE="$SESSION/grumpyscreen-override.cfg"
rm -f "$CONFIG_OVERRIDE_FILE"
if [ -n "${SIM_CFG_OVERRIDE:-}" ]; then
    cp "$SIM_CFG_OVERRIDE" "$CONFIG_OVERRIDE_FILE"
fi
while true; do
    rm -f "$SESSION/restart-gui"
    SCREEN_STATUS=0
    CONFIG_FILE="$SIM_CFG" bash tools/run_sim_screen.sh "$SCREEN_LOG" "$BIN" || SCREEN_STATUS=$?
    [ -f "$SESSION/restart-gui" ] || break
done
exit "$SCREEN_STATUS"
