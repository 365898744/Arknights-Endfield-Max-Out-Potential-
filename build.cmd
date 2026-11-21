@echo off
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE PotentialFix.cpp /link /DLL /OUT:PotentialFix.dll /DEF:PotentialFix.def kernel32.lib
echo ERRORLEVEL=%errorlevel%
endlocal
