#!/bin/sh
# Build the kernel and boot it with the prebuilt Bochs Docker image.
set -eu

cd "$(dirname "$0")"

for file in rootimage-0.11.img hdc-0.11.img; do
    if [ ! -f "$file" ]; then
        printf 'Missing guest image: %s\n' "$file" >&2
        exit 1
    fi
done

# Use the same Windows-hosted kernel toolchain as run-win.sh.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        PATH="/c/Users/Administrator/AppData/Local/bin/NASM:$PWD/../MinGW32/bin:$PATH"
        export PATH
        ;;
esac
make Boot.img

# Install root-files into the guest's /usr/root before Bochs opens the disk.
sh ./overlay.sh

mount_dir=$PWD
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        mount_dir=$(cygpath -m "$PWD")
        # Keep Git Bash from converting the container's /work path.
        export MSYS_NO_PATHCONV=1
        ;;
esac

# Guest disk changes persist in the repository's hdc-0.11.img.
# The image supplies the Bochs configuration and terminal launcher.
exec docker run --rm -it --pull=never --network=none \
    -e TERM=xterm \
    --mount "type=bind,source=$mount_dir,target=/work" \
    linux-bochs-patch:latest
