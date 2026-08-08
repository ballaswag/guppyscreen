#ifndef __CFS_OVERRIDE_PY_H__
#define __CFS_OVERRIDE_PY_H__

/*
 * klipper/cfs_override.py, embedded so the binary can install it itself.
 *
 * GENERATED - do not edit. Edit klipper/cfs_override.py and run
 * scripts/sync_cfs_helper.py.
 */

static const char *CFS_OVERRIDE_PY = R"PYSRC(
# cfs_override.py - manual material/colour assignment for the Creality CFS,
# applied live, with no klipper restart.
#
# Creality's `box` module is closed source and only reads its persistent state
# (tn_data.json) at startup. This module wraps its get_status() and overlays the
# manual assignments, so Moonraker - and therefore the slicer - see the change on
# the next poll, instantly.
#
# Commands:
#   CFS_SET_SLOT UNIT=1 SLOT=C CODE=004001 COLOR=0FF8080
#   CFS_CLEAR_SLOT UNIT=1 SLOT=C
#   CFS_LIST_OVERRIDES
#
# Install: copy to /usr/share/klipper/klippy/extras/ and add [cfs_override] to
#          printer.cfg. GuppyScreen does this on its own.

import json
import logging
import os

SAVE_PATH = "/usr/data/cfs_override.json"

# catalogue id (5 digits) -> base type. Only used to fill the 4th field of
# same_material when the code was not already present.
TYPE_BY_ID = {
    "01001": "PLA", "01002": "PLA", "01004": "PLA", "01601": "PLA",
    "02001": "PLA-CF", "03001": "ABS", "04001": "PLA", "05001": "PLA",
    "06001": "PETG", "06002": "PETG", "06003": "PETG-CF", "07001": "ABS",
    "07002": "PC", "08001": "PLA", "09001": "PLA", "09002": "PLA",
    "10001": "TPU", "11001": "PA", "12002": "PA-CF", "12003": "PA-CF",
    "12004": "PA612-CF", "12005": "PA6-CF", "13001": "PLA-CF",
    "14001": "PLA", "15001": "PLA", "16001": "TPU", "17001": "PLA",
    "18001": "PLA", "19001": "ASA", "29001": "PLA",
    "00001": "PLA", "00002": "PLA", "00003": "PETG", "00004": "ABS",
    "00005": "TPU", "00006": "PLA-CF", "00007": "ASA", "00008": "PA",
    "00009": "PA-CF", "00010": "BVOH", "00011": "PVA", "00012": "HIPS",
    "00013": "PET-CF", "00014": "PETG-CF", "00015": "PA6-CF",
    "00016": "PAHT-CF", "00017": "PPS", "00018": "PPS-CF", "00019": "PP",
    "00020": "PET", "00021": "PC", "00022": "PA-CF", "00023": "PA",
    "00024": "PLA", "00025": "PA-CF", "00026": "TPU", "00027": "PETG-GF",
    "00031": "PP-CF", "00032": "PCTG", "00033": "ASA-CF", "00034": "PA-GF",
    "00035": "PLA",
}

EMPTY = ("-1", "None", "none", "")


def _is_empty(v):
    return str(v) in EMPTY


