# MmuBackend API

`MmuPanel` renders filament slots and drives them through `MmuBackend`
(`src/mmu_backend.h`). It holds no vendor knowledge. Supporting a new
multi-material unit means implementing this interface; the panel is not touched.

Reference implementation: `src/afc_backend.{h,cpp}` (AFC).

---

## Implement

Eleven methods are pure virtual.

### Identity and detection

| Method | Called | Must do |
|---|---|---|
| `const char *vendor() const` | on selection, for the log line | return a display name |
| `const char *backup_label() const` | when the edit screen opens | optional; the vendor's own name for a runout takeover, worn by the edit screen's tile. Default `Backup`; AFC says `Infinite Spool`, Happy Hare `Endless Spool`. Two short words at most |
| `bool detect()` | when klipper starts, and again on **every** reconnect | true if this vendor's objects are in `/printer_objs/objects`. No status data exists yet |
| `bool owns_update(json &j)` | on **every** websocket notification | true if `j` contains this vendor's objects. Keep cheap — returning false skips the redraw |
| `void refresh()` | before every redraw, UI lock held | rebuild **Fill** (below) from `State` |

### Verbs

Fire and forget: send gcode and return. Do not write to `slots` — real state
arrives via the subscription. `slot` is an index into `slots`.

| Method | Contract |
|---|---|
| `void load(int slot)` | ends with that slot loaded to the tool. Whether that is a plain load or a swap out of whatever is loaded now is yours to decide — the panel asks for the end state only |
| `void unload()` | filament leaves the tool, **stays in its slot** — slot keeps `prepped`/`ready` |
| `void eject(int slot)` | filament leaves the unit — slot becomes empty |
| `void set_colour(int slot, const std::string &hex)` | `"RRGGBB"`, no `#`. `""` clears, and is only ever sent when `can_clear_colour()` |
| `void set_material(int slot, const std::string &material)` | |
| `void set_backup(int slot, int backup)` | `backup` is a slot index, `-1` clears |
| `void reset_failure()` | clear the error state / resume |

The panel closes the edit screen on `load` and `unload` and waits for
`loaded_slot` to follow, so a backend that only moved a selector here would
leave it showing stale state.

Slot indices are the panel's currency, but vendor gcode wants the vendor's own
name for the slot, and `MmuSlot::name` is a display name you may have prettied
up. Keep the raw ids in a vector indexed the same as `slots` and look them up
in the verbs — `AfcBackend::lane_id()`.

### Permissions

Six have defaults you can leave alone; `can_*` is where your own rules live.

| Method | Default | Override to say |
|---|---|---|
| `bool can_load(int slot) const` | slot is `ready`, not already loaded, unit not busy | ... and not mid-print, not in bypass, whatever else the vendor refuses |
| `bool can_unload() const` | something is loaded, unit not busy | |
| `bool can_eject(int slot) const` | slot has filament, unit not busy | |
| `bool can_set_backup(int slot) const` | more than one slot | |
| `bool can_clear_colour() const` | true | false if `set_colour(slot, "")` would not really clear it; the panel then drops its clear control |
| `void dismiss_message()` | does nothing | see below |

The panel greys a control when the answer is false and never second-guesses a
true, so this is the one place a verb's rules belong. A verb the vendor does not
have at all returns false always and its control stays greyed for good — there
is no "supports X" flag to set.

`busy()` is `activity != Idle`, which includes `Error`: a unit that needs a
reset is not a unit that should be taking motion commands.

Two gates stay in the panel and are not yours to relax: unload is offered on
the loaded slot only, and a slot must be unloaded before it can be ejected.
Those protect the nozzle rather than model a vendor.

`dismiss_message()` matters only if your vendor holds messages in a queue of
its own: an unacknowledged message sits at the head and hides every later one,
so the panel hiding it locally is not enough. `AfcBackend` sends
`AFC_CLEAR_MESSAGE`. A backend whose message is derived from live state has
nothing to pop and leaves the default alone.

MMU messages use `NotificationBanner`, the same top overlay as Klipper errors
and M117 messages, across all tabs. X hides the message and acknowledges queued
non-fault messages. A fault also offers Reset, which calls `reset_failure()`;
X alone never resets hardware. The MMU status header keeps a reset action
available after dismissal. Status updates with unchanged text do not reopen
a dismissed message; a cleared or changed message can appear again.

