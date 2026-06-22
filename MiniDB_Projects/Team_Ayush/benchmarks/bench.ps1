# Run the MiniDB benchmark suite and capture results.
# Usage:  .\benchmarks\bench.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

& (Join-Path $root "build.ps1") | Out-Null
$exe = Join-Path $root "minidb.exe"
$out = Join-Path $root "benchmarks\results.txt"

Write-Host "Running benchmarks..." -ForegroundColor Cyan
& $exe bench | Tee-Object -FilePath $out
Write-Host "`nResults written to $out" -ForegroundColor Green
