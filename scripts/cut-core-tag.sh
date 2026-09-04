#!/usr/bin/env bash
# Cut a lean, source-only MeshCore tag on the meshcomod monorepo for wadamesh to
# pin as its core lib_dep.
#
# Why: the monorepo (ALLFATHER-BV/meshcomod) is the single source of truth for the
# MeshCore core AND the non-touch firmware, but it also carries ~1.2 GB of
# prebuilt/ release bins. PlatformIO SHALLOW-clones a git lib_dep, so pinning main
# (or a normal tag) would pull that 1.2 GB into wadamesh's lib clone every CI run.
# This script creates a tag whose TREE drops prebuilt/ + bootstrap-backup/, so the
# shallow clone is ~12 MB. main is untouched (keeps prebuilt/ for the flasher/OTA);
# the lean commit is reachable only via the tag.
#
# Usage:
#   scripts/cut-core-tag.sh core-v1.16.2 [/path/to/meshcomod]
# then bump lib_deps in platformio.ini to '...meshcomod.git#core-v1.16.2' + rebuild.
set -euo pipefail

TAG="${1:?usage: cut-core-tag.sh <tag>  e.g. core-v1.16.2  [monorepo-path]}"
MONO="${2:-/Users/kaj/Meshcomod_Touch}"
DROP="prebuilt bootstrap-backup"     # heavy, non-core dirs wadamesh never builds

cd "$MONO"
git fetch origin --quiet
WT="$(mktemp -d)/core-tag"
git worktree add --no-checkout -d "$WT" origin/main >/dev/null
cleanup() { git worktree remove "$WT" --force 2>/dev/null || true; git worktree prune; }
trap cleanup EXIT

cd "$WT"
git reset -q --mixed origin/main         # populate index from main (no 1.2 GB checkout)
git rm -r --cached --quiet $DROP
git commit -q -m "build(core): source-only snapshot for the wadamesh core pin ($TAG)

Drops $DROP from the tree wadamesh shallow-clones as its MeshCore lib_dep.
main keeps them for the flasher/OTA; this commit is reachable only via the tag."

# sanity: prebuilt gone, core intact
[ "$(git ls-tree -r --name-only HEAD | grep -c '^prebuilt/')" = 0 ]   || { echo "!! prebuilt still present"; exit 1; }
[ "$(git ls-tree -r --name-only HEAD | grep -c '^src/')" -gt 100 ]    || { echo "!! src/ missing — aborting"; exit 1; }

git tag -a "$TAG" -m "MeshCore core lib — lean source-only snapshot for wadamesh"
git push origin "$TAG"
echo "pushed lean tag $TAG -> $(git rev-parse --short HEAD)"
echo "next: set lib_deps in wadamesh/platformio.ini to '...meshcomod.git#$TAG' and rebuild."
