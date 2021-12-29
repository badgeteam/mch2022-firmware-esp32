#!/usr/bin/bash

set -e

cd esp-idf
source ./export.sh
cd ../

cd efuse
idf.py build
