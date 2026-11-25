# Testing Without a Printer

Two fakes stand in for the printer, so the screen can be developed and checked without one. Both need only Python 3.

| Part | File |
| --- | --- |
| Fake Moonraker + Klipper | `tools/fake_moonraker.py` (code in `tools/moonsim/`) |
| Fake wpa_supplicant | `tools/fake_wpa_supplicant.py` |

## With a local build

```
./build.sh sdl --small
./sim.sh
```

`sim.sh` starts both fakes and the first of `build/sdl-small`, `build/sdl`, `build` that exists, and stops them when the screen exits.

Each run gets a fresh session in `/tmp/grumpy-sim-<port>`: a copy of the test prints, the UI config written for the session, saved UI settings, and the logs (`moonraker.log`, `wpa.log`). Deleting files, factory reset, shutdown, updates and wifi changes only ever touch that folder. The `[commands]` in the session config run `tools/sim_hook.py` instead of the printer's scripts.

## On their own

Any screen can use the fake printer, including one on a printer or another machine:

```
python3 tools/fake_moonraker.py --host 0.0.0.0
```

Point the screen's `[moonraker]` `host` and `port` at it, and set `thumbnail_path` so thumbnails are downloaded. Only `sim.sh` swaps the screen's `[commands]` for stand-ins; elsewhere they run for real. `fake_wpa_supplicant.py` is a local socket, so it only works on the machine running the screen (`WPA_SUPPLICANT_SOCKET=<dir>`).

## Settings

| Variable | Default | |
| --- | --- | --- |
| `SIM_BACKEND` | `afc` | `afc`, `hh` (Happy Hare) or `none` |
| `SIM_LANES` | `4` | Lanes or gates |
| `SIM_FANS` | `1` | How many of `grumpyscreen.cfg`'s fans exist (1-7) |
| `SIM_LEDS` | `1` | How many of its output_pin LEDs exist (0-5) |
| `SIM_SPOOLMAN` | `push` | `off`, `readonly`, `push` or `pull` |
| `SIM_PRINTING` | `0` | `1` starts `quick_cube.gcode`, or name a file |
| `SIM_ARGS` | | Any `fake_moonraker.py` flag, see below |
| `SIM_WIFI` | `1` | `0` runs without the fake wpa_supplicant |
| `SIM_WIFI_ARGS` | | Any `fake_wpa_supplicant.py` flag |
| `SIM_PORT` | `7125` | Moonraker port |
| `SIM_BUILD` | | Build folder to run, e.g. `build/sdl` |
| `SIM_CFG_SRC` | `grumpyscreen.cfg` | Config the session config is made from |
| `SIM_CFG_OVERRIDE` | | Override file copied into the session |
| `SIM_KEEP` | `0` | `1` keeps other UIs running, for side-by-side ports |

Set `[ui] hide_spoolman: true` in the configuration to hide the global
Spoolman tab; MMU Spoolman integration stays available. To try visibility
settings with the simulator, put them in an override file and run
`SIM_CFG_OVERRIDE=/path/to/grumpyscreen.ini ./sim.sh`.

MMU detection needs no screen backend setting: `SIM_BACKEND=afc` and
`SIM_BACKEND=hh` enable its navigation button automatically; `SIM_BACKEND=none`
leaves it visible and disabled. Set `[ui] hide_mmu: true` to hide it entirely
and disable MMU integration. Both visibility settings default to `false`.

Useful `SIM_ARGS` flags (`python3 tools/fake_moonraker.py --help` lists all):

