#!/usr/bin/env python3
"""Checks that every quoted phrase in docs/spec-table.md appears in the RFC text.

Usage:
    python3 scripts/check_spec_quotes.py RFC_DIR

RFC_DIR must hold rfc9112.txt and rfc9110.txt, as fetched from rfc-editor.org.
They are not stored in this repository. Quoted phrases inside code spans (the
grammar) are not quotations and are skipped. Whitespace is normalised on both
sides, so a line break in the RFC does not count as a difference. Exit status is
1 when any phrase is not found.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = os.path.join(os.path.dirname(HERE), "docs", "spec-table.md")


def normalise(text):
    return re.sub(r"\s+", " ", text).strip()


def main():
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    rfc_dir = sys.argv[1]
    rfc_text = ""
    for name in ("rfc9112.txt", "rfc9110.txt"):
        with open(os.path.join(rfc_dir, name), encoding="utf-8") as f:
            rfc_text += " " + normalise(f.read())
    with open(SPEC, encoding="utf-8") as f:
        spec_lines = f.read().splitlines()

    checked = 0
    missing = 0
    for lineno, line in enumerate(spec_lines, 1):
        line = re.sub(r"`[^`]*`", "", line)
        for phrase in re.findall(r'"([^"]{6,})"', line):
            wanted = normalise(phrase).rstrip(".,")
            checked += 1
            if wanted not in rfc_text:
                missing += 1
                print("not found, spec line %d: %s" % (lineno, wanted))
    print("quoted phrases checked=%d not_found=%d" % (checked, missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
