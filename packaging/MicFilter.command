#!/bin/sh
# Double-click this file on macOS: it opens Terminal and starts MicFilter from this folder.
cd "$(dirname "$0")" || exit 1
MICFILTER_IN_TERMINAL=1 exec ./micfilter "$@"
