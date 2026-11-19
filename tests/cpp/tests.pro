# SPDX-License-Identifier: GPL-3.0-or-later
# The C++ unit tests, built against the host's Qt 5 (run.sh builds and
# runs them). The app's own sources are compiled in unchanged.
TEMPLATE = subdirs
SUBDIRS = twemoji renderhtml golden roomsort exif png mp4 readpoint voip
