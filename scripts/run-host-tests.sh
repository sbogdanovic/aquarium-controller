#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${ROOT_DIR}/build-host-tests"

cmake -S "${ROOT_DIR}/tests/host" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"
( cd "${BUILD_DIR}" && ctest --output-on-failure )
