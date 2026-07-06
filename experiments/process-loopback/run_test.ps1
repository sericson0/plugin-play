# Orchestrates the M2 driverless-capture experiment:
# starts a tone-playing child process, runs loopback_test.exe against its PID.
param([int]$PhaseSeconds = 3, [string]$MuteLever = "endpoint")  # "endpoint" or "session"

$exe = Join-Path $PSScriptRoot "loopback_test.exe"
if (-not (Test-Path $exe)) { Write-Error "Build loopback_test.exe first (see build.ps1)"; exit 1 }

$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList `
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'player.ps1')

Start-Sleep -Seconds 2   # let the tone start rendering

& $exe $player.Id $PhaseSeconds $MuteLever
$code = $LASTEXITCODE

Stop-Process -Id $player.Id -Force -Confirm:$false
exit $code
