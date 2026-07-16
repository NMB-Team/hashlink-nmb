hlsdl
=====

HLSDL is a SDL3 backend for HashLink
Read https://www.libsdl.org/ for more information on SDL

In order to compile on Windows, download the Development Libraries from SDL3 and unzip them into the hashlink/include/sdl directory, so you have the directories hashlink/include/sdl/include and hashlink/include/sdl/lib available.

Also make sure that SDL3.dll is in your PATH.

ANGLE support
-------------

Enable ANGLE through CMake:

```bash
cmake -S . -B build \
  -DWITH_SDL=ON \
  -DWITH_SDL_ANGLE=ON
```

CMake automatically downloads the correct precompiled package from `angle-nmb-builder`, verifies its SHA-256 and `ANGLE_REVISION`, extracts it atomically, and caches it by immutable builder commit.

Platform mapping:

```text
Windows -> ANGLE Vulkan
Linux   -> ANGLE Vulkan
macOS   -> ANGLE Metal
```

Advanced overrides:

```text
ANGLE_NMB_ROOT            local extracted package; disables network access
ANGLE_NMB_MANIFEST_URL    rolling manifest or private mirror URL
ANGLE_NMB_BUILDER_COMMIT  exact immutable builder commit
ANGLE_NMB_AUTO_DOWNLOAD   enable or disable automatic package resolution
ANGLE_NMB_OFFLINE         disallow all network downloads
```

HashLink builds use the immutable builder state recorded in `other/angle-nmb-builder-commit.txt` by default. Set `ANGLE_NMB_BUILDER_COMMIT` explicitly to consume a different immutable release.

The checked-in Visual Studio project enables ANGLE for x64 configurations. Its pre-build step runs `other/msvc/prepare-angle.ps1`, stores the validated package in `include/angle-nmb`, links `libEGL.lib` and `libGLESv2.lib`, and copies the runtime DLLs beside `sdl.hdll`. A package extracted manually into `include/angle-nmb` is accepted after the same metadata validation.

At runtime, call `sdl.Sdl.configureGLProvider(GLContextProvider.Angle, backend)` before `sdl.Sdl.init()`. Explicit ANGLE selection fails if initialization is unavailable; it does not fall back to system OpenGL or Direct3D.