| Flag | |
| --- | --- |
| `--extruders N` | Up to 4 extruders; with `SIM_BACKEND=none`, `T<n>` activates one |
| `--rgb-led`, `--heater-fans`, `--chamber-heater` | Extra hardware, added to the session config too |
| `--speed X` | How much faster than real time prints run (20) |
| `--update MODE` | COSMOS update check: `none`, `ok`, `fail`, `refuse`, `nightly`, `invalid`, `busy`, `missing`; a comma-separated list moves on each refresh |
| `--klippy STATE` | Start `ready`, `shutdown` or `disconnected` |
| `--afc-multi-map` | AFC `enable_multiple_mapping` |
| `--message TEXT`, `--message-type error` | Seed the AFC message queue |
| `--spoolman-lanes 1,3` | Give AFC lanes a Spoolman spool |
| `--http-thumbnails` | The UI fetches thumbnails over HTTP |
| `--history N`, `--queue a,b` | Seed print history and the job queue |

`fake_wpa_supplicant.py` starts joined to `HomeNet`; every secured network's password is `printer123`. `--fail wrong-key|assoc-reject|not-found` makes connects fail, `--known "SSID:psk"` seeds a saved network.

## Test prints

`tools/sim_gcodes/` holds real Klipper gcode in OrcaSlicer's layout, with a short start and end sequence, embedded thumbnails and object labels. `tools/make_sim_gcodes.py` regenerates them.

| File | For |
| --- | --- |
| `quick_cube.gcode` | A quick single-colour print |
| `plate_5_parts.gcode` | Exclude object; the bracket's centre lies outside its outline |
| `keychain_4colour.gcode` | Four colours, plus a fifth tool the file never uses |
| `plate_6colour.gcode` | Six tools, two the same colour, mixed materials |
| `stripes_2colour.gcode` | Frequent tool changes |
| `pause_at_layer_6.gcode` | Pauses itself, like a filament change |
| `error_out_of_range.gcode` | Ends in Klipper's error state |
| `no_object_labels.gcode`, `no_thumbnail.gcode` | Empty states |
| `calibration/`, `spare_parts/` | Subfolders, a temperature tower and a long file name |
| `cable_clip_PETG_6m32s.gcode`, `hex_knob_PLA_4m32s.gcode` | Orca-style material and duration suffixes |

The print panel displays a flat gallery: files inside these folders appear
alongside files at the top level. USB files exposed by Moonraker in a G-code
subfolder join the same gallery. Each card shows its thumbnail, model name,
material, estimated print time and filament weight on the second line. For Orca-style names such as
`pizza-Body_PLA_37m33s.gcode`, the caption is `pizza-Body` with `PLA` below it.
The material and duration suffix is removed only when the whole pattern matches;
other names keep their existing caption without the extension. Materials fall
back to G-code metadata when no suffix is present. Time and weight come from
G-code metadata; time uses two units at most (for example `37m33s` or `2h7m`).
Open a card to see its full relative path,
which distinguishes files with the same name in different folders. Visible cards
show a small spinner while their previews load; it stops when loading finishes
or fails. Names that exceed the card's width scroll horizontally; short names
stay still. Long paths also scroll in the file-details modal. Off-screen cards do not animate.

Open Print from the left navigation, below Console and directly above Tools.
The navigation stays visible beside the gallery, which uses the usual tile spacing
and swipe scrolling without a scrollbar or a gallery Back button. The small
480×272 screen fits three cards across; the normal 800×480 screen fits four.
The cards use narrower previews and compact caption padding. Long material names
scroll when needed, while time and weight stay in place. A narrow empty
strip on the right of the gallery accepts swipes without touching any tiles.
Tap a card to open a modal with its preview and Back, Print and Delete image buttons. Delete's
icon uses the danger colour. There is no
long-press action. The Print entry is disabled while a job is printing or paused;
an active job also closes the gallery and any open file dialog.

Loaded previews remain cached while the gallery is open. Deleting or uploading
a file preserves the other cards and the scroll position. A changed file reloads
its own preview; leaving the print panel releases the cache and decoded images.

## Commands

From the console panel or `python3 tools/sim_send.py "SIM_PRINT"`:

