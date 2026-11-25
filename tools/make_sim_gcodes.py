#!/usr/bin/env python3
"""Generate the simulator's test prints into tools/sim_gcodes/.

Each file is real, printable klipper gcode laid out the way OrcaSlicer writes
it -- header block, embedded PNG thumbnails, EXCLUDE_OBJECT_DEFINE labels,
SET_PRINT_STATS_INFO layer counters, a short start and end sequence, and the
config block moonraker reads its metadata from. The parts are deliberately
tiny so a whole print runs in a few minutes on a real machine, and in well
under one in the simulator at its default speed-up.

The thumbnails are rendered from the same geometry that is sliced, so what
the print list shows is what the file prints.

    python3 tools/make_sim_gcodes.py          # rewrites tools/sim_gcodes/
"""
import base64
import math
import os
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sim_gcodes")

BED = 256.0
NOZZLE = 0.4
WIDTH = 0.45            # extrusion width
FIL_D = 1.75
FIL_AREA = math.pi * (FIL_D / 2) ** 2
DENSITY = {"PLA": 1.24, "PETG": 1.27, "ABS": 1.04, "ASA": 1.07, "TPU": 1.21}
SPOOL_COST = 20.0       # per kg, for the cost line


# ------------------------------------------------------------------ geometry
def rect(x, y, w, h):
    return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]


def circle(cx, cy, r, n=28):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def area(poly):
    return sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1]
               for i in range(len(poly))) / -2.0


def ccw(poly):
    return poly if area(poly) > 0 else poly[::-1]


def offset(poly, d):
    """Shift every edge of a CCW polygon inward by d, joining with mitres."""
    n = len(poly)
    lines = []
    for i in range(n):
        (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln, dx / ln           # left normal: inside for CCW
        lines.append(((x0 + nx * d, y0 + ny * d), (dx, dy)))
    out = []
    for i in range(n):
        (p, r), (q, s) = lines[i - 1], lines[i]
        cross = r[0] * s[1] - r[1] * s[0]
        if abs(cross) < 1e-9:
            out.append(q)
            continue
        t = ((q[0] - p[0]) * s[1] - (q[1] - p[1]) * s[0]) / cross
        out.append((p[0] + t * r[0], p[1] + t * r[1]))
    return out


def scan_lines(poly, spacing, vertical, phase=0.0):
    """Infill: segments where evenly spaced lines cross the polygon's inside."""
    pts = [(y, x) for x, y in poly] if vertical else poly
    ys = [p[1] for p in pts]
    y = min(ys) + spacing / 2 + phase
    segs, flip = [], False
    while y < max(ys):
        xs = []
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i - 1], pts[i]
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        row = [(xs[i], xs[i + 1]) for i in range(0, len(xs) - 1, 2) if xs[i + 1] - xs[i] > 0.6]
        for a, b in (row[::-1] if flip else row):
            seg = ((b, y), (a, y)) if flip else ((a, y), (b, y))
            segs.append([(q, p) for p, q in seg] if vertical else list(seg))
        flip = not flip
        y += spacing
    return segs


class Part:
    """A prism: an outline extruded to a height, coloured per layer by tool."""

    def __init__(self, name, outline, height, tool=0, bands=None, hull_label=True):
        self.name = name
        self.outline = ccw(outline)
        self.height = height
        self.tool = tool
        self.bands = bands or []          # [(z_from, tool)], ascending
        self.hull_label = hull_label      # False: label with the true outline

    def tool_at(self, z):
        t = self.tool
        for z0, tool in self.bands:
            if z >= z0 - 1e-6:
                t = tool
        return t


