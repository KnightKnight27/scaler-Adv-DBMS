#!/usr/bin/env bash
set -euo pipefail

FILE="${1:-lab1_syscall_output.txt}"

if [[ ! -f "${FILE}" ]]; then
  echo "${FILE} not found. Run ./run.sh first." >&2
  exit 1
fi

ls -li "${FILE}"
stat "${FILE}"
