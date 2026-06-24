#!/usr/bin/env bash
set -euo pipefail

if ! command -v strace >/dev/null 2>&1; then
  echo "strace is not installed. Install it and run this script on Linux." >&2
  exit 1
fi

CXX="${CXX:-g++}"
SOURCE="file_syscall.cpp"
BINARY="file_syscall"
TRACE_FILE="strace_output.txt"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "strace and this raw syscall demo are intended to run on Linux." >&2
  exit 1
fi

"${CXX}" -std=c++17 -Wall -Wextra -pedantic "${SOURCE}" -o "${BINARY}"
strace -o "${TRACE_FILE}" -e trace=openat,write,lseek,read,fstat,close ./"${BINARY}"

echo "Trace saved to ${TRACE_FILE}"
