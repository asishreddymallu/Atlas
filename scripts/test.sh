#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON > /dev/null
cmake --build build-tests > /dev/null

TEST_OUTPUT="$(ctest --test-dir build-tests 2>&1)" || true

if [[ "${TEST_OUTPUT}" =~ ([0-9]+)%\ tests\ passed,\ ([0-9]+)\ tests\ failed\ out\ of\ ([0-9]+) ]]; then
    FAILED="${BASH_REMATCH[2]}"
    TOTAL="${BASH_REMATCH[3]}"
    PASSED=$(( TOTAL - FAILED ))
    echo "${TOTAL} total / ${PASSED} passed / ${FAILED} failed"
    if [[ "${FAILED}" -ne 0 ]]; then
        exit 1
    fi
else
    echo "CTest output format unrecognized" >&2
    exit 1
fi
