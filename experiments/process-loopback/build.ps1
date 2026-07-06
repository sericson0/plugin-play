# Builds loopback_test.exe with VS 2022 BuildTools (x64).
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && cl /nologo /EHsc /std:c++17 /O2 /W3 `"$PSScriptRoot\loopback_test.cpp`" /Fe:`"$PSScriptRoot\loopback_test.exe`" /Fo:`"$PSScriptRoot\loopback_test.obj`" ole32.lib mmdevapi.lib"
exit $LASTEXITCODE
