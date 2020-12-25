@echo off
echo "Generating in: " %~dp0Build
mkdir %~dp0..\Build
cd %~dp0..\Build
cmake .. -G "Visual Studio 16 2019" -DCMAKE_GENERATOR_PLATFORM=x64
