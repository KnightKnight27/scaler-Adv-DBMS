# Build MiniDB and run the end-to-end SQL demo script (demo.sql).
# Uses cmd redirection so the input file is fed verbatim (no PowerShell BOM).
# Usage:  .\run_demo.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

& (Join-Path $root "build.ps1") | Out-Null
if (Test-Path (Join-Path $root "minidb.db")) {
    [System.IO.File]::Delete((Resolve-Path (Join-Path $root "minidb.db")))
}
cmd /c ".\minidb.exe < demo.sql"
