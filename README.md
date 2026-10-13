# cleanmodels-qt

Qt6 GUI for [cleanmodels](https://github.com/plenarius/cleanmodels), a tool to validate, repair, compile, and decompile Neverwinter Nights MDL model files.

![Qt6](https://img.shields.io/badge/Qt-6.7-41cd52) ![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)

## Features

- Pick a folder (or drag one in) and run check, repair, compile, or decompile over every model in it, with per-file progress and status
- A findings column and a detail pane per file: what was wrong, what was fixed, and any warnings the parser raised on damaged lines
- 3D model preview with an OpenGL viewport: **double-click a file** in the list, or right-click and choose *Preview in viewport*
- Full access to the cleanmodels repair, transform, and tile options
- Persistent settings for directories, options, and camera

## Download

Grab the latest build for your platform from the [Releases](https://github.com/plenarius/cleanmodels-qt/releases) page:

| Platform | File |
|---|---|
| Linux (x86_64) | `cleanmodels-qt-linux-amd64.AppImage` (mark it executable, then run it) |
| Linux (ARM64) | `cleanmodels-qt-linux-arm64.AppImage` |
| macOS | `cleanmodels-qt-macos.zip` |
| Windows (x86_64) | `cleanmodels-qt-windows-amd64.zip` |

Each download already contains the matching [cleanmodels](https://github.com/plenarius/cleanmodels) command-line tool, so there is nothing else to install. The window shows which version it is using in the Raw Log (`CLI: <path>`).

### Using your own cleanmodels

The app uses the `cleanmodels` next to it first, then one in your `PATH`. To run a newer or custom build, put it beside the app or on your `PATH`. It must be the Go-based cleanmodels **v4 or newer**; the legacy Prolog CLI is not supported (for the old Prolog-based GUI, see [cleanmodels-qt v0.8.0](https://github.com/plenarius/cleanmodels-qt/releases/tag/build0.8.0-HEAD)).

When you build from source (below), the build does not bundle the CLI, so you need to supply one this way.

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
