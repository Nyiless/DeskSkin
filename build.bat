@echo off
rem Builds a portable DeskSkin folder in dist\DeskSkin. Needs Visual Studio 2022 (Desktop C++ workload)
rem and CMake on PATH. If you do not want to install anything, use the GitHub Actions build instead.
setlocal
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build --config Release || goto :fail
ctest --test-dir build -C Release --output-on-failure || goto :fail
if exist dist rmdir /s /q dist
mkdir dist\DeskSkin
copy /y build\Release\DeskSkin.exe dist\DeskSkin\ >nul
xcopy /e /i /y skins dist\DeskSkin\Skins >nul
copy /y README.txt dist\DeskSkin\ >nul
echo.
echo Done: dist\DeskSkin\DeskSkin.exe
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
