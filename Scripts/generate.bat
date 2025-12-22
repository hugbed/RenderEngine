@echo off
echo "Generating in: " %~dp0Build
mkdir %~dp0..\Build
cd %~dp0..\Build
cmake .. -G "Visual Studio 18 2026" -DCMAKE_GENERATOR_PLATFORM=x64
