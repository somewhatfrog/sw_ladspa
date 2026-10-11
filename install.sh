#!/bin/bash
set -e

CFLAGS="-O2 -fPIC -shared"

gcc $CFLAGS sw_noise_gate.c -lm -o sw_noise_gate.so
gcc $CFLAGS sw_compressor.c -lm -o sw_compressor.so

mv sw_noise_gate.so sw_compressor.so /usr/lib/ladspa

echo done
