#!/usr/bin/env bash
set -euo pipefail

CXX="${CXX:-g++}"
SOURCE="file_syscall.cpp"
BINARY="file_syscall"

"${CXX}" -std=c++17 -Wall -Wextra -pedantic "${SOURCE}" -o "${BINARY}"
./"${BINARY}"
