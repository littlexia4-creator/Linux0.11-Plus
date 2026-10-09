#!/bin/sh
# Run inside Linux 0.11 after run.sh stages the host-built lib/libc_syscalls.a.
# overlay.sh installs it as lib/libcsyscalls.a (Minix's 14-character limit).
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -Iinclude thr.c -Llib -lcsyscalls
