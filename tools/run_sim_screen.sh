#!/bin/bash
# Capture the screen's output and exit status, even when it dies from a signal.
set -u

SCREEN_LOG=$1
shift
RUNNER=("$@")
if [ "${SIM_DEBUG:-0}" = 1 ]; then
    if ! command -v gdb >/dev/null 2>&1; then
        echo "[sim] SIM_DEBUG=1 requires gdb" | tee -a "$SCREEN_LOG"
        exit 127
    fi
    RUNNER=(gdb --batch --return-child-result
        -ex 'set pagination off' -ex 'set debuginfod enabled off'
        -ex run -ex 'thread apply all bt full' --args "$@")
fi

printf '[sim] starting %s\n' "$*" | tee -a "$SCREEN_LOG"
"${RUNNER[@]}" 2>&1 | tee -a "$SCREEN_LOG"
SCREEN_STATUS=${PIPESTATUS[0]}
printf '[sim] screen exited with status %s\n' "$SCREEN_STATUS" | tee -a "$SCREEN_LOG"
exit "$SCREEN_STATUS"
