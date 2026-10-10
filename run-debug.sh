#!/bin/sh
# Build (MinGW32 toolchain + NASM) and boot Linux 0.11 under the
# Bochs debugger.  Run from Git Bash / MSYS only (see run.sh).

cd "$(dirname "$0")" || exit 1

PATH="/c/Users/Administrator/AppData/Local/bin/NASM:$PWD/../MinGW32/bin:$PATH"
export PATH

rm -f hdc-0.11.img.lock

make || exit 1
exec "/e/Program Files/bochs-win32-2.6.11-p4-smp/bochsdbg-p4-smp.exe" -f ./bochsrc.bxrc -q
