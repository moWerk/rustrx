#!/bin/bash
# Tag and publish a release: push main, tag vVERSION, create a GitHub
# pre-release with the RPM and the notes. The repo must exist (created by
# the author). usage: tools/release.sh VERSION NOTES.md
set -euo pipefail
cd "$(dirname "$0")/.."
v=${1:?version}; notes=${2:?notes file}
rpm=/mnt/storage/sailfish-sdk/out/rustrx/RPMS/harbour-rustrx-$v-1.aarch64.rpm
[ -f "$rpm" ] || { echo "no RPM $rpm"; exit 1; }
git remote get-url origin >/dev/null 2>&1 || git remote add origin git@github.com:moWerk/rustrx.git
git push -u origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "HEAD is not origin/main"; exit 1; }
git rev-parse -q --verify "refs/tags/v$v" >/dev/null && { echo "tag v$v exists"; exit 1; }
git tag -a "v$v" -m "Rustrx $v"
git push origin "v$v"
gh release create "v$v" "$rpm" --prerelease --title "Rustrx $v" --notes-file "$notes"
