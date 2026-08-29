#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
platform=$(printf '%s' "${1:-T31}" | tr '[:lower:]' '[:upper:]')

case "$platform" in
    T20|T20X)
        exec "$project_dir/build-t20.sh"
        ;;
    T21|T21N)
        exec "$project_dir/build-t21.sh"
        ;;
    T23)
        exec "$project_dir/build-t23.sh"
        ;;
    T30|T30X)
        exec "$project_dir/build-t30.sh"
        ;;
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
        echo "unsupported platform: $platform (use T20, T21, T23, T30, T31, T40, or T41)" >&2
        exit 2
        ;;
esac
