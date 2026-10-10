Xastir Qt MVP - Windows x64

Extract the ZIP, then double-click xastir-qt.exe. No Qt installation or
separate runtime DLLs are needed. Windows 10/11 x64 is the target platform.
Internet access is needed for OpenStreetMap tiles; downloaded tiles are cached.
Drag to pan, scroll to zoom, and close the window with its X button.
Run demo.bat to display sample APRS stations near Buenos Aires without AGWPE.

To use your existing Xastir configuration, copy xastir.cnf to:
  %USERPROFILE%\.xastir\config\xastir.cnf
Or launch from a terminal with:
  xastir-qt.exe --config "C:\path\to\xastir.cnf"
Set the AGWPE host to the TNC's reachable IP address. A Linux localhost address
will refer to this Windows computer. Serial ports are not part of this MVP.
Transmission requires the configured enable flag or the --agwpe-tx option.

Build: static Qt 6.8.2, MinGW-w64, Windows Schannel TLS, raster map rendering.
All APRS symbol assets and required Qt plugins are embedded in the executable.
WINDOWS-IMPORTS.txt lists the imported Windows operating-system libraries.
SOURCE-COMMIT.txt identifies the application source revision at packaging time.
Sources: https://github.com/hjf/Xastir (qt branch)
Qt source: https://download.qt.io/archive/qt/6.8/6.8.2/submodules/qtbase-everywhere-src-6.8.2.tar.xz
Qt and third-party license texts are included under licenses/.

This is a development test build. Some legacy menu entries remain placeholders.
