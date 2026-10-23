@echo off

D:\CMake\bin\cmake.exe -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=D:/WinLibs/mingw64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=D:/WinLibs/mingw64/bin/g++.exe ^
  -DCMAKE_MAKE_PROGRAM=D:/WinLibs/mingw64/bin/mingw32-make.exe ^
  -DCMAKE_PREFIX_PATH=D:/WinLibs/mingw64

if errorlevel 1 exit /b 1

D:\WinLibs\mingw64\bin\mingw32-make.exe -C build -j4