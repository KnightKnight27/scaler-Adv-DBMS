$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\minidb_cli.exe"
$data = Join-Path $root "bench_data"
$outputFile = Join-Path $PSScriptRoot "benchmark_output.txt"

if (!(Test-Path $exe)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $root "build") | Out-Null
    g++ -std=c++17 -Wall -Wextra -I (Join-Path $root "src") `
        (Join-Path $root "src\minidb.cpp") `
        (Join-Path $root "src\main.cpp") `
        -o $exe
}

if (Test-Path $data) {
    Remove-Item -LiteralPath $data -Recurse -Force
}

$commands = @(
    "CREATE TABLE users (id, name, age);",
    "CREATE TABLE orders (id, user_id, amount);",
    "INSERT INTO users VALUES (1, Ada, 31);",
    "INSERT INTO users VALUES (2, Grace, 28);",
    "INSERT INTO users VALUES (3, Linus, 42);",
    "INSERT INTO orders VALUES (10, 1, 200);",
    "INSERT INTO orders VALUES (11, 3, 450);",
    "INSERT INTO orders VALUES (12, 2, 50);",
    "SELECT * FROM users;",
    "SELECT name FROM users WHERE id = 1;",
    "INDEX_DEMO users;",
    "SELECT name,amount FROM users JOIN orders ON users.id = orders.user_id WHERE amount > 100;",
    "STORAGE_DEMO;",
    "LOCK_DEMO;",
    "PERF_DEMO;",
    "DELETE FROM users WHERE id = 2;",
    "SELECT * FROM users;",
    "exit"
)

$inputText = ($commands -join [Environment]::NewLine) + [Environment]::NewLine
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$output = $inputText | & $exe $data 2>&1
$stopwatch.Stop()

$output | Set-Content -Path $outputFile

$queryCount = ($commands | Where-Object { $_ -ne "exit" }).Count
$totalMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
$throughput = [Math]::Round(($queryCount / [Math]::Max($stopwatch.Elapsed.TotalSeconds, 0.001)), 2)
$heapBytes = 0
$walBytes = 0
if (Test-Path $data) {
    $heapBytes = (Get-ChildItem -LiteralPath $data -Filter "*.heap" -File | Measure-Object -Property Length -Sum).Sum
    $walFile = Join-Path $data "minidb.wal"
    if (Test-Path $walFile) {
        $walBytes = (Get-Item -LiteralPath $walFile).Length
    }
}

Write-Host "MiniDB benchmark/demo completed"
Write-Host "Commands: $queryCount"
Write-Host "Latency: $totalMs ms total"
Write-Host "Throughput: $throughput commands/sec"
Write-Host "Heap bytes: $heapBytes"
Write-Host "WAL bytes: $walBytes"
Write-Host "Transcript: $outputFile"
