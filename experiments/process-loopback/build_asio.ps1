# Builds asio_probe.exe. Uses JUCE's bundled ASIO SDK headers (no Steinberg download).
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$asioInc = Join-Path $PSScriptRoot "..\..\build\_deps\juce-src\modules\juce_audio_devices\native\asio"
if (-not (Test-Path (Join-Path $asioInc "iasiodrv.h"))) { Write-Error "ASIO headers not found at $asioInc"; exit 1 }
cmd /c "`"$vcvars`" >nul && cl /nologo /EHsc /std:c++17 /O2 /W3 /I`"$asioInc`" `"$PSScriptRoot\asio_probe.cpp`" /Fe:`"$PSScriptRoot\asio_probe.exe`" /Fo:`"$PSScriptRoot\asio_probe.obj`" ole32.lib user32.lib advapi32.lib mmdevapi.lib"
exit $LASTEXITCODE