| Gcode | |
| --- | --- |
| `SIM_PRINT [FILE=x]` | Start a print, or cancel the running one |
| `SIM_SHUTDOWN`, `SIM_READY` | Klipper shuts down / restarts |
| `SIM_PROMPT` | Show an `action:prompt` dialog |
| `SIM_UPLOAD [FILE=x]` | A new file arrives |
| `SIM_DISCONNECT [SECONDS=5]` | Moonraker drops every client |
| `SIM_KLIPPER_STOP [SECONDS=10]` | The Klipper service stops, then starts |
| `SIM_MESSAGE TEXT=.. [TYPE=error]` | AFC message |
| `PREP` | AFC lane checks and coloured Box Turtle artwork, using the simulated sensors |
| `SIM_AFC_CONSOLE` | AFC PREP plus warning HTML and a Unicode debug timing sample; leaves the message queue alone |
| `SIM_HH_CONSOLE [SAMPLE=all\|boot\|thin\|sensors]` | Replay captured Happy Hare HTML, alignment spaces and Unicode diagrams |
| `SIM_RUNOUT [LANE=x]`, `SIM_FAULT [LANE=x]` | AFC runout and lane fault |
| `SIM_FAULT GATE=n`, `SIM_ERROR` | Happy Hare jammed gate and pause_locked error |

`python3 tools/sim_send.py --rpc server.info` sends any Moonraker method; `--listen 10` prints notifications.

`python3 tools/sim_smoke.py` checks the simulator end to end.

### Shared notification banner

Start `./sim.sh`, then send these from the simulated console or through
`python3 tools/sim_send.py "COMMAND"`:

| Scenario | Commands / check |
| --- | --- |
| Short message and compact height | `M117 Ready` — X sends an empty `M117` to clear it; sending `M117 Ready` again shows it again |
| Long wrapped message | `M117 A long printer message that should wrap across several lines while keeping the close button at the far right of the banner` |
| Klipper command error | `G28`, then `G0 X99999` — X dismisses; repeat to show it again |
| AFC warning queue | `SIM_MESSAGE TEXT="First warning" TYPE=warning`, then `SIM_MESSAGE TEXT="Second warning" TYPE=warning` — X acknowledges the first and exposes the next |
| AFC console error | `SIM_MESSAGE TEXT="Test AFC error" TYPE=error` — detailed error uses the shared banner |
| AFC fault | `SIM_FAULT LANE=lane1` — use the MMU header's Reset action; sending the same command again also toggles the fault |
| Happy Hare fault | Start `SIM_BACKEND=hh ./sim.sh`, then `SIM_ERROR` — X hides the message without resetting; the MMU header still offers Reset |
| Priority and tab changes | Show a warning, then trigger a Klipper error; the error takes priority. Dismiss it to reveal the warning. Change tabs while either is visible |
| Klipper goes offline | Show a banner, then send `SIM_KLIPPER_STOP SECONDS=10`, `SIM_SHUTDOWN`, or `SIM_DISCONNECT` — all banners clear immediately while the UI waits for the printer |
| Prompt interaction | `SIM_PROMPT`, then trigger a Klipper command error — the error belongs to the prompt while it is open |

The simulator supplies backend events; click X and Reset in the UI to check
their effects. `make GUPPY_SDL=1 BUILD_DIR=build/sdl test-notification-banner`
also checks the shared LVGL widget without a window: one overlay, short-message
height, wrapping, priority, dismissal, reset callbacks and fault recurrence.

### AFC console formatting (#442)

Run `python3 tools/sim_send.py "SIM_AFC_CONSOLE"` with the console open to
reproduce AFC's formatting. `PREP` reports the current sensors without moving
motors. With `SIM_LANES=5`, the staged fifth lane produces `NOT LOADED` and
the error artwork; the default four lanes produce the ready artwork.

