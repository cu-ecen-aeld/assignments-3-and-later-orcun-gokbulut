#!/bin/sh

if [[ $# != 2 ]]; then
    echo "Error: Invalid argument count." >&2
    exit 1
fi

FNDR_ARG_DIR="$1"
FNDR_ARG_TEXT="$2"

if [[ ! -d "$FNDR_ARG_DIR" ]]; then
    echo "Error: Search directory not found." >&2
    exit 1
fi

FNDR_VAR_NUM_FILES=$(find "$FNDR_ARG_DIR" -type f | wc -l)
FNDR_VAR_NUM_MATCHES=$(find "$FNDR_ARG_DIR" -type f -exec grep -a "$FNDR_ARG_TEXT" {} + | wc -l)

echo "The number of files are $FNDR_VAR_NUM_FILES and the number of matching lines are $FNDR_VAR_NUM_MATCHES"
exit 0