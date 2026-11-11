#!/usr/bin/env python3
"""Differential runner: sends the same seeded cases to the C++ harness and to the oracle.

Usage:
    python3 diff/run_diff.py --harness build/harness --cases 200000 --seed 1 --out build/diff

The two programs read docs/harness-protocol.md on stdin and write one line per
case on stdout. This script compares the answers as the protocol says:

  * status: complete counts as accept, error as reject, incomplete must match.
  * consumed and body_length, when both sides say complete.
  * error_class and error_offset, when both sides say error.
  * close, when both sides say error.

A bad-input line on either side stops the run, because it is a bug in the
generator or the runner, not a disagreement.

Writes out/cases.jsonl, out/cpp.jsonl, out/oracle.jsonl and out/disagreements.jsonl.
Prints only counts, so two runs with the same seed print the same lines.
The timing line is the only line that differs between runs.
"""

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import generate  # noqa: E402

ROOT = os.path.dirname(HERE)
ORACLE = os.path.join(ROOT, "oracle", "reference.py")
ACCEPT_MAP = {"complete": "accept", "error": "reject", "incomplete": "incomplete"}


def write_cases(path, seed, count):
    ids = []
    with open(path, "w", encoding="ascii") as f:
        for case_id, data in generate.cases(seed, count):
            line = {"id": case_id, "hex": data.hex()}
            f.write(json.dumps(line, sort_keys=True, separators=(",", ":")) + "\n")
            ids.append(case_id)
    return ids


def run_program(argv, stdin_path, stdout_path):
    with open(stdin_path, "rb") as fin, open(stdout_path, "wb") as fout:
        proc = subprocess.run(argv, stdin=fin, stdout=fout, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        raise SystemExit("program failed: %s (exit %d)" % (argv[0], proc.returncode))


def shape_problem(out):
    """Checks that one answer has the fields the protocol requires for its status."""
    status = out.get("status")
    if status == "complete":
        ok = isinstance(out.get("consumed"), int) and isinstance(out.get("body_length"), int)
        ok = ok and out.get("close") is False and out.get("error_class") is None
        return None if ok else "complete fields"
    if status == "error":
        ok = out.get("close") is True and isinstance(out.get("error_class"), str)
        ok = ok and isinstance(out.get("error_offset"), int)
        return None if ok else "error fields"
    if status == "incomplete":
        return None if out.get("close") is False else "incomplete close"
    return "unknown status %r" % (status,)


def compare(cpp, orc):
    """Returns None when the two answers agree, or a short reason when they do not."""
    for side, out in (("cpp", cpp), ("oracle", orc)):
        if out.get("status") == "bad-input":
            return "bad-input-" + side
        problem = shape_problem(out)
        if problem:
            return "shape-" + side + ": " + problem
    if ACCEPT_MAP[cpp["status"]] != ACCEPT_MAP[orc["status"]]:
        return "status: cpp=%s oracle=%s" % (cpp["status"], orc["status"])
    if cpp["status"] == "error":
        if cpp["error_class"] != orc["error_class"]:
            return "error_class: cpp=%s oracle=%s" % (cpp["error_class"], orc["error_class"])
        if cpp["error_offset"] != orc["error_offset"]:
            return "error_offset: cpp=%s oracle=%s" % (cpp["error_offset"], orc["error_offset"])
    if cpp["status"] == "complete":
        if cpp["consumed"] != orc["consumed"]:
            return "consumed: cpp=%s oracle=%s" % (cpp["consumed"], orc["consumed"])
        if cpp["body_length"] != orc["body_length"]:
            return "body_length: cpp=%s oracle=%s" % (cpp["body_length"], orc["body_length"])
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--cases", type=int, required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    cases_path = os.path.join(args.out, "cases.jsonl")
    cpp_path = os.path.join(args.out, "cpp.jsonl")
    oracle_path = os.path.join(args.out, "oracle.jsonl")
    disagree_path = os.path.join(args.out, "disagreements.jsonl")

    start = time.time()
    ids = write_cases(cases_path, args.seed, args.cases)
    run_program([args.harness], cases_path, cpp_path)
    run_program([sys.executable, ORACLE], cases_path, oracle_path)
    elapsed = time.time() - start

    with open(cpp_path, encoding="ascii") as f:
        cpp_lines = f.read().splitlines()
    with open(oracle_path, encoding="ascii") as f:
        oracle_lines = f.read().splitlines()
    with open(cases_path, encoding="ascii") as f:
        case_lines = f.read().splitlines()
    if not (len(ids) == len(cpp_lines) == len(oracle_lines) == len(case_lines)):
        raise SystemExit("line counts differ: cases=%d cpp=%d oracle=%d" % (
            len(ids), len(cpp_lines), len(oracle_lines)))

    counts = {"cases": 0, "agree": 0, "disagree": 0, "complete": 0, "incomplete": 0, "error": 0,
              "bad_input": 0}
    reasons = {}
    with open(disagree_path, "w", encoding="ascii") as dis:
        for i, case_id in enumerate(ids):
            cpp = json.loads(cpp_lines[i])
            orc = json.loads(oracle_lines[i])
            case = json.loads(case_lines[i])
            if cpp.get("id") != case_id or orc.get("id") != case_id:
                raise SystemExit("id mismatch at line %d" % (i + 1))
            counts["cases"] += 1
            if cpp["status"] in ("complete", "incomplete", "error"):
                counts[cpp["status"]] += 1
            if cpp["status"] == "bad-input" or orc["status"] == "bad-input":
                counts["bad_input"] += 1
            reason = compare(cpp, orc)
            if reason is None:
                counts["agree"] += 1
                continue
            counts["disagree"] += 1
            key = reason.split(":")[0]
            reasons[key] = reasons.get(key, 0) + 1
            dis.write(json.dumps({"id": case_id, "hex": case["hex"], "reason": reason,
                                  "cpp": cpp, "oracle": orc}, sort_keys=True) + "\n")

    print("seed=%d cases=%d" % (args.seed, counts["cases"]))
    for key in sorted(counts):
        if key != "cases":
            print("%s=%d" % (key, counts[key]))
    for key in sorted(reasons):
        print("disagreement_kind[%s]=%d" % (key, reasons[key]))
    print("timing: %.2f s, %.0f cases per second" % (elapsed, counts["cases"] / elapsed if elapsed else 0))
    return 0 if counts["disagree"] == 0 and counts["bad_input"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
