#!/usr/bin/env bash

set -e
set -u

export IDF_PATH="$PWD/esp-idf"
export IDF_EXPORT_QUIET=0
source "$IDF_PATH"/export.sh

if [ "$#" -eq 1 ]; then
    idf.py monitor -p $1
else
    if [ "$#" -ne 0 ]; then
        echo "Illegal number of parameters"
    else
        idf.py monitor
    fi
fi
