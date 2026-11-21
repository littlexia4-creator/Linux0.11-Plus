#!/bin/sh
# Build and run the POSIX semaphore demo on modern Linux in Docker.
set -eu

cd "$(dirname "$0")"
source_dir=$PWD/root-files
case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*)
		source_dir=$(cygpath -m "$source_dir")
		export MSYS_NO_PATHCONV=1
		;;
esac

image=${SEM_DOCKER_IMAGE:-abseil-dev:latest}
exec docker run --rm --network none \
	--mount "type=bind,source=$source_dir,target=/demo,readonly" \
	"$image" sh -c '
		set -eu
		cd /tmp
		gcc -O -pthread /demo/sem_demo.c -o a.out
		./a.out
	'
