#!/usr/bin/env bash
set -euo pipefail
if [[ -f .env ]]; then
    set -a
    source <(tr -d '\r' < .env)
    set +a
fi
for executable in cmake-build-debug/hw2.exe cmake-build-debug/hw2 build/hw2.exe build/hw2; do
    if [[ -f "$executable" ]]; then
        if [[ "$executable" == *.exe ]] && command -v cygpath >/dev/null; then
            cache="${executable%/*}/CMakeCache.txt"
            if [[ -f "$cache" ]]; then
                compiler=$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$cache")
                if [[ -n "$compiler" ]]; then
                    export PATH="$(cygpath -u "${compiler%/*}"):$PATH"
                fi
            fi
        fi
        exec "./$executable"
    fi
done
echo 'Run compile.sh before execute.sh' >&2
exit 1
