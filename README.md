# libYSE 2.0

libYSE is a cross-platform sound engine written in C++. Version 2.0 removes the
JUCE dependency; PortAudio and libsndfile are the only audio backends. Windows,
Linux, and Android are supported. The CMake build described here covers Windows
(MSYS2 Clang) and Linux (system Clang or GCC). Android is out of scope for this
build system step.

---

## Building on Windows (MSYS2 Clang64)

### Prerequisites

Open an **MSYS2 CLANG64** shell and install the required packages:

```sh
pacman -S --needed \
  mingw-w64-clang-x86_64-cmake \
  mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-clang \
  mingw-w64-clang-x86_64-portaudio \
  mingw-w64-clang-x86_64-libsndfile \
  mingw-w64-clang-x86_64-rtmidi
```

`rtmidi` is required on Windows because the MIDI device source files link
against it. If you skip it, `cmake` will fail with a clear error. See
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) for background.

### Configure and build

```sh
cd /path/to/yse-soundengine
cmake -B build -G Ninja
cmake --build build
```

The shared library and demo executables are placed in `build/bin/`.

### Run a demo

Demos use hard-coded relative paths (`../../TestResources/...`) so they
**must be run from the `build/bin/` directory**:

```sh
cd build/bin
./Demo00.exe          # Play a sound
./Demo05.exe          # Reverb
# … etc.
```

### Build types

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### Optional flags

| CMake option | Default | Description |
|---|---|---|
| `YSE_ENABLE_LTO` | `OFF` | Link-time optimisation for Release builds |
| `YSE_NATIVE_ARCH` | `OFF` | Add `-march=native` (local builds only — not for distributable binaries) |

---

## Building on Linux

### Prerequisites (Debian/Ubuntu)

```sh
sudo apt install \
  cmake ninja-build clang \
  libportaudio-dev libsndfile1-dev
# rtmidi is optional on Linux (MIDI device code is Windows-only)
# sudo apt install librtmidi-dev
```

### Prerequisites (Fedora/RHEL)

```sh
sudo dnf install cmake ninja-build clang portaudio-devel libsndfile-devel
```

### Configure and build

```sh
cmake -B build -G Ninja
cmake --build build
```

### Run a demo

```sh
cd build/bin
./Demo00          # Play a sound
```

The `$ORIGIN` rpath is embedded in each demo binary so that `libyse.so` is
found automatically from the same directory.

---

## Project structure

| Directory | Contents |
|---|---|
| `YseEngine/` | Engine source (compiled into `libyse`) |
| `Demo.Windows.Native/` | Native C++ demos (one executable each) |
| `TestResources/` | Audio files referenced by demos |
| `dependencies/` | Vendored headers (rtmidi headers used by build; PortAudio/libsndfile vendored copies unused) |

---

## Known issues

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
