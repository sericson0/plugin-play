# Builds list_sources.exe and quality_test.exe (reqs #1 and #3).
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$common = "/nologo /EHsc /std:c++17 /O2 /W3"
cmd /c "`"$vcvars`" >nul && cl $common `"$PSScriptRoot\list_sources.cpp`" /Fe:`"$PSScriptRoot\list_sources.exe`" /Fo:`"$PSScriptRoot\list_sources.obj`" ole32.lib mmdevapi.lib"
$a = $LASTEXITCODE
cmd /c "`"$vcvars`" >nul && cl $common `"$PSScriptRoot\quality_test.cpp`" /Fe:`"$PSScriptRoot\quality_test.exe`" /Fo:`"$PSScriptRoot\quality_test.obj`" ole32.lib mmdevapi.lib propsys.lib"
$b = $LASTEXITCODE
if ($a -ne 0 -or $b -ne 0) { exit 1 } else { exit 0 }
