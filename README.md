<div align=center>
<a href="https://hashlink.haxe.org"><img src="https://hashlink.haxe.org/hashlink.svg" alt="HashLink" /></a>

# HashLink NMB

[![Build Status](https://dev.azure.com/HaxeFoundation/GitHubPublic/_apis/build/status/HaxeFoundation.hashlink?branchName=master)](https://dev.azure.com/HaxeFoundation/GitHubPublic/_build/latest?definitionId=4&branchName=master)
[![Build Status](https://github.com/NMB-Team/hashlink-nmb/workflows/Build/badge.svg "GitHub Actions")](https://github.com/NMB-Team/hashlink-nmb/actions?query=workflow%3ABuild)
</div>

### HashLink is a virtual machine for Haxe <https://hashlink.haxe.org>

## Building on Linux/OSX

HashLink is distributed with some graphics libraries allowing to develop various applications, you can manually disable the libraries you want to compile in Makefile.
Here's the dependencies that you install in order to compile all the libraries:

* fmt: libpng-dev libturbojpeg-dev libvorbis-dev
* openal: libopenal-dev
* ssl: libmbedtls-dev
* uv: libuv1-dev
* sqlite: libsqlite3-dev

To install all dependencies on the latest **Ubuntu**, for example:

`sudo apt-get install libpng-dev libturbojpeg-dev libvorbis-dev libopenal-dev libmbedtls-dev libuv1-dev libsqlite3-dev`

For 16.04, see [this note](https://github.com/HaxeFoundation/hashlink/issues/147).

To install all dependencies on the latest **Fedora**, for example:

`sudo dnf install libpng-devel turbojpeg-devel libvorbis-devel openal-soft-devel mbedtls-devel libuv-devel sqlite-devel`

**And on OSX:**

`brew bundle` to install the dependencies listed in [Brewfile](Brewfile).

Once dependencies are installed you can simply call:

`make`

To be able to use hashlink binary with the debugger you can then call:

`sudo make codesign_osx`

To install hashlink binaries on your system you can then call:

`make install`

## Building on Windows

Open `hl.sln` using Visual Studio C++ to build HashLink without LIMEN, or use CMake to build the complete runtime.

To build all of HashLink libraries it is required to download several additional distributions, read each library README file (in hashlink/libs/xxx/README.md) for additional information.

In short you'll probably need:

- [openal-soft](https://github.com/kcat/openal-soft/releases/download/1.23.1/openal-soft-1.23.1-bin.zip), extract to `<hashlink>/include/openal`

## LIMEN

LIMEN is maintained in [NMB-Team/Limen](https://github.com/NMB-Team/Limen).
The `libs/limen` submodule is the single source of truth for the pinned revision.
CMake initializes the submodule automatically when necessary and builds LIMEN with HashLink for local source builds.
GitHub Actions synchronizes the LIMEN submodule checkout to the commit targeted by its `latest` release, downloads the matching platform package, and includes its modules and runtime libraries in every HashLink package.
LIMEN revisions are never advanced automatically. To update the pin, run the
`Pin LIMEN revision` workflow manually and provide a full 40-character commit
SHA. HashLink tests that exact revision across the complete build matrix and
updates the submodule only after every integration job succeeds.

To update and test the pin locally:

```
git -C libs/limen fetch origin <commit-sha>
git -C libs/limen checkout --detach <commit-sha>
git add libs/limen
```

Clone everything and build manually:

```
git clone --recurse-submodules https://github.com/NMB-Team/hashlink-nmb.git
cd hashlink-nmb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix dist
```

To package prebuilt LIMEN binaries instead of compiling the submodule:

```
cmake -S . -B build \
  -DWITH_LIMEN=OFF \
  -DLIMEN_PREBUILT_DIR=/path/to/extracted/limen-package
```

For LIMEN development, use a separate checkout:

```
cmake -S . -B build -DLIMEN_SOURCE_DIR=/path/to/Limen
```

## Debugging

You can debug Haxe/HashLink applications by using the [Visual Studio Code Debugger](https://marketplace.visualstudio.com/items?itemName=HaxeFoundation.haxe-hl)

## Documentation

Read the [Documentation](https://github.com/HaxeFoundation/hashlink/wiki) on the HashLink wiki.