AFC errors also reach the console, so they show in the top banner.
`AfcBackend` pops them itself and leaves `message` empty, so the panel doesn't
show them too. While AFC is stopped, the MMU status header still offers
`Tap to reset`; its empty message does not create a second generic overlay.

### Tool mapping (optional)

Before a print, `ToolMapPanel` shows the slot feeding each tool the file uses
(from `MmuSlot::map`) and lets the user pick another. Picks are sent only on
Print, one `map_tool()` per changed tool. A vendor without a tool map keeps
the defaults and Print starts the job directly.

| Method | Default | Override to |
|---|---|---|
| `bool can_map_tool(int tool) const` | false | say whether the vendor's map command takes `T(tool)` now (AFC: a T(n) it registered; Happy Hare: one in `ttg_map`) |
| `void map_tool(int tool, int slot)` | does nothing | send that command; the new map arrives through the subscription |
| `bool can_share_slot() const` | false | true if several tools may draw from one slot (Happy Hare; AFC with `enable_multiple_mapping`). Otherwise the screen keeps one tool per slot |

---

## Fill

Set by `refresh()`, read by the panel. Clear what you cannot determine rather
than leaving stale values.

### `slots` — `std::vector<MmuSlot>`

| Field | Type | Meaning |
|---|---|---|
| `name` | `string` | display name, unique in the unit (`Lane 1`, `Gate 0`) |
| `map` | `string` | tool label(s), `T0` or `T0,T1`. `""` = unmapped |
| `material` | `string` | `""` = unset |
| `colour` | `string` | `RRGGBB` no `#`. `""` = unset, draws the checkerboard |
| `backup` | `int` | slot **index** taking over on runout, `-1` = none |
| `weight` | `int` | grams remaining, `0` = unknown |
| `full_weight` | `int` | grams on a full spool, `0` = unknown. With `weight`, the spool is drawn as a pie chart of what is left, and the cards show the percentage |
| `prepped` | `bool` | filament physically present at the slot |
| `ready` | `bool` | fed into the unit, loadable |
| `tool_loaded` | `bool` | in the toolhead |
| `can_configure` | `bool` | may colour/material be edited here. Default true; set false only when something else owns the metadata |

`prepped` and `ready` are not the same thing and the panel does not conflate
them: a slot with filament touching a prep sensor renders as full and can be
ejected, but `can_load` defaults to `ready` because that is what a vendor's
load actually needs.

### Scalars on `MmuBackend`

