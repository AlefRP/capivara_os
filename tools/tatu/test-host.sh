#!/bin/sh
# Testes do motor sem dar boot. Execute em Linux ou no container do projeto.
set -eu
mkdir -p build/tatu
${HOST_CC:-gcc} -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
    -fno-builtin -fno-omit-frame-pointer -fsanitize=address,undefined -no-pie \
    -Ikernel tools/tatu/host.c kernel/tatu/*.c \
    kernel/fs/vfs.c kernel/lib/string.c kernel/lib/utf8.c -o build/tatu/tatu-host
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/tatu/tatu-host
