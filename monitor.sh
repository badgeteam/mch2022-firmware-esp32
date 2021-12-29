#!/usr/bin/bash

set -e

cd esp-idf
source ./export.sh
cd ../

cd factory_test

if [ "$#" -eq 1 ]; then
    idf.py monitor -p $1
else
    if [ "$#" -ne 0 ]; then
        echo "Illegal number of parameters"
    else
        idf.py monitor
    fi
fi
