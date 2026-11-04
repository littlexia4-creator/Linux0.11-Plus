#!/bin/sh
# overlay.sh -- copy root-files/* into / of hdc-0.11.img (additive only).
# Called from run.sh after the build, before Bochs opens the image.
# Removing a file from root-files/ does NOT remove it from the image;
# use tools/minix.py rm for that.  Subdirectories are created as needed.

cd "$(dirname "$0")" || exit 1

[ -d root-files ] || exit 0

# find lists parent directories before their children, including nested headers.
(cd root-files && find . -type d) | while read -r d; do
	[ "$d" = "." ] && continue
	destination="/usr/root/${d#./}"
	MSYS_NO_PATHCONV=1 python tools/minix.py hdc-0.11.img stat "$destination" >/dev/null 2>&1 || \
		MSYS_NO_PATHCONV=1 python tools/minix.py hdc-0.11.img mkdir "$destination" || exit 1
done || exit 1

(cd root-files && find . -type f) | while read -r f; do
	p="${f#./}"
	# Minix V1 directory entries allow at most 14 characters per name.
	case "$p" in
		lib/libc_syscalls.a) destination="lib/libcsyscalls.a" ;;
		*) destination="$p" ;;
	esac
	MSYS_NO_PATHCONV=1 python tools/minix.py hdc-0.11.img put "root-files/$p" "/usr/root/$destination" || exit 1
done
