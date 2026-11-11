#!/bin/sh
# Applies each mutant patch in mutants/ to a private copy of the project and runs the
# checkers on it. A mutant is caught when a checker fails or the build breaks.
# Usage: scripts/run_mutants.sh [PROJECT_ROOT]
# The project itself is never modified. Each mutant runs in its own temporary copy.
set -eu
ROOT=$(cd "${1:-$(dirname "$0")/..}" && pwd)
PATCHES="$ROOT/mutants"
N_DIFF=${N_DIFF:-200000}
ITERS=${ITERS:-1000000}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/framecheck-mut.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

total=0
caught=0
for patch in "$PATCHES"/*.patch; do
  name=$(basename "$patch" .patch)
  total=$((total + 1))
  copy="$WORK/$name"
  mkdir -p "$copy"
  (cd "$ROOT" && tar -cf - --exclude=./build --exclude=./bench/results.json --exclude=__pycache__ .) | tar -C "$copy" -xf -
  if ! (cd "$copy" && patch -p1 -s < "$patch"); then
    echo "$name: PATCH-DOES-NOT-APPLY"
    continue
  fi
  failed=""
  (cd "$copy" && make -s asan-test > "$WORK/$name.asan.log" 2>&1) || failed="$failed asan-test"
  (cd "$copy" && make -s diff-fuzz N="$N_DIFF" SEED=1 > "$WORK/$name.diff.log" 2>&1) || failed="$failed diff-fuzz"
  (cd "$copy" && make -s fuzz ITERS="$ITERS" SEED=1 > "$WORK/$name.fuzz.log" 2>&1) || failed="$failed fuzz"
  if [ -n "$failed" ]; then
    caught=$((caught + 1))
    echo "$name: CAUGHT by$failed"
  else
    echo "$name: SURVIVED"
  fi
  rm -rf "$copy"
done
echo "mutants caught: $caught of $total"
