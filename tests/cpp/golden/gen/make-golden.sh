#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes tests/cpp/golden/plain-texts.json again from the old JavaScript.
# Only needed when the corpus changes; the tests and CI read the file.
# Needs git history, Qt 5 with QtQml (QJSEngine) and qmake.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# the Twemoji the old rustrx.emojify called
git -C "$root" show 84311d4^:src/Twemoji.cpp > "$work/Twemoji.cpp"
git -C "$root" show 84311d4^:src/Twemoji.h > "$work/Twemoji.h"
qmake=${QMAKE:-$(command -v qmake-qt5 || command -v qmake)}
cd "$work"
"$qmake" "$here/gen.pro" OLD="$work" >/dev/null
make -s >/dev/null
./gen "$here/../plain-texts.json"
