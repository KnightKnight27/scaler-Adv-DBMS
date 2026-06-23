$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path "build" | Out-Null

g++ -std=c++17 -Wall -Wextra -I src src/minidb.cpp src/main.cpp -o build/minidb_cli.exe
g++ -std=c++17 -Wall -Wextra -I src src/minidb.cpp tests/smoke_test.cpp -o build/minidb_smoke.exe

Write-Host "Built build\minidb_cli.exe and build\minidb_smoke.exe"
