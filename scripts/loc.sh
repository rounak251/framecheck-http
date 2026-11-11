#!/bin/sh
# Counts lines of non-test C++ source. The set is every .cpp, .hpp and .h file
# outside tests/, oracle/, bench/, diff/ and build/, which is the set that
# `cloc --exclude-dir=tests,oracle,bench` counts. cloc is not installed everywhere,
# so this uses find, wc and awk.
#
# Two numbers are printed:
#   total non-test C++ lines: physical lines, blank and comment lines included.
#                             This is the metric the amended line-count criterion uses.
#   code-only lines:          physical lines that are not blank and not a // comment.
#                             This is the closer match to cloc's code column.
# The spec table is not counted. Block comments are not used in this project,
# and the script stops if one appears, so the code-only count stays honest.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FILES=$(find . \( -path ./tests -o -path ./oracle -o -path ./bench -o -path ./build -o -path ./diff \) -prune -o \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -type f -print | LC_ALL=C sort)
if grep -l '/\*' $FILES >/dev/null 2>&1; then
  echo "loc: a block comment was found; the code-only count does not handle it" >&2
  exit 1
fi
for f in $FILES; do
  printf '%6d %s\n' "$(wc -l < "$f" | tr -d ' ')" "$f"
done
TOTAL=$(for f in $FILES; do cat "$f"; done | wc -l | tr -d ' ')
CODE=$(for f in $FILES; do cat "$f"; done | awk '!/^[ \t]*$/ && !/^[ \t]*\/\// {n++} END {print n+0}')
echo "total non-test C++ lines: $TOTAL"
echo "code-only lines: $CODE"
