#!/usr/bin/env bash

set -e

# Fetch the SDK and all other submodules
git submodule update --init --recursive || exit 1

# Install the toolchain and other SDK tools
cd esp-idf
bash install.sh
