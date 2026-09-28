#!/usr/bin/env sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FETCH=ask
CHECK_LLVM=0

usage()
{
    echo "usage: $0 [--fetch|--no-fetch] [--llvm]"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --fetch) FETCH=yes ;;
        --no-fetch) FETCH=no ;;
        --llvm) CHECK_LLVM=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

need()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 was not found in PATH" >&2
        return 1
    fi
}

missing_sources()
{
    [ ! -f "$ROOT/lib/DolRecomp/CMakeLists.txt" ] ||
        [ ! -f "$ROOT/lib/ModernGekko/CMakeLists.txt" ]
}

fetch_sources()
{
    need git
    echo "Fetching ModernGekko, DolRecomp, and their submodules..."
    git -C "$ROOT" submodule sync --recursive
    git -C "$ROOT" submodule update --init --recursive
}

if missing_sources; then
    echo "ModernGekko and DolRecomp sources are not checked out."
    case "$FETCH" in
        yes) fetch_sources ;;
        no)
            echo "Run 'git submodule update --init --recursive' to fetch them." >&2
            exit 1
            ;;
        ask)
            if [ -t 0 ]; then
                printf "Fetch them now? [Y/n] "
                read -r answer
                case "$answer" in
                    ""|y|Y|yes|YES|Yes) fetch_sources ;;
                    *)
                        echo "Cannot validate the build without the sources." >&2
                        exit 1
                        ;;
                esac
            else
                echo "Run 'make check FETCH=1' to fetch them automatically." >&2
                exit 1
            fi
            ;;
    esac
fi

need cmake
need ninja
need git

CHECK_ROOT=${TMPDIR:-/tmp}/moderngekko-build-check-$$
trap 'rm -rf "$CHECK_ROOT"' EXIT HUP INT TERM

set -- -S "$ROOT/scripts/compiler-check" -B "$CHECK_ROOT" -G Ninja
if [ "$CHECK_LLVM" -eq 1 ]; then
    set -- "$@" -DMODERNGEKKO_CHECK_LLVM=ON
fi
if [ -n "${LLVM_DIR:-}" ]; then
    set -- "$@" "-DLLVM_DIR=$LLVM_DIR"
fi

cmake "$@"
cmake --build "$CHECK_ROOT"

echo
echo "Build requirements satisfied."
