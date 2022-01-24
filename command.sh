#!/usr/bin/env bash

set -e
set -u

export IDF_PATH="$PWD/esp-idf"
export IDF_EXPORT_QUIET=0
source "$IDF_PATH"/export.sh

if [ "$#" -eq 2 ]; then
    idf.py $2 -p $1
else
    if [ "$#" -ne 1 ]; then
        echo "Illegal number of parameters"
    else
        idf.py $1
    fi
fi
