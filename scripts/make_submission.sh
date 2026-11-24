#!/usr/bin/env bash
# Builds the submission zip from the committed tree.
#
#   scripts/make_submission.sh --scope <scope.pdf> --report <report.pdf> [--out <file.zip>]
#
# Refuses unless:
#   - the git tree is clean (nothing modified, staged or untracked);
#   - docs/CHANGES_FROM_KITES.md is committed at HEAD;
#   - both PDFs exist.
# The code comes from `git archive HEAD`: tracked files only, so no .git and no
# build directories. Layout:
#
#   Atlas_CS25BTECH11032_Submission/
#     README_SUBMISSION.md
#     01_Scope_Document/<scope pdf>
#     02_Project_Report/<report pdf>
#     03_Changes_From_Baseline/CHANGES_FROM_KITES.md
#     04_Code/Atlas/...
set -euo pipefail

NAME=Atlas_CS25BTECH11032_Submission
CHANGES=docs/CHANGES_FROM_KITES.md
SCOPE="" REPORT="" OUT=""

die() { echo "make_submission: $*" >&2; exit 1; }
usage() { echo "usage: $0 --scope <scope.pdf> --report <report.pdf> [--out <file.zip>]" >&2; exit 2; }

while [ $# -gt 0 ]; do
  case "$1" in
    --scope)  [ $# -ge 2 ] || usage; SCOPE=$2; shift 2 ;;
    --report) [ $# -ge 2 ] || usage; REPORT=$2; shift 2 ;;
    --out)    [ $# -ge 2 ] || usage; OUT=$2; shift 2 ;;
    -h|--help) usage ;;
    *) echo "make_submission: unknown argument '$1'" >&2; usage ;;
  esac
done
[ -n "$SCOPE" ] && [ -n "$REPORT" ] || usage
[ -f "$SCOPE" ]  || die "scope document not found: $SCOPE"
[ -f "$REPORT" ] || die "project report not found: $REPORT"
case "$SCOPE" in *.pdf|*.PDF) ;; *) die "--scope must be a .pdf: $SCOPE" ;; esac
case "$REPORT" in *.pdf|*.PDF) ;; *) die "--report must be a .pdf: $REPORT" ;; esac
command -v zip >/dev/null || die "the 'zip' command is required (dnf install zip)"

# Absolute paths before changing directory.
SCOPE=$(cd "$(dirname "$SCOPE")" && pwd)/$(basename "$SCOPE")
REPORT=$(cd "$(dirname "$REPORT")" && pwd)/$(basename "$REPORT")
[ -n "$OUT" ] || OUT="$PWD/$NAME.zip"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "$ROOT is not a git checkout"

if [ -n "$(git status --porcelain)" ]; then
  echo "make_submission: the git tree is not clean. Commit or stash these first:" >&2
  git status --short >&2
  exit 1
fi
if ! git cat-file -e "HEAD:$CHANGES" 2>/dev/null; then
  die "$CHANGES is missing from HEAD. Write it, commit it, and run this again.
  (It is the changes-from-baseline document NOTICE.md links to; this script does not create it.)"
fi

COMMIT=$(git rev-parse HEAD)
SHORT=$(git rev-parse --short HEAD)
BRANCH=$(git rev-parse --abbrev-ref HEAD)
DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)

WORK=$(mktemp -d "${TMPDIR:-/tmp}/atlas-submission-XXXXXX")
trap 'rm -rf "$WORK"' EXIT
DEST="$WORK/$NAME"
mkdir -p "$DEST/01_Scope_Document" "$DEST/02_Project_Report" "$DEST/03_Changes_From_Baseline" "$DEST/04_Code/Atlas"

git archive --format=tar HEAD | tar -x -C "$DEST/04_Code/Atlas"
cp "$SCOPE" "$DEST/01_Scope_Document/"
cp "$REPORT" "$DEST/02_Project_Report/"
git show "HEAD:$CHANGES" > "$DEST/03_Changes_From_Baseline/CHANGES_FROM_KITES.md"

cat > "$DEST/README_SUBMISSION.md" <<EOF
# Atlas: submission (CS25BTECH11032)

| | |
| --- | --- |
| Commit | \`$COMMIT\` (branch \`$BRANCH\`) |
| Version | Atlas 1.0.0 (a build from this folder has no git, so Help > About shows plain 1.0.0; a build from the repository at this commit shows 1.0.0+$SHORT) |
| Packaged | $DATE |

## What is where

| Folder | Contents |
| --- | --- |
| \`01_Scope_Document/\` | $(basename "$SCOPE") |
| \`02_Project_Report/\` | $(basename "$REPORT") |
| \`03_Changes_From_Baseline/\` | CHANGES_FROM_KITES.md: the changes since the fork (also at \`04_Code/Atlas/$CHANGES\`) |
| \`04_Code/Atlas/\` | The repository at the commit above (\`git archive\`, no build output). Start with its README.md. |

## Build, run, test

Tested on Fedora 44 with GCC 16.2.1, Qt 6.11.2, CMake 4.3.0 and Ninja 1.13.2.
From \`04_Code/Atlas/\`:

\`\`\`bash
./scripts/build.sh                    # builds build/Atlas and build/atlas-cli
./build/Atlas                         # the application
./build/atlas-cli examples/scheduler/load_use_original.s --vm rv5-h-f   # 11 cycles
./scripts/test.sh                     # the test suite, serially: prints total / passed / failed
\`\`\`

\`scripts/verify_submission.sh <this zip>\` repeats the build and the tests in a
fresh directory and starts the application offscreen.
EOF

rm -f "$OUT"
(cd "$WORK" && zip -qr -X "$OUT" "$NAME")
echo "wrote $OUT"
echo "  commit $COMMIT ($BRANCH), $DATE"
echo "  $(unzip -Z1 "$OUT" | wc -l) entries, $(du -h "$OUT" | cut -f1)"
