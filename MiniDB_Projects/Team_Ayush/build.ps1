# Build MiniDB with MinGW g++ (C++14). No CMake/Make required.
# Usage:  .\build.ps1
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

# Collect every translation unit under src/.
$sources = Get-ChildItem -Path (Join-Path $root "src") -Recurse -Filter *.cpp |
           ForEach-Object { $_.FullName }

if (-not $sources) {
    Write-Host "No source files found under src/." -ForegroundColor Red
    exit 1
}

$gxxArgs = @(
    "-std=c++14", "-O2", "-Wall", "-pedantic",
    "-I", (Join-Path $root "src")
) + $sources + @(
    "-o", (Join-Path $root "minidb.exe"),
    "-static", "-static-libgcc", "-static-libstdc++"
)

Write-Host "Compiling $($sources.Count) source file(s)..." -ForegroundColor Cyan
& g++ @gxxArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build OK -> minidb.exe" -ForegroundColor Green
} else {
    Write-Host "Build FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}
