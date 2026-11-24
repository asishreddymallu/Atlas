#!/usr/bin/env bash
# Smoke-checks a Linux package of Atlas.
#
# usage: scripts/check_package.sh <Atlas-*.AppImage | Atlas-*.tar.gz | extracted folder>
#
#   cli  the packaged atlas-cli runs examples/scheduler/load_use_original.s
#        on rv5-h-f (the packaged copy) and prints "Cycles -> 11"
#   gui  the packaged application starts with QT_QPA_PLATFORM=offscreen in a
#        throwaway settings directory and is still running after 5 s
#        (timeout exit 124 = pass); an AppImage is started as an AppImage
#        (with --appimage-extract-and-run when FUSE is missing)
# Prints PASS or FAIL for each, and exits 1 if any failed.
set -uo pipefail

[ $# -eq 1 ] || { echo "usage: $0 <AppImage | tar.gz | folder>" >&2; exit 2; }
INPUT=$(readlink -f "$1")
[ -e "$INPUT" ] || { echo "check_package: no such file: $1" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/atlas-check-XXXXXX")
trap 'rm -rf "$WORK"' EXIT
RESULTS=()
FAILED=0
record() { RESULTS+=("$(printf '  %-4s %s  %s' "$1" "$2" "$3")"); [ "$2" = PASS ] || FAILED=1; echo "  $2  $1  $3"; }

# --- unpack ----------------------------------------------------------------
APPIMAGE=""
case "$INPUT" in
    *.AppImage)
        APPIMAGE=$INPUT
        chmod +x "$APPIMAGE"
        (cd "$WORK" && "$APPIMAGE" --appimage-extract >/dev/null) || { echo "check_package: could not extract $INPUT" >&2; exit 1; }
        ROOT=$WORK/squashfs-root ;;
    *.tar.gz)
        tar -C "$WORK" -xzf "$INPUT" || { echo "check_package: could not unpack $INPUT" >&2; exit 1; }
        ROOT=$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -1) ;;
    *)
        [ -d "$INPUT" ] || { echo "check_package: not an AppImage, tar.gz or folder: $1" >&2; exit 2; }
        ROOT=$INPUT ;;
esac
CLI=$ROOT/usr/bin/atlas-cli
GUI=$ROOT/usr/bin/Atlas
EXAMPLE=$ROOT/usr/share/atlas/examples/scheduler/load_use_original.s
echo "== checking $(basename "$INPUT")"

# A throwaway home for settings and the working directory (vm_state/ goes there).
SANDBOX=$WORK/sandbox
mkdir -p "$SANDBOX/config" "$SANDBOX/data" "$SANDBOX/cache" "$SANDBOX/run"
sandboxed() {
    (cd "$SANDBOX/run" && env XDG_CONFIG_HOME="$SANDBOX/config" XDG_DATA_HOME="$SANDBOX/data" \
        XDG_CACHE_HOME="$SANDBOX/cache" QT_QPA_PLATFORM=offscreen "$@")
}

# --- cli ---------------------------------------------------------------------
if [ -x "$CLI" ] && [ -f "$EXAMPLE" ]; then
    OUT=$(sandboxed timeout 60 "$CLI" "$EXAMPLE" --vm rv5-h-f 2>&1)
    STATUS=$?
    if [ $STATUS -eq 0 ] && echo "$OUT" | grep -Eq 'Cycles +-> 11$'; then
        record cli PASS "atlas-cli load_use_original.s --vm rv5-h-f: Cycles -> 11"
    else
        record cli FAIL "exit $STATUS; output: $(echo "$OUT" | tail -3 | tr '\n' ' ')"
    fi
else
    record cli FAIL "missing $([ -x "$CLI" ] || echo usr/bin/atlas-cli) $([ -f "$EXAMPLE" ] || echo the example)"
fi

# --- gui ---------------------------------------------------------------------
if [ -n "$APPIMAGE" ]; then
    if [ -e /dev/fuse ] && command -v fusermount >/dev/null 2>&1; then
        START=("$APPIMAGE")
    else
        START=("$APPIMAGE" --appimage-extract-and-run)
    fi
else
    START=("$GUI")
fi
sandboxed timeout 5 "${START[@]}" > "$WORK/gui.log" 2>&1
STATUS=$?
if [ $STATUS -eq 124 ]; then
    record gui PASS "$(basename "${START[0]}") offscreen: still running after 5 s"
else
    record gui FAIL "exited with $STATUS before 5 s: $(tail -3 "$WORK/gui.log" | tr '\n' ' ')"
fi

echo
echo "Package check: $(basename "$INPUT")"
printf '%s\n' "${RESULTS[@]}"
if [ $FAILED -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit $FAILED
