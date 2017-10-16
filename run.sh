#!/usr/bin/env bash
set -ex
make
./bench64 "$@"
./bench32 "$@"
