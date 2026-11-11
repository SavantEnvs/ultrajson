#!/usr/bin/env bash
#
# mayhem/build.sh — ujson is a CPython C extension, so the libFuzzer harnesses embed CPython and have the
# ujson C sources (sanitized + SanCov) linked straight into the harness binary as a built-in module.
# Builds: (1) the three fuzz harnesses + their -standalone reproducers, (2) upstream's normal extension
# build + a launcher for its pytest suite (test.sh only runs it). Re-runnable and air-gapped: the only
# inputs are the committed tree and the apt packages baked into the image.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC="${SRC:-/mayhem}"
cd "$SRC"

# SanCov on the fuzzed library, unconditionally (also when SANITIZER_FLAGS is empty).
COV="-fsanitize=fuzzer-no-link"
PYINC="$(python3-config --includes)"
PYLIBS="-L$(python3-config --configdir) $(python3-config --embed --libs)"
UJ="$SRC/src/ujson"
DC="$UJ/deps/double-conversion/double-conversion"
OUT="$SRC/build/mayhem-fuzz"   # build/ is gitignored upstream, so `git clean -ffdX` removes it
mkdir -p "$OUT"

# 1) ujson + double-conversion, sanitized and instrumented. (Compiler reached by name through PATH.)
UJFLAGS=(-O1 -D_GNU_SOURCE -DUJSON_VERSION='"0.0.0"' -I"$UJ" -I"$DC" $PYINC)
objs=()
for f in ujson encode decode; do
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $COV "${UJFLAGS[@]}" -c "$UJ/$f.c" -o "$OUT/$f.o"
  objs+=("$OUT/$f.o")
done
for f in "$DC"/*.cc "$UJ/dconv_wrapper.cc"; do
  o="$OUT/dc_$(basename "${f%.cc}").o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $COV "${UJFLAGS[@]}" -c "$f" -o "$o"
  objs+=("$o")
done

# 2) Leak detection off at build time (SPEC §6.2 item 15); the embedded interpreter retains state by design.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

# 3) Each harness, twice: libFuzzer binary and run-once standalone reproducer.
for h in ujson_fuzzer json_differential_fuzzer hypothesis_structured_fuzzer; do
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $COV -O1 $PYINC -c "$SRC/mayhem/$h.c" -o "$OUT/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $COV $LIB_FUZZING_ENGINE "$OUT/$h.o" "${objs[@]}" "$OUT/lsan_off.o" \
    $PYLIBS -lm -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $COV "$OUT/$h.o" "$OUT/standalone_main.o" "${objs[@]}" "$OUT/lsan_off.o" \
    $PYLIBS -lm -o "$SRC/$h-standalone"
done

# 4) Upstream's own extension build with its NORMAL flags (no sanitizers, no -gdwarf-3), for the pytest suite.
(
  unset SANITIZER_FLAGS DEBUG_FLAGS CFLAGS CXXFLAGS LDFLAGS
  export SETUPTOOLS_SCM_PRETEND_VERSION=0.0.0
  python3 setup.py -q build_ext --force --build-lib "$SRC/build/test-lib" --build-temp "$SRC/build/test-tmp"
)
ls "$SRC"/build/test-lib/ujson*.so >/dev/null

# 5) ELF launcher for the pytest runner (the sabotage check neuters non-system executables, so the
#    suite must be started through one of ours).
$CC $DEBUG_FLAGS -O1 -DPY_SCRIPT='"/mayhem/mayhem/run_tests.py"' -o "$SRC/ujson-tests" "$SRC/mayhem/test_launcher.c"

echo "build.sh complete"
