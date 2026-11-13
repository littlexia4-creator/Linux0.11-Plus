#!/bin/sh
# Build inside Linux 0.11 after staging the headers and guest libraries.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -O -Iinclude sem_test.c -Llib -lpthread -lcsyscalls -o sem_test
