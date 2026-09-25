#!/bin/sh
# Builds libpalhold.so next to this script. Plain C, libc only; preload it alongside libUE4SS.so.
cd "$(dirname "$0")" && gcc -O2 -fPIC -shared -fvisibility=hidden -Wall -Wextra -o libpalhold.so palhold.c -lpthread
