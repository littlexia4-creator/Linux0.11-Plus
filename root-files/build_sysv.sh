#!/bin/sh
# Build inside Linux 0.11 using the staged syscall wrappers and headers.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude sysv_sem.c -Llib -lcsyscalls
