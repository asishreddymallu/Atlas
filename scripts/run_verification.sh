#!/usr/bin/env bash
# Runs every verification checker in verification/ (see verification/README.md):
#   build the probes (build-verify, -DATLAS_BUILD_VERIFICATION=ON), then
#   FP oracle self-test, ISA sweep, hazard/cycle sweep, li audit, L1 cache
#   sweep, assembler corpus, committed tables, negative controls.
# Each checker's summary line goes to verification/out/summary.txt, which is
# compared with verification/expected_summary.txt. Exit 0 only when every
# checker ran, the summaries match and every negative control was caught.
#
# usage: scripts/run_verification.sh [--no-build] [--record]
#   --no-build  use the probes already in build-verify
#   --record    after an intended change, copy the new summary to
#               verification/expected_summary.txt (review the diff first)
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V=$ROOT/verification
BUILD=${ATLAS_BUILD_VERIFY:-$ROOT/build-verify}
OUT=${ATLAS_VERIFY_OUT:-$V/out}
export ATLAS_BUILD_VERIFY=$BUILD ATLAS_VERIFY_OUT=$OUT
BUILD_STEP=1; RECORD=0
for a in "$@"; do
  case $a in
    --no-build) BUILD_STEP=0 ;;
    --record) RECORD=1 ;;
    *) echo "usage: $0 [--no-build] [--record]" >&2; exit 2 ;;
  esac
done

if [ $BUILD_STEP = 1 ]; then
  echo "== build probes ($BUILD)"
  cmake -S "$ROOT" -B "$BUILD" -G Ninja -DATLAS_BUILD_VERIFICATION=ON > /dev/null || exit 2
  cmake --build "$BUILD" --target atlas_verify_sweep atlas_verify_cacheprobe atlas_verify_asm || exit 2
fi
mkdir -p "$OUT"
SUMMARY=$OUT/summary.txt; : > "$SUMMARY"
FAILED=0

# step <name> <summary-regex> <command...>: runs the command, keeps its full
# log in out/<name>.log and its summary line(s) in the summary.
step() {
  local name=$1 pattern=$2; shift 2
  local start=$SECONDS
  echo "== $name"
  "$@" > "$OUT/$name.log" 2>&1
  local status=$?
  local lines
  lines=$(grep -E "$pattern" "$OUT/$name.log")
  echo "$lines" | sed 's/^/   /'
  echo "   (exit $status, $((SECONDS - start)) s)"
  if [ -z "$lines" ]; then
    echo "   no summary line: see $OUT/$name.log"; FAILED=1
    echo "$name: NO SUMMARY (exit $status)" >> "$SUMMARY"
  else
    echo "$lines" | sed "s/^/$name: /" >> "$SUMMARY"
  fi
  return $status
}

cd "$V" || exit 2
step fpo-selftest '^bad ' sh -c 'cd lib && python3 selftest.py'
step isa-lists 'lines$' python3 isa/sweep.py lists
step isa-gen 'mnemonics, ' python3 isa/sweep.py gen
step isa-run 'results$' python3 isa/sweep.py run
step isa-compare 'model divergences' python3 isa/sweep.py compare
step hazard-gen ' programs; ' python3 hazard/hazgen.py gen
step hazard-run 'results$' python3 hazard/hazgen.py run
step hazard-compare '^(programs|## |nh-nf fails)' python3 hazard/hazgen.py compare
step li-values 'values$' python3 tables/li_audit.py values
step li-check 'values, .* wrong|wrong in' python3 tables/li_audit.py encode now
step li-run 'values, .* wrong' python3 tables/li_audit.py run now
step cache '^rows .* mismatches' python3 cache/run_all.py
step asm '^(programs:|asm:)' asm/dump_all.sh
step tables '^tables ' python3 tables/check_tables.py
step negative-controls '^(CAUGHT|MISSED|controls )' python3 negative_controls.py || FAILED=1

echo
if [ $RECORD = 1 ]; then
  cp "$SUMMARY" "$V/expected_summary.txt"
  echo "recorded verification/expected_summary.txt"
elif diff -u "$V/expected_summary.txt" "$SUMMARY" > "$OUT/summary.diff"; then
  echo "summary matches verification/expected_summary.txt"
else
  echo "SUMMARY DIFFERS from verification/expected_summary.txt:"
  cat "$OUT/summary.diff"
  FAILED=1
fi
[ $FAILED = 0 ] && echo "VERIFICATION PASSED" || echo "VERIFICATION FAILED"
exit $FAILED
