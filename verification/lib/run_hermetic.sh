#!/usr/bin/env bash
# Runs a command with a throwaway XDG home, seeded to a known state.
#
# Why: the probes run the product's code, and ThemeManager::setTheme()
# and FontManager::setSize() write through to QSettings - the developer's
# ~/.config/Atlas/Atlas.conf. A four-theme smoke run leaves whichever theme
# it ended on; a font sweep leaves whichever size it ended on. Both have
# happened, and the second one made the test suite fail.
#
# Usage: run_hermetic.sh [--keep] <command> [args...]
#   --keep  leave the temp dir in place and print its path (for debugging)
set -u

KEEP=0
if [ "${1:-}" = "--keep" ]; then KEEP=1; shift; fi
if [ $# -eq 0 ]; then echo "usage: run_hermetic.sh [--keep] <command> [args...]" >&2; exit 2; fi

SANDBOX=$(mktemp -d "${TMPDIR:-/tmp}/atlas-hermetic-XXXXXX") || exit 2
mkdir -p "$SANDBOX/config/Atlas" "$SANDBOX/data" "$SANDBOX/cache"

# The seed. Every hermetic run starts from exactly this.
cat > "$SANDBOX/config/Atlas/Atlas.conf" <<'CONF'
[editor]
fontSize=11

[theme]
id=tokyo-night
CONF
SEED_MD5=$(md5sum "$SANDBOX/config/Atlas/Atlas.conf" | cut -d' ' -f1)

export XDG_CONFIG_HOME="$SANDBOX/config"
export XDG_DATA_HOME="$SANDBOX/data"
export XDG_CACHE_HOME="$SANDBOX/cache"

# ── vm_state isolation ────────────────────────────────────────────────
# XDG alone is not enough. globals.cpp:11-25 sets
#   invokation_path = getWritableDataDirectory()
# which on Linux is std::filesystem::current_path(), captured at static
# init. So vm_state/ - temp.asm, the dumps, config.ini and the custom
# pseudo-instruction file that IS read back - lives under whatever
# directory the harness was launched from. Running from the Atlas root
# therefore wrote into the repo's own vm_state/ on every harness run.
#
# Fix: give the run its own directory and cd into it. examples/ is
# copied in because several harnesses open examples/*.s by relative
# path. ATLAS_ROOT lets the caller point at a checkout other than the
# current one.
ROOT="${ATLAS_ROOT:-$PWD}"
mkdir -p "$SANDBOX/run/vm_state"
if [ -d "$ROOT/examples" ]; then
  cp -r "$ROOT/examples" "$SANDBOX/run/examples"
fi
cd "$SANDBOX/run" || exit 2

"$@"
STATUS=$?

echo "--- hermetic: seed md5 $SEED_MD5"
echo "--- hermetic: config after the run:"
sed 's/^/      /' "$SANDBOX/config/Atlas/Atlas.conf" 2>/dev/null || echo "      (no config written)"
echo "--- hermetic: config md5 after $(md5sum "$SANDBOX/config/Atlas/Atlas.conf" 2>/dev/null | cut -d' ' -f1)"

echo "--- hermetic: ran in $SANDBOX/run (vm_state isolated)"

if [ "$KEEP" = "1" ]; then
  echo "--- hermetic: kept $SANDBOX"
else
  rm -rf "$SANDBOX"
fi
exit $STATUS