| Field | Type | Meaning |
|---|---|---|
| `loaded_slot` | `int` | index loaded to the tool, `-1` = none |
| `activity` | `MmuActivity` | `Idle`, `Loading`, `Unloading`, `Swapping`, `Ejecting`, `Moving`, `Error` — map your own status onto the nearest, and report `Moving` for anything else that moves. The panel owns the wording it shows |
| `message` | `string` | banner text, `""` = none |
| `message_error` | `bool` | the banner is a fault (red) rather than information (amber) |
| `error` | `bool` | unit stopped; tapping the banner calls `reset_failure()` |
| `bypass` | `bool` | unit bypassed, single spool |
| `spoolman` | `bool` | `weight` is meaningful |
| `weight_tracked` | `bool` | `weight` is meaningful: the backend counts it down as it prints (AFC's `track_filament_weight`) |

---

## Call

### Read printer state

```cpp
State *state = State::get_instance();
json &obj = state->get_data("/printer_state/mmu"_json_pointer);
json &objs = state->get_data("/printer_objs/objects"_json_pointer);
```

`/printer_objs/objects` is the object-name list (use in `detect()`);
`/printer_state/<object>` is live status. Type-check every field you read —
`is_string()`, `is_boolean()`, `is_array()` — never just `is_null()`. Fields
come and go across vendor versions and change type between them, and `refresh()`
runs on the websocket thread: a `json` type error there takes grumpyscreen down.

### Send

```cpp
ws.gcode_script("MMU_UNLOAD");                       // fire and forget
ws.send_jsonrpc(method, params, [this](json &d){});  // moonraker RPC + callback
```

`KWebSocketClient &ws` is yours to hold; the panel passes it to your constructor.

Klipper splits extended gcode parameters with `shlex`, so any value that could
contain whitespace, `#` or `;` has to be quoted before it goes into a command —
see `quote_value` in `afc_backend.cpp`.

### Request a redraw

```cpp
std::function<void()> changed;   // set by the panel, may be null
```

Call `changed()` when state arrives outside a `refresh()` — an async RPC
response, typically. It takes the UI lock, so **never call it synchronously from
inside `refresh()`** (which already runs under that lock) or you deadlock. Guard
against re-entry: the `refresh()` it triggers must not fire the same request
again.

---

## Register

In `GuppyScreen::GuppyScreen()` (`src/guppyscreen.cpp`), as a member so it
outlives the panel:

```cpp
mmu_panel.add_backend(&afc_backend);
```

MMU and Spoolman share a main navigation slot, added dynamically after Klipper
is ready. Neither has a placeholder during initial startup. MMU detection runs
first: a supported backend immediately adds the MMU button. Otherwise, the
Spoolman button is added only if Moonraker reports its Spoolman component.
With an MMU, Spoolman remains accessible from the panel's top-right shortcut;
it never adds a second main-nav button.

`MmuPanel::select_backend()` calls `detect()` on the compiled-in backends in
registration order (AFC, then Happy Hare), choosing the first match. If neither
is available, no MMU screens are allocated. Detection runs again on every
Klipper reconnect; going offline detaches the backend and closes its controls.
Once a navigation slot has been added, it stays in place for the rest of the
app session, disabled while its service is unavailable. Its selected service
does not change on reconnect, even if the other service appears. Each restart
disables both the nav entry and the MMU panel's Spoolman shortcut until fresh
detection confirms their services. A fresh app launch starts with neither
nav button again.

Visibility is automatic, with no MMU or Spoolman visibility flags.
`[mmu] materials` still configures the material catalog.

---

## Notes

Only shared concepts belong in `MmuBackend`. A feature one vendor has and
another lacks stays inside the implementation.

The panel is a flat list of slots. A vendor with several physical units reports
their slots one after another and names them so they read unambiguously
(`Gate 0`..`Gate 11`, `lane1`..`lane8`); there is no unit grouping, and a
bypass path is not a slot — report it with the `bypass` flag instead.

Where the two current vendors differ is instructive:

| | AFC | Happy Hare |
|---|---|---|
| slot | lane (`AFC_lane lane1` object per lane) | gate (index into the one `mmu` object's arrays) |
| loadable | `load` (the lane's own switch, or `loaded_to_hub` on a virtual hub) | `gate_status` not `GATE_EMPTY` |
| tool map | per-lane `map` list of `T(n)` macros | `ttg_map`, indexed by tool |
| load verb | `TOOL_LOAD` fresh, `CHANGE_TOOL` to swap | `MMU_CHANGE_TOOL` either way; unmapped gates need `MMU_SELECT` + `MMU_LOAD` |
| backup | a runout pointer per lane (`SET_RUNOUT`) | endless-spool groups, a set per gate |
| busy | `current_state` enum | `action` string, plus a pending `next_tool` for a swap |
| fault | `error_state` + a message queue to pop, `RESET_FAILURE` clears | `print_state` of `paused`/`pause_locked`, message in `reason_for_pause`, `RESUME` clears |
| clear a colour | impossible, `can_clear_colour()` false | empty `COLOR=` is accepted |
| map a tool | `SET_MAP LANE= MAP=T(n)`, which swaps maps between lanes | `MMU_TTG_MAP TOOL= GATE=` |
| metadata owner | spoolman owns a lane with a `spool_id`, so `can_configure` false there | spoolman owns the whole gate map in `pull` mode only |

None of that reaches the panel.

Report what the vendor reports; do not normalise. Whether ejecting clears the
spool colour is a per-vendor choice — AFC has a `remember_spool` option for it.
The panel mirrors metadata and only writes back what the user changed.

`can_configure` is not about filament presence — AFC accepts metadata on an
empty slot and pre-staging one is a real workflow. Set it false where the
vendor hands metadata ownership elsewhere: `AfcBackend` does it for a lane with
a spoolman spool assigned, because AFC refetches that lane's colour and
material at every PREP and a local edit would apply and then revert. The panel
greys the editing controls and marks the slot locked.

`can_clear_colour()` exists for the same reason in reverse: AFC's `SET_COLOR`
stores `"#"` for an empty argument, which is not a colour but is not empty
either, so AFC keeps treating the lane as coloured. A vendor that cannot
express "no colour" says so and the panel stops offering it.