These samples follow the sibling AFC plugin's `AFC_prep`, `AFC_BoxTurtle`,
`AFC_extruder`, `AFC_logger` and `afcDeltaTime` output. The mock uses Box Turtle
artwork labelled with its existing `CANVAS_1` unit. AFC sends raw messages with
unquoted `<span class=success--text>` (also `error`, `warning`, `info`, `primary`
and `secondary`), adjacent spans, embedded newlines and repeated literal spaces.
The ready artwork even leaves its final success span open upstream; the mock
preserves that. These logger messages have no `// ` prefix and aren't split
into separate responses. Warnings retain HTML, while errors use `!! `.

No tab-based alignment or HTML character entities were found in these AFC
paths. Its debug timings contain the literal Unicode character `Δ`; the sample
uses fixed example durations. The screenshot in
[#442](https://github.com/pellcorp/grumpyscreen/issues/442) shows Happy Hare's
inline `style="color:..."`, `<b>` tags and Unicode diagrams, which differ from
these AFC samples. These commands supply console input for investigating the
renderer.

The console translates `<span style="color:#RRGGBB">` (also `#RGB`) into
inline foreground colours. AFC's `success--text`, `warning--text` and
`error--text` classes use the same theme colours as echo, warning and error
lines; `primary--text`, `secondary--text` and `info--text` use their theme
colours. Nested spans restore the surrounding colour when closed. Unknown
span colours inherit the surrounding colour. `<b>` tags are removed while
retaining their text. Turning console colours off still removes these tags.
Sent commands remain literal.

Non-COSMOS builds also include a small DejaVu Sans Mono fallback containing
28 Happy Hare punctuation, box-drawing, block, arrow and sensor glyphs. Their
advance widths match the existing 14px console font so the diagrams retain
their columns. COSMOS builds omit the fallback source and keep the original
font without a fallback. Emoji are outside this subset.

To regenerate `assets/dejavusans_mono_hh_14.c`, run
`python3 tools/make_console_font.py /path/to/DejaVuSansMono.ttf`. It uses
`lv_font_conv@1.5.3`; the generated file records the source font SHA256.
The font used here is from
[Matplotlib v3.10.0](https://github.com/matplotlib/matplotlib/blob/v3.10.0/lib/matplotlib/mpl-data/fonts/ttf/DejaVuSansMono.ttf),
with its licence in `assets/LICENSE-DejaVu.txt`. Only the generated subset
is needed to build the application. `make test-console-font` checks actual
LVGL lookup, bitmap availability and character widths; run with a separate
`BUILD_DIR` and `COSMOS=true` to check exclusion too.

### Happy Hare console formatting (#442)

Start `SIM_BACKEND=hh ./sim.sh`, open the console, then run:

```
python3 tools/sim_send.py "SIM_HH_CONSOLE"
python3 tools/sim_send.py "SIM_HH_CONSOLE SAMPLE=boot"
```

`boot` reproduces the screenshot's four-gate map, bypass column, black/magenta
swatches, selected-gate triangle and bold UNLOADED filament diagram. `thin`
shows a loaded filament using thin lines and an arrow tip. `sensors` includes
triggered/clear sensor circles, the encoder marker, loading arrows and a
coloured warning. The default `all` replays all three groups. These are fixed
formatting examples, independent of the simulator's current gate count or
filament state; replaying them does not change that state.

The captures use the sibling checkout's actual `_mmu_visual_to_string`,
`_get_filament_char`, `get_filament_position_string` and
`MmuLogger._color_message` methods. They retain adjacent inline colour spans,
nested `<b>`/`<span>` tags, literal Unicode glyphs and non-breaking spaces
(`U+00A0`), rather than HTML entities or tabs. Happy Hare uses Klipper's
`respond_info`, so each line arrives prefixed with `// `.

`tools/moonsim/hh_console_samples.json` records the source commit. Regenerate
it with `python3 tools/make_hh_console_samples.py ../Happy-Hare`; the generator
executes the formatting methods against plain data using Happy Hare's own
filament-display test adapter, without booting Klipper. The simulator itself
needs only the checked-in captures, with no sibling checkout or extra Python
dependencies. Check replay over the websocket and in console history with
`python3 tools/sim_smoke.py --hh-console-only`.
