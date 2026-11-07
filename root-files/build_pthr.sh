#!/bin/sh
# Run inside Linux 0.11 after run.sh stages the headers and guest libraries.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude pthr_join.c -Llib -lpthread -lcsyscalls -o pthr_join
