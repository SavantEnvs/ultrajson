#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS upstream's pytest suite (built by mayhem/build.sh) through the ujson-tests ELF
# launcher and maps its summary line to CTRF. Fails when the launcher is absent or prints no summary
# (which is what a neutered program looks like).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
SRC="${SRC:-/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

if [ ! -x "$SRC/ujson-tests" ] || ! ls "$SRC"/build/test-lib/ujson*.so >/dev/null 2>&1; then
  echo "FATAL: ujson-tests launcher or build/test-lib/ujson*.so missing — run mayhem/build.sh first" >&2
  emit_ctrf pytest 0 1 0
  exit 2
fi

out="$("$SRC/ujson-tests" 2>&1)"; rc=$?
printf '%s\n' "$out" | tail -n 25

line="$(printf '%s\n' "$out" | sed -n 's/^RUNTESTS //p' | tail -1)"
if [ -z "$line" ]; then
  echo "no RUNTESTS summary from the pytest runner (rc=$rc)" >&2
  emit_ctrf pytest 0 1 0
  exit 1
fi
field() { printf '%s\n' "$line" | sed -n "s/.*$1=\([0-9][0-9]*\).*/\1/p"; }
TESTS=$(field tests); PASSED=$(field passed); FAILED=$(field failed); SKIPPED=$(field skipped)
: "${TESTS:=0}" "${PASSED:=0}" "${FAILED:=0}" "${SKIPPED:=0}"

# Known-answer floor: the suite has hundreds of cases; a collapsed collection must not pass.
if [ "$TESTS" -lt 200 ]; then
  echo "pytest collected only $TESTS tests (expected hundreds)" >&2
  emit_ctrf pytest "$PASSED" $(( FAILED + 1 )) "$SKIPPED"
  exit 1
fi
[ "$rc" -eq 0 ] || FAILED=$(( FAILED > 0 ? FAILED : 1 ))
emit_ctrf pytest "$PASSED" "$FAILED" "$SKIPPED"
