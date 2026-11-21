#!/bin/sh
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude futex_qtest.c -Llib -lpthread -lcsyscalls
