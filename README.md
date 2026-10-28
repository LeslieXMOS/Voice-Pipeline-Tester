# Voice Pipeline Test

## Introduction

This project uses xscope_fileio to test Voice Pipeline on XU316

## Configuration

This project uses XCommon Cmake and compatible with XMOS VSCode extension [XMOS XTC Tools](https://marketplace.visualstudio.com/items?itemName=xmos.xtc-tools).

The test sequence is initialized by python script under [host](host) folder, make sure uses venv or conda and install [requirements.txt](host/requirement.txt)

xscope_fileio is shipped with XCommon Cmake, you can only install it after you configure the project with cmake

## Build (Terminal)

```bash
cmake -G "Unix Makefiles" -B build
xmake -C build
```

## Run

```bash
python host/test.py
```