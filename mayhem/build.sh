#!/usr/bin/env bash
#
# mayhem/build.sh — build the yquake2 fuzz targets and the functional-check drivers.
#
# Upstream's Makefile builds whole programs (q2ded, game.so, client + renderer modules);
# the code under test is compiled here from the same sources with the Makefile's own
# portability flags (-fno-strict-aliasing -fwrapv, YQ2OSTYPE/YQ2ARCH), so the fuzzed
# code itself carries $SANITIZER_FLAGS:
#   - renderer asset loaders (src/client/refresh/files/*.c + shared + hunk)
#       -> libFuzzer: fuzz_pcx, fuzz_wal, fuzz_models (+ *-standalone)
#   - WAV parser (src/client/sound/wave.c)          -> AFL file-input: wav_file
#   - collision BSP loader (src/common/collision.c) -> AFL file-input: bsp_file
#   - pack filesystem (src/common/filesystem.c + unzip) -> AFL file-input: pak_file
#   - game module (src/game/**, as baseq2/game.so)  -> AFL file-input: savegame_file
#   - console scripts (src/common/cmdparser.c + cvar.c) -> AFL file-input: cfg_file
#     (gi.error / Com_Error end the process, so these are file-input targets)
#   - the same sources with normal flags            -> asset_dump / savegame_file / wav_file / bsp_file / pak_file / cfg_file
#     functional drivers for mayhem/test.sh
# Outputs land in mayhem/out/ (objects in mayhem/build/), both gitignored by mayhem/.gitignore.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${AFL_CC:=afl-clang-fast}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
BLD="$SRC/mayhem/build"
H="$SRC/mayhem/harnesses"
mkdir -p "$OUT" "$BLD"

# Upstream Makefile flags (see Makefile: CFLAGS overrides). BUILD_DATE is fixed so the
# savegame header (which embeds it) is identical across rebuilds.
YQ2_FLAGS=(-fno-strict-aliasing -fwrapv -DYQ2OSTYPE='"Linux"' -DYQ2ARCH='"x86_64"'
           -DBUILD_DATE='"mayhem"' -Wno-unused-result)

REF_SRCS=(src/client/refresh/files/common.c src/client/refresh/files/pcx.c
          src/client/refresh/files/wal.c src/client/refresh/files/models.c
          src/client/refresh/files/stb.c src/common/shared/shared.c src/common/md4.c
          src/backends/unix/shared/hunk.c mayhem/harnesses/ref_stubs.c)
GAME_SRCS=(src/common/shared/flash.c src/common/shared/rand.c src/common/shared/shared.c
           $(find src/game -name '*.c' | LC_ALL=C sort))

# compile <cc> <objdir> <flags...> -- <sources...>  -> prints object paths
compile() {
  local cc=$1 dir=$2; shift 2
  local flags=() srcs=()
  while [ "$1" != "--" ]; do flags+=("$1"); shift; done; shift
  srcs=("$@")
  mkdir -p "$dir"
  printf '%s\n' "${srcs[@]}" | xargs -P "$MAYHEM_JOBS" -I{} sh -c \
    'o="$0/$(echo "{}" | tr / _).o"; exec "$@" -c "{}" -o "$o"' "$dir" "$cc" "${flags[@]}"
  for s in "${srcs[@]}"; do echo "$dir/$(echo "$s" | tr / _).o"; done
}

FUZZ_CFLAGS=("${YQ2_FLAGS[@]}" $SANITIZER_FLAGS $DEBUG_FLAGS)
# SanCov for the libFuzzer targets: the loaders AND the harness TUs carry coverage
# instrumentation (unconditionally, even with an empty $SANITIZER_FLAGS); linking with
# the same flag keeps the standalone reproducers linkable without the libFuzzer runtime.
COV_FLAGS=(-fsanitize=fuzzer-no-link)

# lsan_off: compiled once per compiler family
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$BLD/lsan_off.o"

# 1) libFuzzer targets over the renderer asset loaders
mapfile -t REF_OBJS < <(compile "$CC" "$BLD/fuzz-ref" "${FUZZ_CFLAGS[@]}" "${COV_FLAGS[@]}" -- "${REF_SRCS[@]}")
$CC "${FUZZ_CFLAGS[@]}" -c "$STANDALONE_FUZZ_MAIN" -o "$BLD/standalone_main.o"
for t in pcx wal models; do
  extra=()
  [ "$t" = models ] && extra=(-Wl,--wrap=Hunk_Begin)  # see fuzz_models.c
  $CC "${FUZZ_CFLAGS[@]}" "${COV_FLAGS[@]}" -c "$H/fuzz_$t.c" -o "$BLD/fuzz_$t.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$BLD/fuzz_$t.o" "${REF_OBJS[@]}" \
    "$BLD/lsan_off.o" -lm "${extra[@]}" -o "$OUT/fuzz_$t"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${COV_FLAGS[@]}" "$BLD/standalone_main.o" "$BLD/fuzz_$t.o" "${REF_OBJS[@]}" \
    "$BLD/lsan_off.o" -lm "${extra[@]}" -o "$OUT/fuzz_$t-standalone"
done

