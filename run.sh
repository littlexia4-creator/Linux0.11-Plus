#!/bin/sh
# Prefer Docker when its daemon is reachable; otherwise use Windows.
set -eu

usage() {
    printf 'Usage: %s [--mode win|docker]\n' "$0"
    printf 'Default: docker when its daemon is reachable, otherwise win.\n'
}

mode=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --mode)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                usage >&2
                exit 2
            fi
            mode=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

case "$mode" in
    '')
        if docker info >/dev/null 2>&1; then
            mode=docker
        else
            mode=win
        fi
        ;;
    win|docker) ;;
    *)
        printf 'Unknown mode: %s\n' "$mode" >&2
        usage >&2
        exit 2
        ;;
esac

cd "$(dirname "$0")"
exec sh "./run-$mode.sh"
