@echo off
setlocal
cd /d "%~dp0"
set "OUTDIR=build\Release"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OUTDIR%" exit /b 1
where cl >nul 2>nul
if not errorlevel 1 goto msvc
where g++ >nul 2>nul
if not errorlevel 1 goto mingw
where zig >nul 2>nul
if not errorlevel 1 goto zig
echo No C++ compiler found. Use a Visual Studio x64 Native Tools prompt,
echo or add MinGW-w64 g++ / Zig to PATH. See README.md.
exit /b 1

:msvc
rc /nologo /fo%OUTDIR%\restcue.res restcue.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 main.cpp %OUTDIR%\restcue.res /Fo%OUTDIR%\main.obj /Fe%OUTDIR%\RestCue.exe /link /MANIFEST:NO /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib gdiplus.lib comctl32.lib dwmapi.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT tests\timer_tests.cpp /Fo%OUTDIR%\timer_tests.obj /Fe%OUTDIR%\timer_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT tests\settings_tests.cpp /Fo%OUTDIR%\settings_tests.obj /Fe%OUTDIR%\settings_tests.exe /link shell32.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT tests\overlay_tests.cpp /Fo%OUTDIR%\overlay_tests.obj /Fe%OUTDIR%\overlay_tests.exe /link gdiplus.lib gdi32.lib user32.lib
if errorlevel 1 exit /b 1
goto test

:mingw
windres -I . restcue.rc -O coff -o %OUTDIR%\restcue_resources.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -D_WIN32_WINNT=0x0A00 -municode -mwindows -static main.cpp %OUTDIR%\restcue_resources.o -o %OUTDIR%\RestCue.exe -luser32 -lgdi32 -lshell32 -lole32 -lgdiplus -lcomctl32 -ldwmapi -ladvapi32
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -static tests\timer_tests.cpp -o %OUTDIR%\timer_tests.exe
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -static tests\settings_tests.cpp -o %OUTDIR%\settings_tests.exe -lshell32 -lole32
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -static tests\overlay_tests.cpp -o %OUTDIR%\overlay_tests.exe -lgdiplus -lgdi32 -luser32
if errorlevel 1 exit /b 1
goto test

:zig
zig rc /I . /fo %OUTDIR%\restcue.res restcue.rc
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -target x86_64-windows-gnu -municode -Wl,--subsystem,windows -static main.cpp %OUTDIR%\restcue.res -o %OUTDIR%\RestCue.exe -luser32 -lgdi32 -lshell32 -lole32 -lgdiplus -lcomctl32 -ldwmapi -ladvapi32
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -target x86_64-windows-gnu -static tests\timer_tests.cpp -o %OUTDIR%\timer_tests.exe
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -target x86_64-windows-gnu -static tests\settings_tests.cpp -o %OUTDIR%\settings_tests.exe -lshell32 -lole32
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -target x86_64-windows-gnu -static tests\overlay_tests.cpp -o %OUTDIR%\overlay_tests.exe -lgdiplus -lgdi32 -luser32
if errorlevel 1 exit /b 1

:test
%OUTDIR%\timer_tests.exe
if errorlevel 1 exit /b 1
%OUTDIR%\settings_tests.exe
if errorlevel 1 exit /b 1
%OUTDIR%\overlay_tests.exe
if errorlevel 1 exit /b 1
rem The project folder is marked Low Integrity, which makes Windows refuse the tray icon.
icacls "%OUTDIR%\RestCue.exe" /setintegritylevel M >nul
echo.
echo Built: %CD%\%OUTDIR%\RestCue.exe
