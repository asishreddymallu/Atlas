#!/usr/bin/env bash
# The assembler corpus check: machine code, tokens and parse errors for every
# program in asm/corpus.txt (repo examples), the directive programs and the
# broken programs, one file per program per dump, compared byte for byte with
# asm/expected.sha256.
#
#   dump_all.sh            dump into verification/out/asm/dumps and compare
#   dump_all.sh --record   dump, then rewrite asm/expected.sha256 (after an
#                          intended assembler change, reviewed by hand)
#
# The input's absolute path is replaced by its repo-relative name in every
# dump, so the result does not depend on where the checkout lives.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
VERIFY=$(dirname "$HERE"); ROOT=$(dirname "$VERIFY")
OUT=${ATLAS_VERIFY_OUT:-$VERIFY/out}/asm/dumps
BUILD=${ATLAS_BUILD_VERIFY:-$ROOT/build-verify}
PROBE=$BUILD/verification/probes/atlas_verify_asm
H=$VERIFY/lib/run_hermetic.sh
[ -x "$PROBE" ] || { echo "missing $PROBE: build with -DATLAS_BUILD_VERIFICATION=ON" >&2; exit 2; }
rm -rf "$OUT"; mkdir -p "$OUT"

inputs() {
  sed 's#^#'"$ROOT"'/#' "$HERE/corpus.txt"
  find "$HERE/inputs/directives" "$HERE/inputs/broken" -type f -name '*.s' | sort
}
n=0
while IFS= read -r f; do
  rel=${f#"$ROOT"/}
  k=$(echo "${rel#verification/asm/inputs/}" | tr '/' '_')
  for mode in code tokens errors; do
    "$H" "$PROBE" "$mode" "$f" 2>/dev/null \
      | grep -av "^---\|^ *\[\|^ *id=\|^ *fontSize=\|^$" \
      | sed "s#$f#$rel#g" > "$OUT/${mode}_$k.txt"
  done
  n=$((n+1))
done < <(inputs)
echo "programs: $n, dumps: $(ls "$OUT" | wc -l)"

cd "$OUT" || exit 2
if [ "${1:-}" = "--record" ]; then
  sha256sum -- * > "$HERE/expected.sha256"
  echo "recorded $(wc -l < "$HERE/expected.sha256") hashes in asm/expected.sha256"
  exit 0
fi
bad=$(sha256sum --quiet -c "$HERE/expected.sha256" 2>/dev/null | wc -l)
missing=$(comm -13 <(ls | sort) <(awk '{print $2}' "$HERE/expected.sha256" | sort) | wc -l)
extra=$(comm -23 <(ls | sort) <(awk '{print $2}' "$HERE/expected.sha256" | sort) | wc -l)
echo "asm: differing $bad, missing $missing, unexpected $extra"
[ "$bad" = 0 ] && [ "$missing" = 0 ] && [ "$extra" = 0 ]
