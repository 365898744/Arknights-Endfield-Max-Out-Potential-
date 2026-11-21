@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /MT /EHsc /utf-8 fakega_slim.cpp /link /DLL /OUT:GameAssembly.dll kernel32.lib
if errorlevel 1 goto end
cl /nologo /O2 /MT /EHsc /utf-8 host_slim.cpp /link /OUT:host_slim.exe kernel32.lib
if errorlevel 1 goto end
host_slim.exe
echo EXIT=%errorlevel%
:end
