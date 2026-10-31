@echo off
rem Builds build\dinput8.dll (x86) from src\. Requires Visual Studio (or Build Tools) with the C++ x86/x64 tools.
rem Compile build\dinput8.dll (x86) depuis src\. Nécessite Visual Studio (ou Build Tools) avec les outils C++ x86/x64.
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found: install Visual Studio Build Tools with "Desktop development with C++".
  exit /b 1
)
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo No Visual Studio installation with the C++ x86/x64 tools was found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%HERE%build" mkdir "%HERE%build"
cd /d "%HERE%build" || exit /b 1
cl /nologo /LD /O1 /MT /W4 /EHsc /GS /utf-8 "%HERE%src\fear3fix.cpp" /link /DEF:"%HERE%src\dinput8.def" /OUT:dinput8.dll /MACHINE:X86 || exit /b 1
copy /y "%HERE%fear3fix.ini" . >nul
echo OK: %HERE%build\dinput8.dll
