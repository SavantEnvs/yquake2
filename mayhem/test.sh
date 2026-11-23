#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
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

# yquake2 ships no test suite. These known-answer checks run the real loaders/parsers
# (built by build.sh with normal flags into mayhem/out/test/) on committed fixtures in
# mayhem/testdata/ and compare their decoded output to known values; the savegame checks
# write a game + level save through the game module, read them back and compare the
# restored state, and require a truncated save to be rejected.
T="$SRC/mayhem/out/test"
D="$SRC/mayhem/testdata"
for b in asset_dump wav_file savegame_file bsp_file pak_file cfg_file; do
  [ -x "$T/$b" ] || { echo "missing $T/$b — run mayhem/build.sh first" >&2; emit_ctrf "yquake2-known-answer" 0 1; exit 1; }
done

passed=0 failed=0
check() { # check <name> <expected output> <command...>
  local name=$1 want=$2; shift 2
  local got; got=$("$@" 2>/dev/null)
  if [ "$got" == "$want" ]; then
    echo "PASS $name"; passed=$((passed+1))
  else
    echo "FAIL $name"; echo "  want: $want"; echo "  got:  $got"; failed=$((failed+1))
  fi
}

check pcx-decode "pcx 4x2 bpp=8 hash=cfa18afe pal=58f90ec5" "$T/asset_dump" pcx "$D/test.pcx"
check wal-decode "wal 4x4 bits=8 hash=c8fff215" "$T/asset_dump" wal "$D/test.wal"
check md2-load "skin models/test/skin.pcx
model type=3
md2 frames=1 tris=1 xyz=3 skins=1 glcmds=11" "$T/asset_dump" md2 "$D/test.md2"
check sp2-load "skin sprites/s0.pcx
skin sprites/s1.pcx
model type=2
sp2 frames=2 first=16x32 sprites/s0.pcx" "$T/asset_dump" sp2 "$D/test.sp2"
check wav-info "rate=11025 width=1 channels=1 loopstart=-1 samples=8 dataofs=44" "$T/wav_file" "$D/test.wav"
check bsp-load "map: checksum=f8ea1ac7 clusters=1 inline_models=2 entchars=100
trace: fraction=0.666 endz=0.0 contents=1 below=1" "$T/bsp_file" "$D/test.bsp"

work=$(mktemp -d "${TMPDIR:-/tmp}/yq2-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
# run the pack reader from the scratch dir (it writes nothing, but keep the tree clean)
cd "$work"
check pak-read "maps/test.ent 29 bc73856b
pics/colormap.txt 17 d4124706" "$T/pak_file" "$D/test.pak"
check pk3-read "maps/test.ent 29 bc73856b
pics/stored.txt 52 7b86edc1" "$T/pak_file" "$D/test.pk3"
cd "$SRC"
check save-write "write: num_edicts=19 total_monsters=1" "$T/savegame_file" write "$work"
check save-read-game "game: maxclients=4 maxentities=1024 helpmessage1=\"\"" "$T/savegame_file" game "$work/game.ssv"
check save-read-level "level: mapname=\"fuzz\" num_edicts=19 total_monsters=1" "$T/savegame_file" level "$work/level.sav"
check cfg-exec "Byte ordering: little endian

execing fuzz.cfg.
hello Ranger 
second 
from vstr 
a ; b 
c 
before 
after " "$T/cfg_file" "$D/test.cfg"
head -c 200 "$work/level.sav" > "$work/short.sav" 2>/dev/null
"$T/savegame_file" level "$work/short.sav" >/dev/null 2>&1; rc=$?
if [ "$rc" -eq 1 ]; then echo "PASS save-reject-truncated"; passed=$((passed+1)); else echo "FAIL save-reject-truncated (rc=$rc)"; failed=$((failed+1)); fi

emit_ctrf "yquake2-known-answer" "$passed" "$failed"
