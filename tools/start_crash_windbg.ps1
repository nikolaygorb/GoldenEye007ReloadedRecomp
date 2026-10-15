param([string]$Script = 'windbg_crash.cmd')

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot 'out\build\win-amd64-relwithdebinfo\goldeneye007reloadedrecomp.exe'
$commands = Join-Path $PSScriptRoot $Script
$logDir = Join-Path $repoRoot 'out\build\win-amd64-relwithdebinfo\logs'
$windbg = Get-Command WinDbgX.exe -ErrorAction Stop

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build executable not found: $executable"
}
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

Start-Process -FilePath $windbg.Source -ArgumentList "-o -c `"`$<$commands`" `"$executable`""
Write-Output "WinDbg started. Log: $logDir\windbg_crash.log"
