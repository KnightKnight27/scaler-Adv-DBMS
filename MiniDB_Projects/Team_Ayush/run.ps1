# Build (if needed) and run MiniDB.
# Usage:
#   .\run.ps1                 # build + start the REPL
#   .\run.ps1 .test storage   # build + run a one-shot command
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

& (Join-Path $root "build.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $root "minidb.exe") @args
