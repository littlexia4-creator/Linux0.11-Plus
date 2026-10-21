#!/bin/sh
# Run inside Linux 0.11 after run.sh installs this as build_clone.sh.
# The archive is installed as lib/libcsyscalls.a (Minix's filename limit).
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude -Ilib clone_test.c -Llib -lcsyscalls -o clone_test