# ------------------------------------------------------------------ thumbnails
def png_rgba(w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ear_clip(poly):
    pts = list(range(len(poly)))
    tris = []

    def inside(p, a, b, c):
        def s(u, v, w):
            return (u[0] - w[0]) * (v[1] - w[1]) - (v[0] - w[0]) * (u[1] - w[1])
        d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))

    guard = 0
    while len(pts) > 3 and guard < 10000:
        guard += 1
        for k in range(len(pts)):
            i0, i1, i2 = pts[k - 1], pts[k], pts[(k + 1) % len(pts)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) <= 0:
                continue                    # reflex corner
            if any(inside(poly[j], a, b, c) for j in pts if j not in (i0, i1, i2)):
                continue
            tris.append((a, b, c))
            pts.pop(k)
            break
    tris.append(tuple(poly[i] for i in pts))
    return tris


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def render(parts, colours, size, ss=3, az=-35.0, el=32.0):
    """Orthographic, lit, z-buffered render of the parts on a clear background."""
    ca, sa = math.cos(math.radians(az)), math.sin(math.radians(az))
    ce, se = math.cos(math.radians(el)), math.sin(math.radians(el))
    xs = [p[0] for part in parts for p in part.outline]
    ys = [p[1] for part in parts for p in part.outline]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2

    def view(x, y, z):
        x, y = x - cx, y - cy
        xr, yr = x * ca - y * sa, x * sa + y * ca
        return xr, z * ce + yr * se, yr * ce - z * se      # screen x, screen up, depth

    light = (-0.45, -0.55, 0.70)
    ln = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ln for c in light)

    tris = []  # (3 view points, rgb)
    for part in parts:
        zs = sorted({0.0, part.height} | {z for z, _ in part.bands if 0 < z < part.height})
        for z0, z1 in zip(zs, zs[1:]):
            rgb = hex_rgb(colours[part.tool_at(z0)])

            def lit(n, rgb=rgb):
                k = 0.38 + 0.62 * max(0.0, sum(a * b for a, b in zip(n, light)))
                return tuple(min(255, int(c * k + 18)) for c in rgb)

            o = part.outline
            for i in range(len(o)):
                (x0, y0), (x1, y1) = o[i], o[(i + 1) % len(o)]
                dx, dy = x1 - x0, y1 - y0
                ln2 = math.hypot(dx, dy)
                col = lit((dy / ln2, -dx / ln2, 0.0))
                quad = [view(x0, y0, z0), view(x1, y1, z0), view(x1, y1, z1), view(x0, y0, z1)]
                tris += [((quad[0], quad[1], quad[2]), col), ((quad[0], quad[2], quad[3]), col)]
            if z1 == zs[-1]:
                col = lit((0.0, 0.0, 1.0))
                for a, b, c in ear_clip(o):
                    tris.append(((view(*a, z1), view(*b, z1), view(*c, z1)), col))

    allp = [p for t, _ in tris for p in t]
    minx, maxx = min(p[0] for p in allp), max(p[0] for p in allp)
    miny, maxy = min(p[1] for p in allp), max(p[1] for p in allp)
    big = size * ss
    scale = big * 0.86 / max(maxx - minx, maxy - miny)
    ox = big / 2 - (minx + maxx) / 2 * scale
    oy = big / 2 + (miny + maxy) / 2 * scale

    zbuf = [1e18] * (big * big)
    cbuf = [None] * (big * big)
    for (a, b, c), col in tris:
        P = [(ox + p[0] * scale, oy - p[1] * scale, p[2]) for p in (a, b, c)]
        (x0, y0, d0), (x1, y1, d1), (x2, y2, d2) = P
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for py in range(max(0, int(min(y0, y1, y2))), min(big, int(max(y0, y1, y2)) + 1)):
            for px in range(max(0, int(min(x0, x1, x2))), min(big, int(max(x0, x1, x2)) + 1)):
                fx, fy = px + 0.5, py + 0.5
                w0 = ((y1 - y2) * (fx - x2) + (x2 - x1) * (fy - y2)) / den
                w1 = ((y2 - y0) * (fx - x2) + (x0 - x2) * (fy - y2)) / den
                w2 = 1 - w0 - w1
                if w0 < -1e-6 or w1 < -1e-6 or w2 < -1e-6:
                    continue
                d = w0 * d0 + w1 * d1 + w2 * d2
                i = py * big + px
                if d < zbuf[i]:
                    zbuf[i], cbuf[i] = d, col

    out = bytearray(size * size * 4)
    n = ss * ss
    for y in range(size):
        for x in range(size):
            r = g = b = cnt = 0
            for sy in range(ss):
                row = (y * ss + sy) * big + x * ss
                for sx in range(ss):
                    c = cbuf[row + sx]
                    if c:
                        r += c[0]; g += c[1]; b += c[2]; cnt += 1
            o = (y * size + x) * 4
            if cnt:
                out[o:o + 4] = bytes((r // cnt, g // cnt, b // cnt, 255 * cnt // n))
    return png_rgba(size, size, out)


def thumbnail_block(png, w, h):
    b64 = base64.b64encode(png).decode()
    lines = ["; thumbnail begin %dx%d %d" % (w, h, len(b64))]
    lines += ["; " + b64[i:i + 78] for i in range(0, len(b64), 78)]
    lines.append("; thumbnail end")
    return lines


# ------------------------------------------------------------------ slicing
class Writer:
    """Emits moves and keeps the books: time, filament per tool, position."""

    def __init__(self):
        self.lines = []
        self.x = self.y = self.z = 0.0
        self.f = 3000.0
        self.time = 0.0
        self.e_mm = {}
        self.tool = 0

    def emit(self, line):
        self.lines.append(line)

    def travel(self, x, y, z=None, f=9000):
        d = math.hypot(x - self.x, y - self.y)
        if d > 2.0:
            self.emit("G1 E-0.8 F2100")
            self.time += 0.8 / 35
        if z is not None and abs(z - self.z) > 1e-6:
            self.emit("G1 Z%.3f F720" % z)
            self.time += abs(z - self.z) / 12
            self.z = z
        self.emit("G1 X%.3f Y%.3f F%d" % (x, y, f))
        self.time += d / (f / 60)
        if d > 2.0:
            self.emit("G1 E0.8 F2100")
            self.time += 0.8 / 35
        self.x, self.y = x, y

    def extrude(self, x, y, height, f):
        d = math.hypot(x - self.x, y - self.y)
        e = d * WIDTH * height / FIL_AREA
        self.emit("G1 X%.3f Y%.3f E%.5f F%d" % (x, y, e, f) if f != self.f else
                  "G1 X%.3f Y%.3f E%.5f" % (x, y, e))
        self.f = f
        self.time += d / (f / 60)
        self.e_mm[self.tool] = self.e_mm.get(self.tool, 0.0) + e
        self.x, self.y = x, y

    def loop(self, poly, height, f):
        self.travel(*poly[0])
        for p in poly[1:] + poly[:1]:
            self.extrude(p[0], p[1], height, f)


def hull(points):
    pts = sorted(set((round(x, 3), round(y, 3)) for x, y in points))

    def half(seq):
        out = []
        for p in seq:
            while len(out) >= 2 and ((out[-1][0] - out[-2][0]) * (p[1] - out[-2][1])
                                     - (out[-1][1] - out[-2][1]) * (p[0] - out[-2][0])) <= 0:
                out.pop()
            out.append(p)
        return out
    return half(pts)[:-1] + half(pts[::-1])[:-1]


def label(part):
    xs = [p[0] for p in part.outline]
    ys = [p[1] for p in part.outline]
    ring = hull(part.outline) if part.hull_label else part.outline
    ring = list(ring) + [ring[0]]
    poly = ",".join("[%.3f,%.3f]" % p for p in ring)
    return "EXCLUDE_OBJECT_DEFINE NAME=%s CENTER=%.3f,%.3f POLYGON=[%s]" % (
        part.name, (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, poly)


def fmt_time(s):
    s = int(round(s))
    d, s = divmod(s, 86400)
    h, s = divmod(s, 3600)
    m, s = divmod(s, 60)
    out = ("%dd " % d if d else "") + ("%dh " % h if h else "") + ("%dm " % m if m or h else "")
    return out + "%ds" % s


def slice_file(spec):
    parts = spec["parts"]
    filaments = spec["filaments"]            # [(colour, type, name)], one per tool
    layer_h = spec.get("layer_height", 0.2)
    first_h = spec.get("first_layer_height", 0.2)
    temps = spec.get("temps", (215, 60))
    top = max(p.height for p in parts)
    zs = [first_h]
    while zs[-1] + layer_h <= top + 1e-6:
        zs.append(round(zs[-1] + layer_h, 3))
    total_layers = len(zs)

    w = Writer()
    changes = 0
    used_tools = set()
    body = w.lines

    # --- start sequence: heat, home, prime, the way a minimal klipper
    # machine start gcode does it
    w.emit("M140 S%d ; bed" % temps[1])
    w.emit("M104 S150 ; nozzle soak while homing")
    w.emit("G28")
    w.emit("M190 S%d" % temps[1])
    w.emit("M109 S%d" % temps[0])
    w.emit("G90")
    w.emit("M83 ; relative extrusion")
    w.emit("G1 Z5 F720")
    w.z = 5.0
    w.emit("G1 X5 Y5 F9000")
    w.x, w.y = 5.0, 5.0
    w.emit("G1 Z0.3 F720")
    w.z = 0.3
    w.emit(";TYPE:Prime line")
    w.extrude(80, 5, 0.3, 1500)
    w.extrude(80, 5.5, 0.3, 1500)
    w.extrude(5, 5.5, 0.3, 1500)
    w.time += 60 + temps[0] / 3       # heating, roughly
    for extra in spec.get("start_extra", []):
        w.emit(extra)
    w.emit("SET_PRINT_STATS_INFO TOTAL_LAYER=%d" % total_layers)

    tool = spec.get("first_tool", 0)
    multi = len(filaments) > 1
    if multi:
        w.emit("T%d" % tool)
        w.tool = tool
    used_tools.add(tool)

    for li, z in enumerate(zs):
        h = first_h if li == 0 else layer_h
        w.emit(";BEFORE_LAYER_CHANGE")
        w.emit(";%g" % z)
        w.emit("G92 E0")
        w.emit(";LAYER_CHANGE")
        w.emit(";Z:%.3f" % z)
        w.emit(";HEIGHT:%.3f" % h)
        w.emit("SET_PRINT_STATS_INFO CURRENT_LAYER=%d" % (li + 1))
        for hook in spec.get("layer_hooks", {}).get(li + 1, []):
            w.emit(hook)
        if li == 1:
            w.emit("M106 S255")
        speed_perim = 1200 if li == 0 else 3000
        speed_fill = 1800 if li == 0 else 4800
        live = [p for p in parts if p.height >= z - 1e-6]
        # group by tool so a layer changes tool as few times as a slicer would
        live.sort(key=lambda p: (p.tool_at(z) != w.tool, p.tool_at(z)))
        for part in live:
            t = part.tool_at(z)
            if multi and t != w.tool:
                w.emit("T%d" % t)
                w.tool = t
                w.time += 25          # a tool change, about what an MMU spends
                changes += 1
            used_tools.add(t)
            if spec.get("labels", True):
                w.emit("EXCLUDE_OBJECT_START NAME=%s" % part.name)
            w.travel(*offset(part.outline, WIDTH / 2)[0], z=z)
            w.emit(";TYPE:Outer wall")
            w.loop(offset(part.outline, WIDTH / 2), h, speed_perim)
            w.emit(";TYPE:Inner wall")
            inner = offset(part.outline, WIDTH * 1.5)
            w.loop(inner, h, speed_perim)
            solid = li < 2 or z > part.height - 0.6
            w.emit(";TYPE:%s" % ("Solid infill" if solid else "Sparse infill"))
            fill_poly = offset(part.outline, WIDTH * 2.2)
            for a, b in scan_lines(fill_poly, WIDTH if solid else 4.0, vertical=li % 2 == 1):
                w.travel(*a)
                w.extrude(b[0], b[1], h, speed_fill)
            if spec.get("labels", True):
                w.emit("EXCLUDE_OBJECT_END NAME=%s" % part.name)
        for hook in spec.get("after_layer", {}).get(li + 1, []):
            w.emit(hook)

    # --- end sequence: retract, lift, park, everything off
    w.emit(";TYPE:Custom")
    w.emit("G1 E-1.5 F2100")
    w.emit("G91")
    w.emit("G1 Z5 F720")
    w.emit("G90")
    w.emit("G1 X10 Y%.0f F9000" % (BED - 10))
    w.emit("M104 S0")
    w.emit("M140 S0")
    w.emit("M106 S0")
    w.emit("M84")
    w.time += 10

    # --- header / footer, OrcaSlicer's layout
    n_tools = len(filaments)
    mm = [w.e_mm.get(t, 0.0) for t in range(n_tools)]
    grams = [m * FIL_AREA / 1000 * DENSITY.get(filaments[t][1], 1.24) for t, m in enumerate(mm)]
    cost = [g / 1000 * SPOOL_COST for g in grams]

    head = ["; HEADER_BLOCK_START",
            "; generated by OrcaSlicer 2.3.0 on 2026-10-12 at 09:30:00",
            "; total layer number: %d" % total_layers,
            "; total filament length [mm] : %s" % ",".join("%.2f" % m for m in mm),
            "; total filament volume [cm^3] : %s" % ",".join("%.2f" % (m * FIL_AREA / 1000) for m in mm),
            "; total filament weight [g] : %s" % ",".join("%.2f" % g for g in grams),
            "; model printing time: %s; total estimated time: %s" % (fmt_time(w.time * 0.95), fmt_time(w.time)),
            "; filament_density: %s" % ",".join("%.2f" % DENSITY.get(f[1], 1.24) for f in filaments),
            "; filament_diameter: %s" % ",".join("1.75" for _ in filaments),
            "; max_z_height: %.2f" % zs[-1],
            "; HEADER_BLOCK_END", ""]
    if spec.get("thumbnail", True):
        cols = [f[0] for f in filaments]
        head.append("; THUMBNAIL_BLOCK_START")
        for size in (32, 300):
            head += thumbnail_block(render(parts, cols, size, ss=3 if size > 100 else 5), size, size)
            head.append(";")
        head.append("; THUMBNAIL_BLOCK_END")
        head.append("")
    head += ["; EXECUTABLE_BLOCK_START"]
    if spec.get("labels", True):
        head += [label(p) for p in sorted(parts, key=lambda p: p.name)]
    head.append("")

    unused = [t for t in range(n_tools) if t not in used_tools]
    tail = ["; EXECUTABLE_BLOCK_END", "",
            "; filament used [mm] = %s" % ", ".join("%.2f" % m for m in mm),
            "; filament used [cm3] = %s" % ", ".join("%.2f" % (m * FIL_AREA / 1000) for m in mm),
            "; filament used [g] = %s" % ", ".join("%.2f" % g for g in grams),
            "; filament cost = %s" % ", ".join("%.2f" % c for c in cost),
            "; total filament used [g] = %.2f" % sum(grams),
            "; total filament cost = %.2f" % sum(cost),
            "; total layers count = %d" % total_layers,
            *(["; total filament change = %d" % changes] if changes else []),
            "; estimated printing time (normal mode) = %s" % fmt_time(w.time),
            "",
            "; CONFIG_BLOCK_START",
            "; extruder_colour = %s" % ";".join(f[0] for f in filaments),
            "; filament_colour = %s" % ";".join(f[0] for f in filaments),
            "; filament_density = %s" % ",".join("%.2f" % DENSITY.get(f[1], 1.24) for f in filaments),
            "; filament_diameter = %s" % ",".join("1.75" for _ in filaments),
            "; filament_settings_id = %s" % ";".join('"%s"' % f[2] for f in filaments),
            "; filament_type = %s" % ";".join(f[1] for f in filaments),
            "; first_layer_bed_temperature = %s" % ",".join(str(temps[1]) for _ in filaments),
            "; first_layer_height = %.2f" % first_h,
            "; first_layer_temperature = %s" % ",".join(str(temps[0]) for _ in filaments),
            "; layer_height = %.2f" % layer_h,
            "; nozzle_diameter = %s" % ",".join("%.1f" % NOZZLE for _ in filaments),
            "; nozzle_temperature = %s" % ",".join(str(temps[0]) for _ in filaments),
            "; nozzle_temperature_initial_layer = %s" % ",".join(str(temps[0]) for _ in filaments),
            "; print_settings_id = 0.20mm Standard @Generic",
            "; printer_model = Generic Klipper Printer",
            "; single_extruder_multi_material = %d" % (1 if multi else 0),
            "; printer_settings_id = Grumpy Sim 0.4 nozzle",
            "; CONFIG_BLOCK_END", ""]
    if unused:
        tail.insert(1, "; unused tools in this file: %s" % ", ".join("T%d" % t for t in unused))
    return "\n".join(head + body + tail)


# ------------------------------------------------------------------ the library
RED, BLUE, GREEN, YELLOW, ORANGE, PURPLE, CYAN, WHITE, BLACK, TEAL = (
    "#F44336", "#2196F3", "#4CAF50", "#FFEB3B", "#FF9800", "#9C27B0",
    "#00BCD4", "#FFFFFF", "#212121", "#00897B")
C = BED / 2


def bracket(x, y):
    """A U: the middle of its bounding box sits outside its own outline."""
    return [(x, y), (x + 36, y), (x + 36, y + 32), (x + 26, y + 32), (x + 26, y + 10),
            (x + 10, y + 10), (x + 10, y + 32), (x, y + 32)]


LIBRARY = {
    # the default quick check: one small cube
    "quick_cube.gcode": dict(
        parts=[Part("quick_cube.stl_id_0_copy_0", rect(C - 8, C - 8, 16, 16), 6.0)],
        filaments=[(RED, "PLA", "Generic PLA")]),

    # several parts for the exclude object panel; the bracket is labelled
    # with its true outline (not a hull) so its centre marker falls outside
    # the shape -- the case where the marker must be tappable by itself
    "plate_5_parts.gcode": dict(
        parts=[Part("bracket.stl_id_0_copy_0", bracket(70, 140), 4.0, hull_label=False),
               Part("knob.stl_id_1_copy_0", circle(150, 160, 9), 4.0),
               Part("knob.stl_id_1_copy_1", circle(175, 160, 9), 4.0),
               Part("spacer.stl_id_2_copy_0", rect(70, 90, 30, 14), 3.0),
               Part("washer.stl_id_3_copy_0", circle(160, 105, 12), 2.0)],
        filaments=[(BLUE, "PETG", "Generic PETG")], temps=(240, 80)),

    "calibration/first_layer.gcode": dict(
        parts=[Part("first_layer_%d.stl_id_%d_copy_0" % (i, i),
                    rect(C - 60 + (i % 3) * 45, C - 40 + (i // 3) * 45, 30, 30), 0.2)
               for i in range(6)],
        filaments=[(ORANGE, "PLA", "Generic PLA")]),

    # nozzle steps down 5 degrees every 5 layers, like a temperature tower
    "calibration/temp_tower.gcode": dict(
        parts=[Part("temp_tower.stl_id_0_copy_0", rect(C - 10, C - 6, 20, 12), 6.0)],
        filaments=[(PURPLE, "PLA", "Generic PLA")], temps=(230, 60),
        layer_hooks={n: ["M104 S%d" % (230 - 5 * (n // 5))] for n in range(5, 31, 5)}),

    # no EXCLUDE_OBJECT labels: the exclude panel's empty state
    "no_object_labels.gcode": dict(
        parts=[Part("box.stl_id_0_copy_0", rect(C - 10, C - 10, 20, 20), 3.0)],
        filaments=[(TEAL, "PLA", "Generic PLA")], labels=False),

    # no embedded thumbnail: the print list's placeholder
    "no_thumbnail.gcode": dict(
        parts=[Part("box.stl_id_0_copy_0", rect(C - 10, C - 10, 20, 20), 3.0)],
        filaments=[(WHITE, "PLA", "Generic PLA")], thumbnail=False),

    # pauses itself at layer 6, the way a filament-change-at-height does
    "pause_at_layer_6.gcode": dict(
        parts=[Part("coaster.stl_id_0_copy_0", circle(C, C, 18), 2.4)],
        filaments=[(YELLOW, "PLA", "Generic PLA")],
        layer_hooks={6: ["M117 Change filament", "PAUSE"]}),

    # a move off the bed at layer 4: klipper's "Move out of range" ends the
    # print in the error state
    "error_out_of_range.gcode": dict(
        parts=[Part("doomed.stl_id_0_copy_0", rect(C - 10, C - 10, 20, 20), 3.0)],
        filaments=[(BLACK, "ABS", "Generic ABS")], temps=(250, 100),
        after_layer={4: ["G1 X300 Y128 F9000 ; past the 256mm bed"]}),

    "spare_parts/a_really_long_file_name_to_check_how_lists_truncate_it_v2_final_FINAL.gcode": dict(
        parts=[Part("clip.stl_id_0_copy_0", rect(C - 12, C - 4, 24, 8), 2.0)],
        filaments=[(GREEN, "PLA", "Generic PLA")]),

    # four colours in bands up a tag, plus a fifth tool the file defines but
    # never uses -- Orca still lists it, with zero filament
    "keychain_4colour.gcode": dict(
        parts=[Part("keychain.stl_id_0_copy_0",
                    [(C - 25, C - 9), (C + 18, C - 9), (C + 25, C), (C + 18, C + 9), (C - 25, C + 9)],
                    3.2, tool=0, bands=[(0.8, 1), (1.6, 2), (2.4, 3)])],
        filaments=[(PURPLE, "PLA", "Purple PLA"), (YELLOW, "PLA", "Yellow PLA"),
                   (ORANGE, "PLA", "Orange PLA"), (CYAN, "PLA", "Cyan PLA"),
                   (WHITE, "PLA", "White PLA")]),

    # six tools, one per part, two parts the same colour, mixed materials,
    # more tools than the default four lanes
    "plate_6colour.gcode": dict(
        parts=[Part("cube_%s.stl_id_%d_copy_0" % (n, i), rect(70 + (i % 3) * 42, 100 + (i // 3) * 42, 24, 24),
                    3.0, tool=i)
               for i, n in enumerate(["red", "blue", "green", "yellow", "black", "red2"])],
        filaments=[(RED, "PLA", "Red PLA"), (BLUE, "PETG", "Blue PETG"), (GREEN, "ABS", "Green ABS"),
                   (YELLOW, "PLA", "Yellow PLA"), (BLACK, "PETG", "Black PETG"), (RED, "PLA", "Red PLA")],
        temps=(235, 70)),

    # two colours swapped every two layers: lots of tool changes quickly
    "stripes_2colour.gcode": dict(
        parts=[Part("stripes.stl_id_0_copy_0", circle(C, C, 14), 2.4, tool=0,
                    bands=[(z / 10, (k + 1) % 2) for k, z in enumerate(range(4, 24, 4))])],
        filaments=[(BLUE, "PLA", "Blue PLA"), (WHITE, "PLA", "White PLA")]),

    # A few screens of distinct previews for the tiled file browser: tall,
    # wide, round, multi-part and multi-colour models, with readable names.
    "hex_knob_PLA_4m32s.gcode": dict(
        parts=[Part("hex_knob", circle(C, C, 10, n=6), 5.0)],
        filaments=[(ORANGE, "PLA", "Orange PLA")]),
    "cable_clip_PETG_6m32s.gcode": dict(
        parts=[Part("cable_clip", bracket(C - 14, C - 10), 2.0, hull_label=False)],
        filaments=[(GREEN, "PETG", "Green PETG")], temps=(240, 80)),
    "round_coaster_PLA_6m43s.gcode": dict(
        parts=[Part("round_coaster", circle(C, C, 20), 1.2)],
        filaments=[(TEAL, "PLA", "Teal PLA")]),
    "tall_test_tower_PLA_4m8s.gcode": dict(
        parts=[Part("test_tower", rect(C - 4, C - 4, 8, 8), 12.0)],
        filaments=[(PURPLE, "PLA", "Purple PLA")]),
    "desk_name_plate_PLA_5m32s.gcode": dict(
        parts=[Part("name_plate", rect(C - 32, C - 6, 64, 12), 1.6)],
        filaments=[(BLUE, "PLA", "Blue PLA")]),
    "spacer_set.gcode": dict(
        parts=[Part("spacer_%d" % i, circle(C - 18 + i * 18, C, 6), 2.0 + i)
               for i in range(3)],
        filaments=[(YELLOW, "PLA", "Yellow PLA")]),
    "two_colour_badge.gcode": dict(
        parts=[Part("badge", circle(C, C, 16, n=6), 2.0, bands=[(1.0, 1)])],
        filaments=[(RED, "PLA", "Red PLA"), (WHITE, "PLA", "White PLA")]),
    "Small parts tray_PLA_5m20s.gcode": dict(
        parts=[Part("tray", rect(C - 18, C - 12, 36, 24), 1.0)],
        filaments=[(CYAN, "PLA", "Cyan PLA")]),
    "a_really_long_filename_for_the_thumbnail_grid_with_spaces and details.gcode": dict(
        parts=[Part("long_label", rect(C - 20, C - 4, 40, 8), 1.2)],
        filaments=[(GREEN, "PLA", "Green PLA")]),

    # The basename is shared intentionally: remote thumbnail caching must
    # keep the red and blue previews separate across folders. The second
    # folder also checks navigation beyond a single directory level.
    "spare_parts/knob.gcode": dict(
        parts=[Part("red_knob", circle(C, C, 9), 4.0)],
        filaments=[(RED, "PLA", "Red PLA")]),
    "spare_parts/blue/knob.gcode": dict(
        parts=[Part("blue_knob", circle(C, C, 9, n=6), 4.0)],
        filaments=[(BLUE, "PLA", "Blue PLA")]),
    'spare_parts/hinge "left".gcode': dict(
        parts=[Part("left_hinge", rect(C - 12, C - 5, 24, 10), 2.0)],
        filaments=[(WHITE, "PETG", "White PETG")], temps=(240, 80)),
}


def main():
    for name, spec in LIBRARY.items():
        path = os.path.join(OUT, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        text = slice_file(spec)
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print("%-90s %7.1f KB" % (name, len(text) / 1024))


if __name__ == "__main__":
    main()
