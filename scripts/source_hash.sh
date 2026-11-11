#!/bin/sh
# Prints one SHA-256 over the C++ and Python sources the benchmark depends on.
# There is no git in this build, so the benchmark records this hash instead.
# Works with shasum (macOS) or sha256sum (Linux).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
if command -v shasum >/dev/null 2>&1; then
  SHA="shasum -a 256"
else
  SHA="sha256sum"
fi
find include src harness fuzz bench tests diff -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.py' \) \
  | LC_ALL=C sort | xargs $SHA | $SHA | cut -d' ' -f1
