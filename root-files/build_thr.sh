#!/bin/sh
# Run inside Linux 0.11 after run.sh stages the host-built lib/libsyscall.a.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -Iinclude thr.c -Llib -lsyscall
