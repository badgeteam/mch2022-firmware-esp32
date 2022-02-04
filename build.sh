#!/usr/bin/env bash

set -e
set -u

export IDF_PATH="$PWD/esp-idf"
export IDF_EXPORT_QUIET=0
source "$IDF_PATH"/export.sh

idf.py build
