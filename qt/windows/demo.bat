@echo off
cd /d "%~dp0"
start "" xastir-qt.exe --no-config --stations examples/stations.json --lat -34.6037 --lon -58.3816 --zoom 11
