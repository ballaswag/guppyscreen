#!/usr/bin/env python3
"""Keep src/cfs_override_py.h in sync with klipper/cfs_override.py.

The klipper helper is embedded in the binary so it can install itself on first
run. klipper/cfs_override.py is the source of truth; this regenerates the C++
header from it.

    python3 scripts/sync_cfs_helper.py            # regenerate
    python3 scripts/sync_cfs_helper.py --check    # exit 1 if out of date
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY_SRC = os.path.join(ROOT, "klipper", "cfs_override.py")
HEADER = os.path.join(ROOT, "src", "cfs_override_py.h")

TEMPLATE = '''#ifndef __CFS_OVERRIDE_PY_H__
#define __CFS_OVERRIDE_PY_H__

/*
 * klipper/cfs_override.py, embedded so the binary can install it itself.
 *
 * GENERATED - do not edit. Edit klipper/cfs_override.py and run
 * scripts/sync_cfs_helper.py.
 */

static const char *CFS_OVERRIDE_PY = R"PYSRC(
{body}
)PYSRC";

#endif /* __CFS_OVERRIDE_PY_H__ */
'''


def render():
    with open(PY_SRC) as fh:
        body = fh.read().strip("\n")
    if ')PYSRC"' in body:
        sys.exit("cfs_override.py contains the raw-string delimiter; cannot embed")
    return TEMPLATE.format(body=body)


def main():
    want = render()
    check = "--check" in sys.argv
    current = None
    if os.path.exists(HEADER):
        with open(HEADER) as fh:
            current = fh.read()
    if current == want:
        print("cfs_override_py.h is up to date")
        return 0
    if check:
        print("cfs_override_py.h is out of date; run scripts/sync_cfs_helper.py",
              file=sys.stderr)
        return 1
    with open(HEADER, "w") as fh:
        fh.write(want)
    print("regenerated %s" % os.path.relpath(HEADER, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
