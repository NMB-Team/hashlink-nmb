<div align=center>
<a href="https://hashlink.haxe.org"><img src="https://hashlink.haxe.org/hashlink.svg" alt="HashLink" /></a>

# HashLink NMB

[![Build Status](https://dev.azure.com/HaxeFoundation/GitHubPublic/_apis/build/status/HaxeFoundation.hashlink?branchName=master)](https://dev.azure.com/HaxeFoundation/GitHubPublic/_build/latest?definitionId=4&branchName=master)
[![Build Status](https://github.com/NMB-Team/hashlink-nmb/workflows/Build/badge.svg "GitHub Actions")](https://github.com/NMB-Team/hashlink-nmb/actions?query=workflow%3ABuild)
</div>

### HashLink is a virtual machine for Haxe <https://hashlink.haxe.org>

## Building on Linux/OSX

HashLink and generated HLC applications require C23 mode. CMake builds require CMake 3.21 or newer. Make builds detect `-std=c23` or the older `-std=c2x` spelling and fail if neither supports `nullptr`.

HashLink is distributed with some graphics libraries allowing to develop various applications, you can manually disable the libraries you want to compile in Makefile.
Here's the dependencies that you install in order to compile all the libraries:

* fmt: libpng-dev libturbojpeg-dev libvorbis-dev
* openal: libopenal-dev
* ssl: libmbedtls-dev
* uv: libuv1-dev
* sqlite: libsqlite3-dev
* updater HTTPS: libcurl4-openssl-dev

To install all dependencies on the latest **Ubuntu**, for example:

`sudo apt-get install libpng-dev libturbojpeg-dev libvorbis-dev libopenal-dev libmbedtls-dev libuv1-dev libsqlite3-dev libcurl4-openssl-dev`

For 16.04, see [this note](https://github.com/HaxeFoundation/hashlink/issues/147).

To install all dependencies on the latest **Fedora**, for example:

`sudo dnf install libpng-devel turbojpeg-devel libvorbis-devel openal-soft-devel mbedtls-devel libuv-devel sqlite-devel libcurl-devel`

**And on OSX:**

`brew bundle` to install the dependencies listed in [Brewfile](Brewfile).

Once dependencies are installed you can simply call:

`make`

To be able to use hashlink binary with the debugger you can then call:

`sudo make codesign_osx`

To install hashlink binaries on your system you can then call:

`make install`

## Building on Windows

Open `hl.sln` using Visual Studio C++, or use CMake to build HashLink. Limen is installed separately.

MSVC builds use `/std:clatest` and require a toolset with C23 `typeof` support; the existing HashLink headers provide MSVC's `nullptr` compatibility definition.

When compiling generated HLC code manually, pass `-std=c23` (`-std=c2x` on older GCC/Clang versions) or `/std:clatest` with MSVC. For example, from a directory containing generated `hello.c`:

```sh
cc -std=c23 -I. -I/usr/local/include hello.c -L/usr/local/lib -lhl -lm -o hello
```

To build all of HashLink libraries it is required to download several additional distributions, read each library README file (in hashlink/libs/xxx/README.md) for additional information.

In short you'll probably need:

- [openal-soft](https://github.com/kcat/openal-soft/releases/download/1.23.1/openal-soft-1.23.1-bin.zip), extract to `<hashlink>/include/openal`

## Limen

Limen is an optional component and is not included with HashLink.

Install it with:

```sh
hl limen install
```

Update it independently:

```sh
hl limen update
```

Reinstall the latest verified release, replacing all Limen-managed files:

```sh
hl limen reinstall
```

Check the installed version:

```sh
hl limen status
```

Remove it:

```sh
hl limen remove
```

Limen uses `.limen-manifest`; `hl update` owns `.hl-manifest` and manages HashLink only. Removal preserves modified files and retains the Limen manifest until they are restored or moved.

Build Limen from source independently in the [Limen repository](https://github.com/NMB-Team/Limen).

## Self-update

Run `hl update` to check the latest HashLink NMB release. It verifies every managed installation file by size and SHA-256, repairs missing or modified official files, and installs newer releases. Files not managed by HashLink are preserved. `hl update --check` reports changes without installing them; `hl update --force` reinstalls managed files.

The installation is located from the running `hl` executable, regardless of the current directory. System-wide Linux installations may require running with sufficient permissions; the updater does not invoke `sudo`.

Linux ARM64 releases include a native `hl` updater command. The VM/JIT remains unavailable on ARM64.

The updater uses the latest release archives offered for manual download: ZIP on Windows and tar.gz on Linux. A per-platform manifest records the managed files. The updater verifies the manifest and archive against their GitHub release asset SHA-256 digests, then verifies extracted files before changing the installation.

## Debugging

You can debug Haxe/HashLink applications by using the [Visual Studio Code Debugger](https://marketplace.visualstudio.com/items?itemName=HaxeFoundation.haxe-hl)

## Documentation

Read the [Documentation](https://github.com/HaxeFoundation/hashlink/wiki) on the HashLink wiki.
