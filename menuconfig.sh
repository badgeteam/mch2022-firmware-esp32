#!/usr/bin/bash

set -e

cd esp-idf
source ./export.sh
cd ../

cd factory_test
idf.py menuconfig
