# Creality Filament System (CFS) support

This fork adds first-class CFS support to GuppyScreen: a panel to see and set
what is in each slot, and a filament mapping step before every print.

Tested on a K1 SE running Creality's stock klipper with
[Guilouz's Helper Script](https://github.com/Guilouz/Creality-Helper-Script),
CFS firmware 1.1.3. The same code paths apply to the K1, K1C and K1 Max.

On a printer with no CFS attached nothing changes: the menu button is hidden,
the mapping dialog never opens and the klipper helper is not installed.

## The CFS panel

A **CFS** button on the main menu opens a panel showing, for the selected unit:

- the four slots (A-D) with their real colour, product name and remaining %
- unit temperature and humidity, prominently
- **T1-T4** tabs, only for units that are actually connected

Tapping **Assign** on a slot opens a two-step picker:

1. **material type** - PLA, PETG, ABS, ASA, TPU, PC, PA, the CF variants... (26)
2. **product** - the varieties of that type (Hyper PLA, CR-PLA Matte, Generic PLA...)

plus a 20-swatch colour grid and a live preview.

**Apply is instant.** No confirmation dialog, no restart. The change lands in the
`box` printer object, so Moonraker - and any slicer reading it - sees it on the
next poll. This matters: assigning filament is something you do on every spool
change, and requiring a klipper restart each time makes the feature unusable.

Load / Unload ask for confirmation, since they move the toolhead and heat the
nozzle.

## Filament mapping

Pressing **Print** on any file opens a mapping dialog first - the basic AMS/CFS
gesture that was missing:

| the file wants | | feeds from |
|---|---|---|
| 1 PLA 9 g | -> | T1C |
| 2 PETG unused | -> | T1B |
| 3 PLA 2 g | -> | T1D |

Each row shows the colour, type and weight the sliced file expects; filaments
the file declares but never extrudes are marked `unused`. Tapping the right side
cycles through the slots that actually hold filament, showing their real colour.

It defaults to the slicer's 1:1 order, so confirming without touching anything
behaves exactly as before. The dialog is shown for single-filament files too -
that is the case where you most often want to print with a different colour than
the one you sliced with.

Confirming writes klipper's own tool -> slot table (`box.box_state.Tnn_map`) live
and then starts the print, so it applies to the job about to run.

The mapping is also exposed as `tnn_map` inside the `box` status, and is
reachable from the console:

```
CFS_MAP_TOOL TOOL=0 SLOT=T1C     # or TOOL=T1A
CFS_MAP_RESET
```

## Why a klipper helper is needed

Creality's `box` klipper module is closed source (`box.py` is a thin wrapper
around `box_wrapper.cpython-38-mipsel-linux-gnu.so`). It reads its persistent
state only at startup and exposes no write path. This was established on real
hardware, not assumed:

- Klipper exposes 31 endpoints on this machine; the CFS contributes exactly one,
  `box/get_box_cfg`, which is read-only.
- `BOX_MODIFY_TN_DATA` and `BOX_MODIFY_TN_INNER_DATA` accept only `ADDR`. Checked
  by passing non-numeric values to 29 candidate parameter names - klipper reports
  "unable to parse" even for *optional* parameters, so any that existed would have
  surfaced. None did. The internal symbol is `modify_tn_save_data`: it saves
  memory to disk, it does not load.
- It does not read `material_modify_info.json`.
- Nothing reloads `tn_data.json` hot. `BOX_CREATE_CONNECT`,
  `BOX_COMMUNICATION_TEST`, `BOX_GET_VERSION_SN`, `BOX_SET_PRE_LOADING` and
  `BOX_UPDATE_SAME_MATERIAL_LIST` were all tried.

So writing `tn_data.json` directly only takes effect after a klipper restart.

`klipper/cfs_override.py` (~270 lines) wraps `box.get_status()` and overlays the
manual assignments on the way out, recomputing `same_material` and injecting
`tnn_map`. It never modifies Creality's module and never touches its files.

**It is embedded in the binary and installs itself** on first run: it writes
`klippy/extras/cfs_override.py`, appends `[cfs_override]` to `printer.cfg` and
triggers one klipper restart. The user does nothing and sees no dialog.
Assignments live in `/usr/data/cfs_override.json` and survive reboots.

