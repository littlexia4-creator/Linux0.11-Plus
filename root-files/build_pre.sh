#!/bin/sh
# Run inside Linux 0.11 after run.sh installs the rebuilt kernel and files.
case "$0" in
	*/*) cd "${0%/*}" || exit 1 ;;
esac
exec gcc -Iinclude preempt_demo.c -Llib -lcsyscalls
