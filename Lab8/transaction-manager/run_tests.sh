#!/usr/bin/env bash

set -u

echo "Starting Lab 8 transaction manager checks"

if ! make build; then
    echo "Build failed"
    exit 1
fi

echo "Build completed"

if ! ./tx_manager --test; then
    echo "Automated tests failed"
    exit 1
fi

echo "Automated tests passed"

if ! ./tx_manager; then
    echo "Demo execution failed"
    exit 1
fi

echo "Demo execution completed successfully"
