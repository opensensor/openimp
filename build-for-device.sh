#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
platform=$(printf '%s' "${1:-T31}" | tr '[:lower:]' '[:upper:]')

case "$platform" in
    T31)
        exec "$project_dir/build-t31.sh"
        ;;
    T40|T40XP)
        exec "$project_dir/build-t40.sh"
        ;;
    T41|T41NQ)
        exec "$project_dir/build-t41.sh"
        ;;
    *)
        echo "unsupported platform: $platform (use T31, T40, or T41)" >&2
        exit 2
        ;;
esac
