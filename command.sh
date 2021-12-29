#!/usr/bin/bash

set -e

cd esp-idf
source ./export.sh
cd ../

cd factory_test

if [ "$#" -eq 2 ]; then
    idf.py $2 -p $1
else
    if [ "$#" -ne 1 ]; then
        echo "Illegal number of parameters"
    else
        idf.py $1
    fi
fi
