#!/usr/bin/env bash
# Refresh μField's translation files after changing strings in ufield/qml, ufield/storage or ufield/tools.
# Usage: scripts/update_ufield_i18n.sh [qt-bin-dir]   (default /usr/lib/qt6/bin)
# Then translate the new entries in ufield/i18n/ufield_fi.ts and run this again to build the .qm.
set -euo pipefail
QT_BIN="${1:-/usr/lib/qt6/bin}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
"$QT_BIN/lupdate" ufield/qml ufield/storage/*.cpp ufield/tools/*.cpp -ts ufield/i18n/ufield_fi.ts -no-obsolete -locations relative
"$QT_BIN/lrelease" ufield/i18n/ufield_fi.ts -qm ufield/i18n/ufield_fi.qm
if grep -q 'type="unfinished"' ufield/i18n/ufield_fi.ts; then
  echo "ufield_fi.ts has untranslated strings; Finnish is required (CLAUDE.md)." >&2
  exit 1
fi
