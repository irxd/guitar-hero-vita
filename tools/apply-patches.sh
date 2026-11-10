#!/usr/bin/env bash
# Applies patches/NNNN-*.patch to the upstream checkout (upstream/), in order.
# Idempotent: a patch that is already applied is skipped; one that neither
# applies nor is applied stops the build (upstream moved: refresh the patch).
#
# The patches are the source of truth for every change to upstream files.
# To change one: edit the file in upstream/, then regenerate the patch with
#   git -C upstream diff -- <files> > patches/NNNN-name.patch
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UP="$REPO/upstream"
shopt -s nullglob

for p in "$REPO"/patches/[0-9][0-9][0-9][0-9]-*.patch; do
    name="$(basename "$p")"
    if git -C "$UP" apply --check -R "$p" 2>/dev/null; then
        echo "patch $name: already applied"
    elif git -C "$UP" apply --check "$p" 2>/dev/null; then
        git -C "$UP" apply "$p"
        echo "patch $name: applied"
    else
        echo "patch $name: does not apply to upstream $(git -C "$UP" rev-parse --short HEAD); refresh it" >&2
        exit 1
    fi
done
