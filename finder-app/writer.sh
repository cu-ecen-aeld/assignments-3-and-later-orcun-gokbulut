#!/bin/bash

if [[ $# != 2 ]]; then
    echo "Error: Invalid argument count." >&2
    exit 1
fi

WRTR_ARG_FILE_PATH="$1"
WRTR_ARG_OUTPUT="$2"

mkdir -p "$(dirname "$WRTR_ARG_FILE_PATH")"
echo "$WRTR_ARG_OUTPUT" > "$WRTR_ARG_FILE_PATH"

exit 0