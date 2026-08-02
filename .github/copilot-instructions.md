# HashLink – Copilot Agent Instructions

## Project Overview

HashLink is a virtual machine for the [Haxe](https://haxe.org) programming language. It can either execute `.hl` bytecode produced by the Haxe compiler (using the `hl` binary), or compile Haxe code to C for standalone native execution (HLC mode, requiring all dependent libraries but not the `hl` binary). This repository contains:

- The HashLink VM (`hl` binary) and runtime library (`libhl.so`)
- Optional native extension libraries (cross-platform: `fmt.hdll`, `ssl.hdll`, `openal.hdll`, `uv.hdll`, `mysql.hdll`, `sqlite.hdll`, `heaps.hdll`, `ui.hdll`, plus LIMEN's `limen.hdll`, `opengl.limen`, and `vulkan.limen`; platform-specific: LIMEN's `d3d11.limen`/`d3d12.limen` renderers on Windows)
- Source code of the HashLink core library (hl/libhl) in `src/` and native extension library implementations in `libs/`
- Test programs in `other/tests/`
- CMake build support alongside the classic `Makefile`

## Building HashLink on Linux

### Install system dependencies

```bash
sudo apt-get update -y
sudo apt-get install --no-install-recommends -y \
  libmbedtls-dev \
  libopenal-dev \
  libpng-dev \
  libturbojpeg-dev \
  libuv1-dev \
  libvorbis-dev \
  libsqlite3-dev
```

### Build and install

```bash
make
sudo make install
sudo ldconfig
```

This installs the `hl` binary to `/usr/local/bin` and `libhl.so` plus the `.hdll` extension libraries to `/usr/local/lib`.

## Compiling and Running a Haxe Program

Haxe source files are compiled to HashLink bytecode with the Haxe compiler, then executed with `hl`:

```bash
# Compile a Haxe program to HashLink bytecode
haxe --hl program.hl -cp <source-dir> -m <MainClass>

# Run the bytecode
hl program.hl
```

Example using the test program included in the repository:

```bash
haxe --hl hello.hl -cp other/tests -m HelloWorld
hl hello.hl
```

## Running Tests

The main test in the build workflow:

```bash
haxe -hl hello.hl -cp other/tests -main HelloWorld -D interp
hl hello.hl
```

To test the C output path:

```bash
haxe -hl src/_main.c -cp other/tests -main HelloWorld
make hlc
./hlc
```

HashLink is mostly tested as part of the Haxe tests over at https://github.com/HaxeFoundation/haxe/tree/development/tests. There are several test suites:

- `unit/` - General Unit tests
- `misc/hl` - Tests expected to produce failures/warnings or assert specific stdout/stderr output
- `sys/` - Tests specific to the `std/sys` package
- `threads/` - Thread-related tests, generally for `std/sys/thread` package

> **Note:** The latest Haxe nightly generally requires git versions of haxelibs rather than the released ones. For example:
> ```bash
> haxelib git utest https://github.com/haxe-utest/utest.git
> ```

## Key Source Files and Directories

| Path | Description |
|------|-------------|
| `src/` | Source code of the HashLink core library (hl/libhl): JIT compiler, GC, module loader, debugger |
| `src/hl.h` | Main public header for embedding HashLink |
| `libs/` | Native extension libraries. LIMEN is pinned as the `libs/limen` submodule and initialized automatically for CMake builds. |
| `include/` | Vendored third-party headers and libraries. |
| `other/tests/` | Haxe test programs |
| `other/haxelib/` | HashLink haxelib package sources |
| `Makefile` | Legacy HashLink build file for Linux/macOS; it does not build LIMEN |
| `CMakeLists.txt` | Cross-platform build file and the supported LIMEN build path |

## Code Style and Conventions

- The VM and libraries are written in **C11** (`-std=c11`).
- Use `${CC}` for C compilation; `${CXX}` for C++ (only in the `heaps` library).
- Follow the existing pattern of adding new native functions via `HL_PRIM` macros defined in `src/hl.h`.
- New native libraries should be placed under `libs/<name>/` and expose a `DEFINE_PRIM` table.