# 2) AFL file-input targets (sanitized, instrumented with afl-clang-fast)
AFL_CFLAGS=("${YQ2_FLAGS[@]}" $SANITIZER_FLAGS $DEBUG_FLAGS)
AFL_CXX="${AFL_CC/clang-fast/clang-fast++}"
export AFL_QUIET=1
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$BLD/lsan_off_afl.o"
mapfile -t AFL_GAME_OBJS < <(compile "$AFL_CC" "$BLD/afl-game" "${AFL_CFLAGS[@]}" -- "${GAME_SRCS[@]}" mayhem/harnesses/savegame_file.c)
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${AFL_GAME_OBJS[@]}" "$BLD/lsan_off_afl.o" -lm -o "$OUT/savegame_file"
mapfile -t AFL_WAV_OBJS < <(compile "$AFL_CC" "$BLD/afl-wav" "${AFL_CFLAGS[@]}" -- src/client/sound/wave.c mayhem/harnesses/wav_file.c)
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${AFL_WAV_OBJS[@]}" "$BLD/lsan_off_afl.o" -lm -o "$OUT/wav_file"

BSP_SRCS=(src/common/collision.c src/common/shared/shared.c src/common/md4.c src/common/crc.c
          mayhem/harnesses/bsp_file.c)
mapfile -t AFL_BSP_OBJS < <(compile "$AFL_CC" "$BLD/afl-bsp" "${AFL_CFLAGS[@]}" -- "${BSP_SRCS[@]}")
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${AFL_BSP_OBJS[@]}" "$BLD/lsan_off_afl.o" -lm -o "$OUT/bsp_file"

# pack archives: filesystem.c (#included by the harness) + the bundled minizip/miniz,
# with the Makefile's server flags (-DDEDICATED_ONLY, -DNOUNCRYPT)
PAK_SRCS=(mayhem/harnesses/pak_file.c src/common/unzip/unzip.c src/common/unzip/ioapi.c
          src/common/unzip/miniz/miniz.c src/common/unzip/miniz/miniz_tdef.c
          src/common/unzip/miniz/miniz_tinfl.c src/common/shared/shared.c src/common/glob.c)
PAK_FLAGS=(-DDEDICATED_ONLY -DNOUNCRYPT)
mapfile -t AFL_PAK_OBJS < <(compile "$AFL_CC" "$BLD/afl-pak" "${AFL_CFLAGS[@]}" "${PAK_FLAGS[@]}" -- "${PAK_SRCS[@]}")
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${AFL_PAK_OBJS[@]}" "$BLD/lsan_off_afl.o" -lm -o "$OUT/pak_file"

# console command interpreter: cmdparser.c + cvar.c (+ the zone/sizebuf/argv helpers they
# use), with the server flag (-DDEDICATED_ONLY: unknown commands are not forwarded)
CFG_SRCS=(mayhem/harnesses/cfg_file.c src/common/cmdparser.c src/common/cvar.c
          src/common/zone.c src/common/szone.c src/common/argproc.c src/common/shared/shared.c)
CFG_FLAGS=(-DDEDICATED_ONLY)
mapfile -t AFL_CFG_OBJS < <(compile "$AFL_CC" "$BLD/afl-cfg" "${AFL_CFLAGS[@]}" "${CFG_FLAGS[@]}" -- "${CFG_SRCS[@]}")
$AFL_CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${AFL_CFG_OBJS[@]}" "$BLD/lsan_off_afl.o" -lm -o "$OUT/cfg_file"

# 3) Functional-check drivers for mayhem/test.sh: same sources, normal flags, no sanitizers.
TEST_CFLAGS=("${YQ2_FLAGS[@]}" -O2 $COVERAGE_FLAGS)
mkdir -p "$OUT/test"
mapfile -t T_REF < <(compile "$CC" "$BLD/test-ref" "${TEST_CFLAGS[@]}" -- "${REF_SRCS[@]}" mayhem/harnesses/asset_dump.c)
$CC $COVERAGE_FLAGS "${T_REF[@]}" -lm -o "$OUT/test/asset_dump"
mapfile -t T_GAME < <(compile "$CC" "$BLD/test-game" "${TEST_CFLAGS[@]}" -- "${GAME_SRCS[@]}" mayhem/harnesses/savegame_file.c)
$CC $COVERAGE_FLAGS "${T_GAME[@]}" -lm -o "$OUT/test/savegame_file"
mapfile -t T_WAV < <(compile "$CC" "$BLD/test-wav" "${TEST_CFLAGS[@]}" -- src/client/sound/wave.c mayhem/harnesses/wav_file.c)
$CC $COVERAGE_FLAGS "${T_WAV[@]}" -lm -o "$OUT/test/wav_file"
mapfile -t T_BSP < <(compile "$CC" "$BLD/test-bsp" "${TEST_CFLAGS[@]}" -- "${BSP_SRCS[@]}")
$CC $COVERAGE_FLAGS "${T_BSP[@]}" -lm -o "$OUT/test/bsp_file"
mapfile -t T_PAK < <(compile "$CC" "$BLD/test-pak" "${TEST_CFLAGS[@]}" "${PAK_FLAGS[@]}" -- "${PAK_SRCS[@]}")
$CC $COVERAGE_FLAGS "${T_PAK[@]}" -lm -o "$OUT/test/pak_file"

mapfile -t T_CFG < <(compile "$CC" "$BLD/test-cfg" "${TEST_CFLAGS[@]}" "${CFG_FLAGS[@]}" -- "${CFG_SRCS[@]}")
$CC $COVERAGE_FLAGS "${T_CFG[@]}" -lm -o "$OUT/test/cfg_file"

ls -l "$OUT" "$OUT/test"
