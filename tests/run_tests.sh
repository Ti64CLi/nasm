#!/bin/sh
# Regression suite for the nasm on-calc assembler.
#
# Builds a host binary of the assembler core (tests/harness.c) and
# byte-compares its output against the Ndless toolchain's GNU assembler
# (nspire-as). Also checks that invalid sources fail cleanly.
#
# Usage: sh tests/run_tests.sh      (or: make test)
# Needs: gcc, and the Ndless SDK (nspire-as) in PATH or $NDLESS_SDK.
set -u
cd "$(dirname "$0")" || exit 1

# --- locate the Ndless toolchain ------------------------------------------
if ! command -v nspire-as >/dev/null 2>&1; then
  for sdk in "${NDLESS_SDK:-}" "$HOME/Documents/Nspire/Ndless/ndless-sdk"; do
    [ -n "$sdk" ] && [ -x "$sdk/bin/nspire-as" ] || continue
    PATH="$sdk/bin:$sdk/toolchain/install/bin:$PATH"
    break
  done
fi
if ! command -v nspire-as >/dev/null 2>&1; then
  echo "error: nspire-as not found (install the Ndless SDK or set NDLESS_SDK)" >&2
  exit 1
fi
OBJCOPY=arm-none-eabi-objcopy
if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
  echo "error: $OBJCOPY not found in PATH" >&2
  exit 1
fi

mkdir -p build
echo "building host assembler..."
gcc -Wall -Wextra -O1 -o build/nasm_host harness.c \
  ../settings.c ../util.c ../bytebuf.c ../optab.c || exit 1

pass=0
fail=0

# --- positive tests: byte-compare against nspire-as ------------------------
for asm in cases/t*.asm; do
  name=$(basename "$asm" .asm)
  ref="cases/$name.ref.s"
  out="build/$name.bin"

  if ! build/nasm_host "$asm" "$out" >"build/$name.log" 2>&1; then
    echo "FAIL $name (assembler reported errors)"
    sed 's/^/    /' "build/$name.log"
    fail=$((fail + 1))
    continue
  fi
  if ! nspire-as -c -o "build/$name.o" "$ref" 2>"build/$name.as.log"; then
    echo "FAIL $name (reference did not assemble)"
    sed 's/^/    /' "build/$name.as.log"
    fail=$((fail + 1))
    continue
  fi

  "$OBJCOPY" -O binary "build/$name.o" "build/$name.ref.bin"
  tail -c +5 "$out" >"build/$name.code.bin" # strip the PRG\0 header

  if cmp -s "build/$name.ref.bin" "build/$name.code.bin"; then
    pass=$((pass + 1))
  else
    echo "FAIL $name (output differs from nspire-as)"
    cmp -l "build/$name.ref.bin" "build/$name.code.bin" | head -5 | sed 's/^/    /'
    fail=$((fail + 1))
  fi
done

# --- negative tests: must fail cleanly, with the expected message ----------
for asm in cases/err_*.asm; do
  name=$(basename "$asm" .asm)

  build/nasm_host "$asm" "build/$name.bin" >"build/$name.log" 2>&1
  rc=$?

  if [ "$rc" -eq 0 ]; then
    echo "FAIL $name (expected an error, assembly succeeded)"
    fail=$((fail + 1))
    continue
  fi
  if [ "$rc" -gt 127 ]; then
    echo "FAIL $name (assembler crashed, rc=$rc)"
    fail=$((fail + 1))
    continue
  fi
  if [ -f "cases/$name.expect" ] &&
    ! grep -qF "$(cat "cases/$name.expect")" "build/$name.log"; then
    echo "FAIL $name (error message mismatch)"
    echo "    expected: $(cat "cases/$name.expect")"
    head -3 "build/$name.log" | sed 's/^/    got: /'
    fail=$((fail + 1))
    continue
  fi

  pass=$((pass + 1))
done

# --- examples must assemble -------------------------------------------------
for ex in ../examples/*.asm.tns; do
  [ -e "$ex" ] || continue
  name="ex_$(basename "$ex" .asm.tns)"

  if build/nasm_host "$ex" "build/$name.bin" >"build/$name.log" 2>&1; then
    pass=$((pass + 1))
  else
    echo "FAIL $name (example failed to assemble)"
    sed 's/^/    /' "build/$name.log"
    fail=$((fail + 1))
  fi
done

echo "-----------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
