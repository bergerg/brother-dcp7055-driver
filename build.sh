#!/bin/sh
set -e
cd "$(dirname "$0")"
clang -O2 -Wall -Wextra -Wno-deprecated-declarations -o rastertodcp7055 rastertodcp7055.c -lcupsimage -lcups
