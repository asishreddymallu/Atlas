#!/usr/bin/env bash
# Checks a submission zip made by scripts/make_submission.sh, the way a marker
# would use it: in a fresh directory, with nothing from this checkout.
#
#   scripts/verify_submission.sh [--quick] [--keep] <submission.zip>
#
# Steps, each reported PASS or FAIL:
#   layout   the zip has README_SUBMISSION.md, 01_ and 02_ with a PDF each,
#            03_Changes_From_Baseline/CHANGES_FROM_KITES.md, 04_Code/Atlas/
#   build    04_Code/Atlas/scripts/build.sh
#   tests    04_Code/Atlas/scripts/test.sh (the whole suite, serially);
#            with --quick, only the CliSmoke, Scheduler*, CacheLab* and
#            Version tests (serially)
#   app      build/Atlas started on the offscreen platform stays up for 5 s
#            (in a throwaway settings directory)
# --keep leaves the temporary directory in place and prints its path.
set -uo pipefail

QUICK=0 KEEP=0 ZIP=""
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    --keep) KEEP=1 ;;
    -h|--help) echo "usage: $0 [--quick] [--keep] <submission.zip>"; exit 2 ;;
    -*) echo "verify_submission: unknown option '$a'" >&2; exit 2 ;;
    *) ZIP=$a ;;
  esac
done
[ -n "$ZIP" ] && [ -f "$ZIP" ] || { echo "usage: $0 [--quick] [--keep] <submission.zip>" >&2; exit 2; }
command -v unzip >/dev/null || { echo "verify_submission: 'unzip' is required" >&2; exit 2; }
ZIP=$(cd "$(dirname "$ZIP")" && pwd)/$(basename "$ZIP")

WORK=$(mktemp -d "${TMPDIR:-/tmp}/atlas-verify-submission-XXXXXX")
if [ $KEEP = 1 ]; then echo "keeping $WORK"; else trap 'rm -rf "$WORK"' EXIT; fi
LOG=$WORK/logs; mkdir -p "$LOG"

declare -a NAMES RESULTS NOTES
record() { NAMES+=("$1"); RESULTS+=("$2"); NOTES+=("$3"); echo "  $2  $1  $3"; }

echo "== unpack $(basename "$ZIP") into $WORK"
unzip -q "$ZIP" -d "$WORK/unpacked" || { echo "verify_submission: unzip failed" >&2; exit 1; }
TOP=$(find "$WORK/unpacked" -mindepth 1 -maxdepth 1 -type d | head -1)

# --- layout -------------------------------------------------------------------
missing=()
[ -f "$TOP/README_SUBMISSION.md" ] || missing+=("README_SUBMISSION.md")
ls "$TOP"/01_Scope_Document/*.pdf >/dev/null 2>&1 || missing+=("01_Scope_Document/*.pdf")
ls "$TOP"/02_Project_Report/*.pdf >/dev/null 2>&1 || missing+=("02_Project_Report/*.pdf")
[ -f "$TOP/03_Changes_From_Baseline/CHANGES_FROM_KITES.md" ] || missing+=("03_Changes_From_Baseline/CHANGES_FROM_KITES.md")
[ -f "$TOP/04_Code/Atlas/CMakeLists.txt" ] || missing+=("04_Code/Atlas/CMakeLists.txt")
[ -d "$TOP/04_Code/Atlas/.git" ] && missing+=("(04_Code/Atlas/.git must not be there)")
if [ ${#missing[@]} -eq 0 ]; then
  record layout PASS "$(basename "$TOP")/"
else
  record layout FAIL "missing: ${missing[*]}"
fi

CODE=$TOP/04_Code/Atlas
if [ ! -f "$CODE/CMakeLists.txt" ]; then
  echo "no code to build"; exit 1
fi
cd "$CODE" || exit 1

# --- build --------------------------------------------------------------------
echo "== build (scripts/build.sh; log: $LOG/build.log)"
start=$SECONDS
if ./scripts/build.sh > "$LOG/build.log" 2>&1 && [ -x build/Atlas ] && [ -x build/atlas-cli ]; then
  record build PASS "build/Atlas, build/atlas-cli ($((SECONDS - start)) s)"
else
  record build FAIL "see $LOG/build.log"; tail -20 "$LOG/build.log"
fi

# --- tests --------------------------------------------------------------------
start=$SECONDS
if [ $QUICK = 1 ]; then
  echo "== tests, quick: CliSmoke, Scheduler*, CacheLab*, Version (log: $LOG/tests.log)"
  if cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON > "$LOG/tests.log" 2>&1 &&
     cmake --build build-tests >> "$LOG/tests.log" 2>&1; then
    ctest --test-dir build-tests --output-on-failure -R '^([A-Za-z]+/)?(CliSmoke|Scheduler[A-Za-z]*|CacheLab[A-Za-z]*|Version)\.' \
      >> "$LOG/tests.log" 2>&1
    summary=$(grep -E 'tests passed|tests failed' "$LOG/tests.log" | tail -1)
    if grep -q '100% tests passed' "$LOG/tests.log" && ! grep -q 'out of 0$' "$LOG/tests.log"; then
      record tests PASS "$summary ($((SECONDS - start)) s)"
    else
      record tests FAIL "${summary:-no result}; see $LOG/tests.log"
    fi
  else
    record tests FAIL "test build failed; see $LOG/tests.log"; tail -20 "$LOG/tests.log"
  fi
else
  echo "== tests, full suite, serially (scripts/test.sh; log: $LOG/tests.log)"
  if ./scripts/test.sh > "$LOG/tests.log" 2>&1; then
    record tests PASS "$(tail -1 "$LOG/tests.log") ($((SECONDS - start)) s)"
  else
    record tests FAIL "$(tail -1 "$LOG/tests.log"); see $LOG/tests.log"
  fi
fi

# --- app ----------------------------------------------------------------------
echo "== app: build/Atlas offscreen for 5 s"
if [ -x build/Atlas ]; then
  SANDBOX=$WORK/sandbox; mkdir -p "$SANDBOX/config" "$SANDBOX/data" "$SANDBOX/cache" "$SANDBOX/run"
  (cd "$SANDBOX/run" && XDG_CONFIG_HOME="$SANDBOX/config" XDG_DATA_HOME="$SANDBOX/data" \
     XDG_CACHE_HOME="$SANDBOX/cache" QT_QPA_PLATFORM=offscreen timeout 5 "$CODE/build/Atlas" \
     > "$LOG/app.log" 2>&1)
  status=$?
  if [ $status -eq 124 ]; then
    record app PASS "still running after 5 s (stopped by the timeout)"
  else
    record app FAIL "exited with status $status before 5 s; see $LOG/app.log"
  fi
else
  record app FAIL "build/Atlas was not built"
fi

# --- summary ------------------------------------------------------------------
echo
echo "Submission check: $(basename "$ZIP")$([ $QUICK = 1 ] && echo ' (quick)')"
fail=0
for i in "${!NAMES[@]}"; do
  printf '  %-6s %-4s %s\n' "${NAMES[$i]}" "${RESULTS[$i]}" "${NOTES[$i]}"
  [ "${RESULTS[$i]}" = PASS ] || fail=1
done
if [ $fail = 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit $fail