`src/cfs_override_py.h` is a verbatim copy of `klipper/cfs_override.py` as a C++
raw string. `scripts/sync_cfs_helper.py` regenerates it and fails if they drift.

Verified end to end on hardware:

```
CFS_SET_SLOT UNIT=1 SLOT=B CODE=006002 COLOR=FFA800
-> same_material: ['006002', '0FFA800', ['T1B'], 'PETG']   instantly
CFS_CLEAR_SLOT UNIT=1 SLOT=B
-> back to the RFID value
CFS_MAP_TOOL TOOL=T1A SLOT=T1C
-> tnn_map: {'T1A': 'T1C', 'T1B': 'T1B', ...}              instantly
```

### Removing the helper

```sh
sed -i '/^\[cfs_override\]/d' /usr/data/printer_data/config/printer.cfg
rm -f /usr/share/klipper/klippy/extras/cfs_override.py /usr/data/cfs_override.json
```

## Material codes

The stored code is `<prefix><5-digit catalogue id>`. The prefix varies by
firmware - `0` for Creality and `7` for Generic on a K1 SE, `1` on a K1C. Lookup
therefore **ignores the prefix** and compares the last five digits, exactly like
`creality_cfs_lookup()` in the
[OrcaSlicer CFS fork](https://github.com/DKNS-JCC/OrcaSlicer), whose 66-entry
catalogue this shares.

`same_material` is the field OrcaSlicer's `Moonraker::parse_cfs_slots` reads, so
anything assigned from the screen shows up in the slicer on the next fetch.

## Build

Standard GuppyScreen build. No new dependencies, no Makefile changes - the
Makefile globs `src/*.cpp`, so the new files are picked up automatically.

```sh
git clone --recursive https://github.com/DKNS-JCC/guppyscreen && cd guppyscreen
(cd lv_drivers && git apply ../patches/0001-lv_driver_fb_ioctls.patch)
(cd spdlog     && git apply ../patches/0002-spdlog_fmt_initializer_list.patch)

export CROSS_COMPILE=mips-linux-gnu-      # mips-gcc720 toolchain
make nproc=$(nproc) -j$(nproc) build
```

Two notes for anyone building this today, both inherited from upstream:

- upstream's `$(MAKE) -j$(nproc)` expands `$(nproc)` as a *make* variable, not as
  a command, so it ends up as a bare `-j` (unlimited jobs) and thrashes swap on
  anything smaller than a workstation. Pass `nproc=N` on the command line, as
  above.
- GCC >= 14 turns `-Wincompatible-pointer-types` into an error, which trips
  `lv_touch_calibration`. Add `-Wno-error=incompatible-pointer-types` to
  `WARNINGS` in the Makefile.

## Install

One file, no extra steps:

```sh
scp guppyscreen root@PRINTER_IP:/usr/data/guppyscreen/guppyscreen.new
```

```sh
cd /usr/data/guppyscreen
cp guppyscreen guppyscreen.bak
mv guppyscreen.new guppyscreen && chmod +x guppyscreen
/etc/init.d/S99guppyscreen restart
```

Rolling back is `mv guppyscreen.bak guppyscreen` and a service restart.

## Files

| File | Role |
|---|---|
| `src/cfs_panel.{h,cpp}` | the CFS panel and its material/colour picker |
| `src/cfs_mapping.{h,cpp}` | the pre-print filament mapping dialog |
| `klipper/cfs_override.py` | klipper helper (source of truth) |
| `src/cfs_override_py.h` | the same helper, embedded for self-install |
| `scripts/sync_cfs_helper.py` | keeps those two in sync |
| `src/main_panel.{h,cpp}` | menu button, hidden when there is no CFS |
| `src/print_panel.{h,cpp}` | opens the mapping dialog before printing |

## Notes

- All UI strings, comments and log messages are in English, matching the rest of
  GuppyScreen, which has no i18n layer.
- A manual assignment takes precedence over RFID. **Clear** hands the slot back
  to the tag.
- The panel reuses the existing spool icon rather than adding an asset, so the
  theme files are untouched.
