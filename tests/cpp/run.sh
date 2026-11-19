#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the C++ tests against the host's Qt 5 in tests/cpp/build and runs
# every one. Arguments go to each test binary (e.g. -silent). Exit status:
# non-zero when a build or any test failed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
build="$here/build"
qmake=${QMAKE:-$(command -v qmake-qt5 || command -v qmake)}
mkdir -p "$build"
cd "$build"
"$qmake" "$here/tests.pro" >/dev/null
make -s -j"$(nproc 2>/dev/null || echo 2)" >/dev/null

status=0
run() {
    env QT_QPA_PLATFORM=offscreen "$@" || status=1
}
for t in twemoji renderhtml golden exif mp4 readpoint voip; do
    run "$build/$t/tst_$t" "$@"
done
# the room order by name depends on the locale: once without collation
# rules, once in German where the host has it
run LC_ALL=C.UTF-8 LANG=C.UTF-8 "$build/roomsort/tst_roomsort" "$@"
if locale -a 2>/dev/null | grep -qi '^de_DE\.utf-\?8$'; then
    run LC_ALL=de_DE.UTF-8 LANG=de_DE.UTF-8 "$build/roomsort/tst_roomsort" "$@"
fi
exit $status
