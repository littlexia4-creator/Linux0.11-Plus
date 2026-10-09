#!/bin/sh
# Build the kernel and guest syscall library, then boot Linux 0.11 in Bochs.
# Run from Git Bash / MSYS only: the Makefiles need a POSIX shell
# ('cd xxx ; make', rm, sync) -- the cmd.exe make in run.bat cannot
# run them and can leave a corrupt mixed build.

cd "$(dirname "$0")" || exit 1

# toolchain: NASM (user install) + the bundled MinGW32 next to the repo
PATH="/c/Users/Administrator/AppData/Local/bin/NASM:$PWD/../MinGW32/bin:$PATH"
export PATH

# a lock left by an abnormally closed Bochs would panic the next launch
rm -f hdc-0.11.img.lock

make || exit 1

# Stage the a.out archive built by MinGW32-i386-linux for the guest linker.
mkdir -p root-files/lib || exit 1
cp lib/libsyscall.a root-files/lib/libsyscall.a || exit 1
# Keep the guest demos' clone declaration and flags in sync with the archive.
cp include/sched.h root-files/sched.h || exit 1

# overlay: stage root-files/* into the guest disk image
./overlay.sh || exit 1

exec "/e/Program Files/Bochs-2.8/bochs.exe" -f bochsrc.bxrc -q
