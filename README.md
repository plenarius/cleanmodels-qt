# cleanmodels-qt
QT front end for cleanmodels-cli, a tool to fix and clean NWN:EE mdl files.

# Download

You can download binaries from the [Releases](https://github.com/plenarius/cleanmodels-qt/releases) tab.

# Building

On Ubuntu/Debian
```
$ git clone https://github.com/plenarius/cleanmodels-qt
$ cd cleanmodels-qt
$ mkdir build
$ sudo apt-get install qtbase5-dev
$ cd build && cmake .. && make
```

This will create an executable `cleanmodels-qt` binary in your current folder.