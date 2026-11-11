#!/bin/sh
# Checks docs/threat-model.md. It fails if there are fewer than six threats, or if a
# threat's Test column names a test that the unit-test binary does not run.
# A name counts only when the binary prints it as passing ("ok"), so a test that is
# commented out, misspelled, left unregistered or failing fails the check. A row whose Test column says
# "out of scope" needs no test.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL="$ROOT/docs/threat-model.md"
BIN="$ROOT/build/unit_tests"
WORK=$(mktemp)
RUN=$(mktemp)
trap 'rm -f "$WORK" "$RUN"' EXIT

if [ ! -x "$BIN" ]; then
  echo "check_threats: $BIN is missing; run make test first" >&2
  exit 1
fi
# The test run may fail on its own. That is reported by make test, not here.
"$BIN" > "$RUN" 2>/dev/null || true

grep -E '^\| T-[0-9]{2} \|' "$MODEL" > "$WORK" || true
THREATS=$(grep -c . "$WORK" || true)
if [ "$THREATS" -lt 6 ]; then
  echo "check_threats: found $THREATS threats, need at least 6" >&2
  exit 1
fi

FAILED=0
MAPPED=0
OUT=0
while IFS= read -r row; do
  id=$(printf '%s\n' "$row" | awk -F'|' '{gsub(/ /,"",$2); print $2}')
  cell=$(printf '%s\n' "$row" | awk -F'|' '{gsub(/^ +| +$/,"",$(NF-1)); print $(NF-1)}')
  if [ "$cell" = "out of scope" ]; then
    OUT=$((OUT + 1))
    continue
  fi
  names=$(printf '%s\n' "$cell" | grep -o '`[A-Za-z_][A-Za-z0-9_]*`' | tr -d '`' || true)
  if [ -z "$names" ]; then
    echo "check_threats: $id names no test and is not out of scope" >&2
    FAILED=1
    continue
  fi
  for name in $names; do
    # Only a passing test counts. A FAIL line means the threat's test is broken.
    if grep -qE "^ok +$name\$" "$RUN"; then
      MAPPED=$((MAPPED + 1))
    else
      echo "check_threats: $id names test '$name', which the unit-test binary does not run" >&2
      FAILED=1
    fi
  done
done < "$WORK"

echo "threats=$THREATS mapped_tests=$MAPPED out_of_scope=$OUT"
if [ "$FAILED" -ne 0 ]; then
  echo "check_threats: FAILED" >&2
  exit 1
fi
echo "check_threats: ok"
