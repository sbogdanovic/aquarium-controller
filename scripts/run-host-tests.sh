#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${ROOT_DIR}/build-host-tests"

if command -v ninja >/dev/null 2>&1; then
	GENERATOR="Ninja"
	MAKE_PROGRAM=""
elif command -v make >/dev/null 2>&1; then
	GENERATOR="Unix Makefiles"
	MAKE_PROGRAM="$(command -v make)"
else
	echo "❌ neither ninja nor make is available; cannot run host tests"
	exit 1
fi

if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
	CACHE_GENERATOR=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n1)
	CACHE_MAKE_PROGRAM=$(sed -n 's/^CMAKE_MAKE_PROGRAM:FILEPATH=//p' "${BUILD_DIR}/CMakeCache.txt" | head -n1)

	if [ "${CACHE_GENERATOR}" != "${GENERATOR}" ]; then
		rm -rf "${BUILD_DIR}"
	elif [ "${GENERATOR}" = "Unix Makefiles" ] && [ ! -x "${CACHE_MAKE_PROGRAM}" ]; then
		rm -rf "${BUILD_DIR}"
	fi
fi

if [ "${GENERATOR}" = "Unix Makefiles" ]; then
	cmake -S "${ROOT_DIR}/tests/host" -B "${BUILD_DIR}" -G "${GENERATOR}" -DCMAKE_MAKE_PROGRAM="${MAKE_PROGRAM}"
else
	cmake -S "${ROOT_DIR}/tests/host" -B "${BUILD_DIR}" -G "${GENERATOR}"
fi

cmake --build "${BUILD_DIR}"
( cd "${BUILD_DIR}" && ctest --output-on-failure )
