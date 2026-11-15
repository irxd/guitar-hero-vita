#!/usr/bin/env bash
# Release build of GH Vita: a clean VPK build plus the full source it was
# built from, in dist/ghvita-v<version>/:
#   ghvita-v<version>.vpk             the game (no game data: players bring their own disc image)
#   ghvita-v<version>-source.tar.gz   our tree (git HEAD), upstream gd-game-01 + gd-engine-core at the
#                                     pinned commit with patches/ applied, the generated SPIR-V headers,
#                                     and vitaGL's source at the commit we link (LGPL-3.0, static)
#   RELEASE_NOTES.md                  docs/releases/v<version>.md
#   SHA256SUMS
#
# The license (PolyForm Noncommercial + upstream's additional terms) requires
# the full source with every distributed build, and vitaGL's LGPL requires
# that the game can be relinked against a changed vitaGL: the source archive
# covers both.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/release-vita.sh [--allow-dirty]
# --allow-dirty: build from uncommitted changes (a test of the script, not a
# release: the folder gets a -dirty suffix).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UP="$REPO/upstream"
VITAGL_SRC="${VITAGL_SRC:-/root/build/vitagl-src}"
OUT="${OUT:-/root/build/ghvita-release}"
DESKTOP="${DESKTOP:-/root/build/gd01-linux}"
DIRTY=0
[ "${1:-}" = "--allow-dirty" ] && DIRTY=1

die() { echo "release: $*" >&2; exit 1; }

VERSION="$(grep -oP '^project\(ghvita VERSION \K[0-9.]+' "$REPO/vita/CMakeLists.txt")" || die "no version in vita/CMakeLists.txt"
TITLE_ID="$(grep -oP '^set\(VITA_TITLEID\s+"\K[A-Z0-9]{9}' "$REPO/vita/CMakeLists.txt")" || die "no title id in vita/CMakeLists.txt"
PIN="$(grep -oP '^\| Commit \| `\K[0-9a-f]{40}' "$REPO/docs/port-plan.md")" || die "no upstream pin in docs/port-plan.md"
VITAGL_COMMIT="$(grep -oP 'VITAGL_COMMIT:-\K[0-9a-f]+' "$REPO/tools/build-vitagl.sh")" || die "no vitaGL commit in tools/build-vitagl.sh"
NOTES="$REPO/docs/releases/v$VERSION.md"
NAME="ghvita-v$VERSION"

echo "=== GH Vita $VERSION ($TITLE_ID), upstream $PIN, vitaGL $VITAGL_COMMIT"

# --- checks
[ "${SKIP_PATCHES:-0}" = 1 ] && die "SKIP_PATCHES=1 is never for a release"
[ -f "$NOTES" ] || die "missing release notes $NOTES"
grep -q "Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine" "$NOTES" || die "release notes lack the attribution line"
[ "$(git -C "$UP" rev-parse HEAD)" = "$PIN" ] || die "upstream/ is not at the pinned commit $PIN"
[ "$(git -C "$VITAGL_SRC" rev-parse --short=7 HEAD)" = "${VITAGL_COMMIT:0:7}" ] || die "$VITAGL_SRC is not at vitaGL $VITAGL_COMMIT (run tools/build-vitagl.sh)"
if [ -n "$(git -C "$REPO" status --porcelain)" ]; then
    [ "$DIRTY" = 1 ] || die "uncommitted changes in the repo (commit them, or --allow-dirty for a test)"
    NAME="$NAME-dirty"
fi
COMMIT="$(git -C "$REPO" rev-parse --short HEAD)"

# --- clean build (the patch stack is applied and checked by build-vita.sh)
rm -rf "$OUT"
OUT="$OUT" DESKTOP="$DESKTOP" bash "$REPO/tools/build-vita.sh"
[ -f "$OUT/ghvita.vpk" ] || die "no VPK built"

DIST="$REPO/dist/$NAME"
rm -rf "$DIST"
mkdir -p "$DIST"
cp "$OUT/ghvita.vpk" "$DIST/$NAME.vpk"
cp "$NOTES" "$DIST/RELEASE_NOTES.md"

# --- source archive
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
SRC="$STAGE/$NAME-source"
mkdir -p "$SRC"
if [ "$DIRTY" = 1 ]; then  # the working files, as built
    git -C "$REPO" ls-files -z --cached --others --exclude-standard | (cd "$REPO" && xargs -0 cp --parents -t "$SRC" 2>/dev/null || true)
else
    git -C "$REPO" archive HEAD | tar -x -C "$SRC"
fi
# Upstream as built: the pinned commit's engine, core and licenses, with our patches applied.
mkdir -p "$SRC/upstream"
git -C "$UP" archive "$PIN" -- gd-game-01 gd-engine-core LICENSE LICENSE-ADDITIONAL-TERMS.md README.md | tar -x -C "$SRC/upstream"
for p in "$REPO"/patches/[0-9][0-9][0-9][0-9]-*.patch; do
    (cd "$SRC/upstream" && git apply -p1 "$p") || die "patch $(basename "$p") does not apply to the archive"
done
cat > "$SRC/upstream/SOURCE-RELEASE.txt" <<EOF
prj-gd-engine (https://github.com/mbracali/prj-gd-engine) at $PIN:
gd-game-01, gd-engine-core and the licenses only, with every patch in ../patches/
already applied (the files this release was built from). tools/apply-patches.sh
sees this file and leaves the tree as it is.
EOF
# The SPIR-V headers the Vita build takes from the desktop build (tools/build-vita.sh).
mkdir -p "$SRC/vita/gen"
for h in core_renderer_spirv.h core_post_fx_hdr_spirv.h; do cp "$OUT/gen/$h" "$SRC/vita/gen/"; done
# vitaGL as linked (LGPL-3.0), built by tools/build-vitagl.sh with its flags.
mkdir -p "$SRC/third_party/vitaGL"
git -C "$VITAGL_SRC" archive HEAD -- Makefile source COPYING COPYING.LESSER README.md | tar -x -C "$SRC/third_party/vitaGL"  # no samples
cat > "$SRC/BUILD-FROM-SOURCE.txt" <<EOF
GH Vita $VERSION, source of the release build (repo commit $COMMIT).
Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine

Needs VitaSDK (tools/setup-vitasdk.sh, Ubuntu or WSL2) and CMake.
  1. vitaGL:  SRC=\$PWD/third_party/vitaGL bash tools/build-vitagl.sh
     (or any vitaGL: the build links /root/build/vitagl/libvitaGL.a when it exists, else the SDK's)
  2. game:    DESKTOP=\$PWD/vita bash tools/build-vita.sh
     (the SPIR-V headers are in vita/gen/, so no desktop build is needed)
The VPK is written to /root/build/gd01-vita/ghvita.vpk and build-out/.
Licenses: LICENSE and LICENSE-ADDITIONAL-TERMS.md (GDE, PolyForm Noncommercial 1.0.0 plus terms),
third_party/vitaGL/COPYING.LESSER (LGPL-3.0). CHANGES.md lists every change to upstream files.
EOF
tar -C "$STAGE" -czf "$DIST/$NAME-source.tar.gz" "$NAME-source"

(cd "$DIST" && sha256sum "$NAME.vpk" "$NAME-source.tar.gz" > SHA256SUMS)
echo "=== $DIST"
ls -la "$DIST"
cat "$DIST/SHA256SUMS"
