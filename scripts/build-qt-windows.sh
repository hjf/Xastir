#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-build a single Windows x64 EXE on Debian using static Qt.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
deps="$root/build/windows-deps"
version=6.8.2
archive="qtbase-everywhere-src-$version.tar.xz"
source_dir="$deps/qtbase-everywhere-src-$version"
sdk="$deps/qt-static-sdk"
jobs=${XASTIR_BUILD_JOBS:-4}
host=${XASTIR_QT_HOST:-/usr}
host_cmake=${XASTIR_QT_HOST_CMAKE:-/usr/lib/x86_64-linux-gnu/cmake}
if [[ $("$host/bin/qmake6" -query QT_VERSION) != "$version" ]]; then
  echo "This build needs native Qt $version host tools (set XASTIR_QT_HOST)." >&2
  exit 1
fi
mkdir -p "$deps"
base="https://download.qt.io/archive/qt/6.8/$version/submodules"
for file in "$archive" "$archive.sha256"; do
  if [[ ! -f "$deps/$file" ]]; then
    curl --fail --location --retry 3 "$base/$file" -o "$deps/$file.partial"
    mv "$deps/$file.partial" "$deps/$file"
  fi
done
(cd "$deps" && sha256sum -c "$archive.sha256")
if [[ ! -d "$source_dir" ]]; then
  tar -xf "$deps/$archive" -C "$deps"
fi
cmake -S "$source_dir" -B "$deps/qt-static-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchains/mingw64.cmake" \
  -DCMAKE_INSTALL_PREFIX="$sdk" -DQT_HOST_PATH="$host" \
  -DQT_HOST_PATH_CMAKE_DIR="$host_cmake" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
  -DINPUT_opengl=no -DQT_FEATURE_opengl=OFF \
  -DFEATURE_opengl_dynamic=OFF -DFEATURE_dynamicgl=OFF \
  -DQT_FEATURE_openssl=OFF -DQT_FEATURE_schannel=ON \
  -DQT_FEATURE_sql=OFF -DQT_FEATURE_printsupport=OFF \
  -DQT_FEATURE_system_zlib=OFF -DQT_FEATURE_system_png=OFF \
  -DQT_FEATURE_system_jpeg=OFF -DQT_FEATURE_system_freetype=OFF
cmake --build "$deps/qt-static-build" --parallel "$jobs"
cmake --install "$deps/qt-static-build"
export XASTIR_WINDOWS_QT="$sdk"
cmake -S "$root" -B "$root/build/windows" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchains/mingw64.cmake" \
  -DCMAKE_PREFIX_PATH="$sdk" -DQT_HOST_PATH="$host" \
  -DQT_HOST_PATH_CMAKE_DIR="$host_cmake" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build "$root/build/windows" --parallel "$jobs"
package="$root/build/windows-package"
mkdir -p "$package/licenses/Qt" "$package/examples"
cp "$root/build/windows/qt/xastir-qt.exe" "$package/"
x86_64-w64-mingw32-strip "$package/xastir-qt.exe"
cp "$root/COPYING" "$package/licenses/Xastir-GPL.txt"
for component in mingw-w64-common gcc-mingw-w64-base gcc-mingw-w64-x86-64-posix-runtime; do
  notice="/usr/share/doc/$component/copyright"
  if [[ -f "$notice" ]]; then
    cp "$notice" "$package/licenses/$component.txt"
  fi
done
cp -R "$source_dir/LICENSES/." "$package/licenses/Qt/"
# Retain upstream attribution metadata and component copyright/license notices.
python3 - "$source_dir" "$package/licenses/Qt-third-party" <<'PYTHON'
import json, pathlib, shutil, sys
source, destination = map(pathlib.Path, sys.argv[1:])
files = set()
for path in (source / "src/3rdparty").rglob("*"):
    name = path.name.lower()
    if path.is_file() and (name.startswith(("license", "licence", "copying", "copyright"))
                          or name == "qt_attribution.json"):
        files.add(path)
    if name == "qt_attribution.json":
        # Qt attribution files can contain literal newlines in descriptions.
        metadata = json.loads(path.read_text(), strict=False)
        for entry in metadata if isinstance(metadata, list) else [metadata]:
            for field in ("LicenseFile", "LicenseFiles", "CopyrightFile"):
                names = entry.get(field, [])
                for filename in names if isinstance(names, list) else [names]:
                    notice = (path.parent / filename).resolve()
                    if notice.is_file() and notice.is_relative_to(source / "src/3rdparty"):
                        files.add(notice)
for path in files:
    target = destination / path.relative_to(source / "src/3rdparty")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, target)
PYTHON
cp "$root/qt/examples/stations.json" "$package/examples/"
cp "$root/qt/windows/README-Windows.txt" "$root/qt/windows/demo.bat" "$package/"
git -C "$root" rev-parse HEAD > "$package/SOURCE-COMMIT.txt"
cp "$deps/$archive.sha256" "$package/QT-SOURCE-SHA256.txt"
x86_64-w64-mingw32-objdump -p "$package/xastir-qt.exe" \
  | sed -n 's/^[[:space:]]*DLL Name: / /p' > "$package/WINDOWS-IMPORTS.txt"
if grep -Ei 'Qt6|libgcc|libstdc|libwinpthread|libssl|libcrypto' "$package/WINDOWS-IMPORTS.txt"; then
  echo "Unexpected external runtime dependency; packaging failed." >&2
  exit 1
fi
(cd "$package" && sha256sum xastir-qt.exe > SHA256SUMS)
(cd "$root/build" && zip -qr xastir-qt-windows-x64.zip windows-package)
echo "Windows package: $root/build/xastir-qt-windows-x64.zip"
