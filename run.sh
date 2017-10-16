#!/usr/bin/env bash
set -e
make
./bench64 "$@"
./bench32 "$@"
