#!/usr/bin/env bash
# Applies patches/NNNN-*.patch to the upstream checkout (upstream/), in order.
# The patches are a stack (later ones may touch the same files as earlier
# ones), so the state is checked for the whole stack:
#   - the whole stack already applied      -> nothing to do
#   - the files the stack touches are clean -> apply the stack in order
#   - anything else (edits not in patches/) -> stop; regenerate or reset
#
# The patches are the source of truth for every change to upstream files.
# To change one: edit the file in upstream/ (with the stack applied), then
# regenerate that patch, e.g. for a file only it touches:
#   git -C upstream diff -- <file> > patches/NNNN-name.patch
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UP="$REPO/upstream"
shopt -s nullglob
PATCHES=("$REPO"/patches/[0-9][0-9][0-9][0-9]-*.patch)
[ ${#PATCHES[@]} -gt 0 ] || exit 0

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mapfile -t FILES < <(grep -hE '^\+\+\+ b/' "${PATCHES[@]}" | sed 's|^+++ b/||' | sort -u)

# The expected result: the stack applied to upstream HEAD in a throwaway
# index (the working tree is not touched), compared with the working files.
stack_is_applied() {
    export GIT_INDEX_FILE="$TMP/index"
    git -C "$UP" read-tree HEAD
    for p in "${PATCHES[@]}"; do git -C "$UP" apply --cached "$p" 2>/dev/null || { unset GIT_INDEX_FILE; return 1; }; done
    git -C "$UP" diff --quiet -- "${FILES[@]}"
    local same=$?
    unset GIT_INDEX_FILE
    return $same
}

if stack_is_applied; then
    echo "patches: all ${#PATCHES[@]} already applied"
elif git -C "$UP" diff --quiet -- "${FILES[@]}"; then
    for p in "${PATCHES[@]}"; do
        git -C "$UP" apply "$p" || { echo "patch $(basename "$p"): does not apply to upstream $(git -C "$UP" rev-parse --short HEAD); refresh it" >&2; exit 1; }
        echo "patch $(basename "$p"): applied"
    done
else
    echo "patches: upstream files have changes that are not the patch stack:" >&2
    git -C "$UP" diff --stat -- "${FILES[@]}" >&2
    echo "regenerate the patch(es) in patches/, or reset with: git -C upstream checkout -- <files>" >&2
    exit 1
fi
