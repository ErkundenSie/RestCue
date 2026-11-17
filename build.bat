@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
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
rc /nologo /fobuild\restcue.res restcue.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 main.cpp build\restcue.res /Fobuild\main.obj /Febuild\RestCue.exe /link /MANIFEST:NO /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib gdiplus.lib comctl32.lib dwmapi.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT tests\timer_tests.cpp /Fobuild\timer_tests.obj /Febuild\timer_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT tests\settings_tests.cpp /Fobuild\settings_tests.obj /Febuild\settings_tests.exe /link shell32.lib ole32.lib
if errorlevel 1 exit /b 1
goto test

:mingw
windres -I . restcue.rc -O coff -o build\restcue_resources.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -D_WIN32_WINNT=0x0A00 -municode -mwindows -static main.cpp build\restcue_resources.o -o build\RestCue.exe -luser32 -lgdi32 -lshell32 -lole32 -lgdiplus -lcomctl32 -ldwmapi
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -static tests\timer_tests.cpp -o build\timer_tests.exe
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -static tests\settings_tests.cpp -o build\settings_tests.exe -lshell32 -lole32
if errorlevel 1 exit /b 1
goto test

:zig
zig rc /I . /fo build\restcue.res restcue.rc
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00 -target x86_64-windows-gnu -municode -Wl,--subsystem,windows -static main.cpp build\restcue.res -o build\RestCue.exe -luser32 -lgdi32 -lshell32 -lole32 -lgdiplus -lcomctl32 -ldwmapi
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -target x86_64-windows-gnu -static tests\timer_tests.cpp -o build\timer_tests.exe
if errorlevel 1 exit /b 1
zig c++ -std=c++17 -O2 -Wall -Wextra -target x86_64-windows-gnu -static tests\settings_tests.cpp -o build\settings_tests.exe -lshell32 -lole32
if errorlevel 1 exit /b 1

:test
build\timer_tests.exe
if errorlevel 1 exit /b 1
build\settings_tests.exe
if errorlevel 1 exit /b 1
copy /y build\RestCue.exe RestCue.exe >nul
if errorlevel 1 exit /b 1
echo.
echo Built: %CD%\RestCue.exe
