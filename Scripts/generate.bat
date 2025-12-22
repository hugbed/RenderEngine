@echo off
echo "Generating in: " %~dp0Build
mkdir %~dp0..\Intermediate\Projects
cd %~dp0..\Intermediate\Projects
cmake ..\.. -G "Visual Studio 18 2026" -DCMAKE_GENERATOR_PLATFORM=x64
