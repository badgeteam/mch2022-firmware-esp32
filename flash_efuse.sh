#!/usr/bin/bash

set -e

cd esp-idf
source ./export.sh
cd ../

cd efuse

if [ "$#" -eq 1 ]; then
    idf.py flash -p $1
else
    if [ "$#" -ne 0 ]; then
        echo "Illegal number of parameters"
    else
        idf.py flash
    fi
fi
