#!/bin/sh
# Run inside Linux 0.11 after run.sh stages the headers and guest libraries.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude pthr_exit.c -Llib -lpthread -lcsyscalls
