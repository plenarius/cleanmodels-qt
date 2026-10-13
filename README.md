# cleanmodels-qt

Qt6 GUI for [cleanmodels](https://github.com/plenarius/cleanmodels), a tool to validate, repair, compile, and decompile Neverwinter Nights MDL model files.

![Qt6](https://img.shields.io/badge/Qt-6.7-41cd52) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)

## Features

- Batch processing with per-file progress, status, and detailed findings
- 3D model preview with OpenGL viewport
- Full access to all cleanmodels repair, transform, and tile operations
- Compile (ASCII to binary) and decompile (binary to ASCII) modes
- Persistent settings for directories, options, and camera

## Requirements

- **cleanmodels** CLI binary — download from [cleanmodels releases](https://github.com/plenarius/cleanmodels/releases) and place it in your PATH or alongside this application

> **Note:** cleanmodels-qt requires the Go-based cleanmodels v4+. The legacy Prolog CLI is not supported.
> If you need the old Prolog-based GUI, see [cleanmodels-qt v0.8.0](https://github.com/plenarius/cleanmodels-qt/releases/tag/build0.8.0-HEAD).

## Download

Grab the latest binary for your platform from the [Releases](https://github.com/plenarius/cleanmodels-qt/releases) page.

## Building from source

Requires Qt 6.7+ with OpenGL support, CMake 3.16+, and a C++17 compiler.

### Linux (Ubuntu/Debian)

```
sudo apt-get install qt6-base-dev qt6-opengl-dev libgl1-mesa-dev
git clone https://github.com/plenarius/cleanmodels-qt
cd cleanmodels-qt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### macOS

```
brew install qt@6
git clone https://github.com/plenarius/cleanmodels-qt
cd cleanmodels-qt
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
cmake --build build --parallel
```

### Windows

Install Qt 6.7+ via the [Qt Online Installer](https://www.qt.io/download-qt-installer), then:

```
git clone https://github.com/plenarius/cleanmodels-qt
cd cleanmodels-qt
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64
cmake --build build --config Release
```

## License

MIT