class CfsOverride:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.overrides = {}          # "T1C" -> {"code": ..., "color": ...}
        self._load()

        self.printer.register_event_handler("klippy:ready", self._on_ready)

        gcode = self.printer.lookup_object("gcode")
        gcode.register_command("CFS_SET_SLOT", self.cmd_CFS_SET_SLOT,
                               desc="Assign material/colour to a CFS slot")
        gcode.register_command("CFS_CLEAR_SLOT", self.cmd_CFS_CLEAR_SLOT,
                               desc="Remove a slot's manual assignment")
        gcode.register_command("CFS_LIST_OVERRIDES", self.cmd_CFS_LIST,
                               desc="List CFS manual assignments")
        gcode.register_command("CFS_MAP_TOOL", self.cmd_CFS_MAP_TOOL,
                               desc="Map a print tool to a physical CFS slot")
        gcode.register_command("CFS_MAP_RESET", self.cmd_CFS_MAP_RESET,
                               desc="Reset the CFS tool mapping to identity")

    # ------------------------------------------------------------- persistence
    def _load(self):
        try:
            if os.path.exists(SAVE_PATH):
                with open(SAVE_PATH, "r") as fh:
                    data = json.load(fh)
                if isinstance(data, dict):
                    self.overrides = data
        except Exception:
            logging.exception("cfs_override: could not read %s", SAVE_PATH)
            self.overrides = {}

    def _save(self):
        try:
            tmp = SAVE_PATH + ".tmp"
            with open(tmp, "w") as fh:
                json.dump(self.overrides, fh)
            os.rename(tmp, SAVE_PATH)
        except Exception:
            logging.exception("cfs_override: could not write %s", SAVE_PATH)

    # ---------------------------------------------------------- box patching
    def _on_ready(self):
        box = self.printer.lookup_object("box", None)
        if box is None:
            logging.info("cfs_override: no 'box' object; nothing to do")
            return
        if getattr(box, "_cfs_override_installed", False):
            return
        original = box.get_status

        def patched(eventtime, _orig=original, _self=self):
            status = _orig(eventtime)
            return _self._apply(status)

        box.get_status = patched
        box._cfs_override_installed = True
        logging.info("cfs_override: wrapped box.get_status (%d assignments)",
                     len(self.overrides))

    def _tnn_map(self):
        """Creality keeps the tool -> physical slot mapping here."""
        box = self.printer.lookup_object("box", None)
        if box is None:
            return None
        state = getattr(box, "box_state", None)
        if state is None:
            return None
        return getattr(state, "Tnn_map", None)

    def _apply(self, status):
        if not isinstance(status, dict):
            return status
        if not self.overrides:
            tnn = self._tnn_map()
            if tnn is not None:
                status = dict(status)
                status["tnn_map"] = dict(tnn)
            return status
        try:
            out = dict(status)
            for slot_id, ov in self.overrides.items():
                if len(slot_id) < 3:
                    continue
                unit, letter = slot_id[:-1], slot_id[-1].upper()
                idx = ord(letter) - ord("A")
                unit_st = out.get(unit)
                if not isinstance(unit_st, dict):
                    continue
                unit_st = dict(unit_st)
                mt = list(unit_st.get("material_type") or [])
                cv = list(unit_st.get("color_value") or [])
                if 0 <= idx < len(mt) and ov.get("code"):
                    mt[idx] = ov["code"]
                if 0 <= idx < len(cv) and ov.get("color"):
                    cv[idx] = ov["color"]
                unit_st["material_type"] = mt
                unit_st["color_value"] = cv
                out[unit] = unit_st
            out["same_material"] = self._same_material(out, status)
            tnn = self._tnn_map()
            if tnn is not None:
                out["tnn_map"] = dict(tnn)
            return out
        except Exception:
            logging.exception("cfs_override: failed applying assignments")
            return status

    def _same_material(self, out, original):
        # types the firmware already knows, so we do not lose them
        type_by_code = {}
        try:
            for entry in (original.get("same_material") or []):
                if isinstance(entry, (list, tuple)) and len(entry) >= 4:
                    type_by_code[str(entry[0])] = entry[3]
        except Exception:
            pass

        groups, order = {}, []
        for unit_no in range(1, 5):
            unit = "T%d" % unit_no
            unit_st = out.get(unit)
            if not isinstance(unit_st, dict):
                continue
            mt = unit_st.get("material_type") or []
            cv = unit_st.get("color_value") or []
            for i in range(min(len(mt), len(cv))):
                code, colour = str(mt[i]), str(cv[i])
                if _is_empty(code):
                    continue
                label = "%s%s" % (unit, chr(ord("A") + i))
                key = (code, colour)
                if key not in groups:
                    name = type_by_code.get(code) \
                        or TYPE_BY_ID.get(code[-5:], "") \
                        or TYPE_BY_ID.get(code, "")
                    groups[key] = [code, colour, [], name]
                    order.append(key)
                groups[key][2].append(label)
        return [groups[k] for k in order]

    # -------------------------------------------------------------- commands
    def _slot_id(self, gcmd):
        unit = gcmd.get_int("UNIT", 1, minval=1, maxval=4)
        slot = gcmd.get("SLOT", "A").strip().upper()
        if slot.isdigit():
            slot = chr(ord("A") + int(slot))
        if slot not in ("A", "B", "C", "D"):
            raise gcmd.error("SLOT must be A-D or 0-3")
        return "T%d%s" % (unit, slot)

    def cmd_CFS_SET_SLOT(self, gcmd):
        slot_id = self._slot_id(gcmd)
        code = gcmd.get("CODE", None)
        colour = gcmd.get("COLOR", gcmd.get("COLOUR", None))
        if code is None and colour is None:
            raise gcmd.error("provide CODE and/or COLOR")
        entry = dict(self.overrides.get(slot_id, {}))
        if code is not None:
            entry["code"] = code.strip()
        if colour is not None:
            c = colour.strip().lstrip("#")
            if len(c) == 6:          # the CFS uses 7 chars: prefix + RRGGBB
                c = "0" + c
            entry["color"] = c
        self.overrides[slot_id] = entry
        self._save()
        gcmd.respond_info("CFS %s -> %s / %s" % (slot_id,
                                                 entry.get("code", "="),
                                                 entry.get("color", "=")))

    def cmd_CFS_CLEAR_SLOT(self, gcmd):
        slot_id = self._slot_id(gcmd)
        if self.overrides.pop(slot_id, None) is not None:
            self._save()
            gcmd.respond_info("CFS %s: manual assignment removed" % slot_id)
        else:
            gcmd.respond_info("CFS %s: had no manual assignment" % slot_id)

    SLOTS = tuple("T%d%s" % (u, s) for u in range(1, 5) for s in "ABCD")

    def _slot_name(self, raw, what):
        v = str(raw).strip().upper()
        if v.isdigit():
            i = int(v)
            if 0 <= i < len(self.SLOTS):
                return self.SLOTS[i]
            raise self.printer.command_error("%s out of range (0-15)" % what)
        if v in self.SLOTS:
            return v
        raise self.printer.command_error("%s must be T1A-T4D or 0-15" % what)

    def cmd_CFS_MAP_TOOL(self, gcmd):
        tnn = self._tnn_map()
        if tnn is None:
            raise gcmd.error("CFS mapping not available")
        tool = self._slot_name(gcmd.get("TOOL"), "TOOL")
        slot = self._slot_name(gcmd.get("SLOT"), "SLOT")
        tnn[tool] = slot
        gcmd.respond_info("CFS mapping: %s -> %s" % (tool, slot))

    def cmd_CFS_MAP_RESET(self, gcmd):
        tnn = self._tnn_map()
        if tnn is None:
            raise gcmd.error("CFS mapping not available")
        for k in list(tnn.keys()):
            tnn[k] = k
        gcmd.respond_info("CFS mapping reset")

    def cmd_CFS_LIST(self, gcmd):
        if not self.overrides:
            gcmd.respond_info("CFS: no manual assignments")
            return
        for slot_id in sorted(self.overrides):
            ov = self.overrides[slot_id]
            gcmd.respond_info("%s -> code=%s color=%s"
                              % (slot_id, ov.get("code", "-"),
                                 ov.get("color", "-")))


def load_config(config):
    return CfsOverride(config)
)PYSRC";

#endif /* __CFS_OVERRIDE_PY_H__ */
