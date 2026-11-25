#!/bin/sh
# Host-side logic test (no 3DS needed): sh test/run.sh [seed] [hours]
cd "$(dirname "$0")" && gcc -std=c11 -O2 -I../source host_test.c ../source/game.c ../source/world.c ../source/skin.c -lm -o /tmp/summit_host_test && /tmp/summit_host_test "$@"
