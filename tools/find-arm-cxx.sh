#!/bin/sh
# Print an arm-none-eabi-g++ that can include <cstdint>.
# Homebrew's GCC 16 bottle is built --without-headers, so the compiler on
# PATH may accept -std=c++23 and still be unable to build this firmware.

choose() {
    c=$1
    if ! { command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; }; then
        return 1
    fi
    printf '%s\n' '#include <cstdint>' 'int main(){return 0;}' |
        "$c" -std=c++17 -fsyntax-only -xc++ - >/dev/null 2>&1
}

if [ -n "${ARM_CXX_REQUEST:-}" ]; then
    printf '%s\n' "$ARM_CXX_REQUEST"
    exit 0
fi

if choose arm-none-eabi-g++; then
    printf '%s\n' arm-none-eabi-g++
    exit 0
fi

for c in "$HOME"/workspace/netkit/boards/nucleo-f446re/.toolchain/xpack-arm-none-eabi-gcc-*/bin/arm-none-eabi-g++; do
    if choose "$c"; then
        printf '%s\n' "$c"
        exit 0
    fi
done

printf '%s\n' arm-none-eabi-g++
exit 0
