<p align="center">
  <img src="logo/yse-logo.svg" alt="libYSE" width="520">
</p>

# libYSE 2.1

libYSE is a cross-platform sound engine written in C++. PortAudio handles audio
I/O on desktop, Oboe (AAudio + OpenSL ES fallback) on Android, and libsndfile
decodes sample files everywhere. Windows, Linux, and Android are all supported
by the CMake build:

- **Windows** — MSYS2 Clang64 (primary) or MSVC
- **Linux** — system Clang or GCC (Ubuntu/Debian, Fedora/RHEL)
- **Android** — NDK r27+, API 26+, arm64-v8a and x86_64 (built via Gradle in `Tests/Android/`)

A flat `extern "C"` ABI (`yse_c/yse_*.h`) is folded into the same shared library
so language bindings (Dart FFI, Python ctypes, …) can call it without C++ ABI
compatibility — enabled by default via the `YSE_BUILD_C_API` option.

[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=bugs)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=reliability_rating)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)
[![Vulnerabilities](https://sonarcloud.io/api/project_badges/measure?project=yvanvds_yse-soundengine&metric=vulnerabilities)](https://sonarcloud.io/summary/new_code?id=yvanvds_yse-soundengine)


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

`rtmidi` is required when the MIDI device backend is enabled (the default on
desktop). To build without it, configure with `-DYSE_ENABLE_MIDI_DEVICE=OFF` —
the rest of the engine (MIDI file playback, music primitives, patcher) is
unaffected and still builds.

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
| `YSE_BUILD_TESTS` | `OFF` | Build the `Tests/` doctest suite and enable CTest |
| `YSE_BUILD_BENCHMARKS` | `OFF` | Build the `Bench/` google-benchmark suite (fetched on demand) |
| `YSE_BUILD_C_API` | `ON` | Fold the `extern "C"` ABI bridge into `libyse` |
| `YSE_ENABLE_MIDI_DEVICE` | `ON` (desktop) | RtMidi-backed MIDI device backend |

---

## Building on Linux

### Prerequisites (Debian/Ubuntu)

```sh
sudo apt install \
  cmake ninja-build clang \
  libportaudio-dev libsndfile1-dev librtmidi-dev
```

### Prerequisites (Fedora/RHEL)

```sh
sudo dnf install \
  cmake ninja-build clang \
  portaudio-devel libsndfile-devel rtmidi-devel
```

`librtmidi` (the ALSA-backed MIDI device library) is required when the MIDI
device backend is enabled (the default on Linux). Configure with
`-DYSE_ENABLE_MIDI_DEVICE=OFF` to build without it.

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

## Building on Android

The Android build is wired through Gradle in `Tests/Android/`. It produces a
NativeActivity APK that ships `libyse_tests.so` (the full test suite) for two
ABIs — `arm64-v8a` and `x86_64`. The release workflow at
`.github/workflows/release.yml` builds production multi-ABI archives the same
way and publishes them as release assets.

### Prerequisites

- NDK r27+ installed (Android Studio's SDK Manager → NDK (Side by side))
- Gradle 8+ (the wrapper in `Tests/Android/gradlew` will use it automatically)
- An attached device or emulator at API 26+

### Build and run on-device

```sh
cd Tests/Android
./gradlew installDebug
adb shell am start -n net.attrx.yse.tests/.MainActivity
adb logcat -s yse_tests
```

The engine fetches libsndfile 1.2.2 and Oboe 1.9.3 from source via
`FetchContent` on the first configure — no system packages are required. RtMidi
is unused on Android (the MIDI device source files compile to empty TUs).

---

## Development workflow

If you have Python 3.8+ available, `yse.py` at the repo root provides a
Flutter-style CLI for the common tasks.  On Windows run it via:

```sh
python yse.py build              # configure + debug build (default)
python yse.py build --release    # release build
python yse.py test               # build tests-debug preset, run ctest
python yse.py coverage           # coverage build + gcovr report (Linux only)
python yse.py run                # run Demo00 from build-debug/bin/
python yse.py run Demo05         # run a specific demo
python yse.py debug Demo00       # launch under lldb
python yse.py clean              # remove all build directories
python yse.py analyze [path]     # run clang-tidy; path narrows scope (default: full tree)
python yse.py format             # clang-format on YseEngine/ and Tests/
```

On Unix you can also `chmod +x yse.py` and use `./yse.py <command>`.
Pass `--help` to any subcommand for full usage.

The script is a thin wrapper over `cmake --preset` / `ctest --preset` calls.
`CMakePresets.json` at the repo root defines every named configuration; IDEs
with CMake Tools support (VS Code, CLion, Visual Studio) discover it
automatically without any extra setup.

Direct `cmake -B build ...` invocations remain fully valid — the presets are
additive and do not change how the build works when invoked directly.

---

## Documentation

API reference is generated from the source by **Doxygen + Sphinx + Breathe**
using the `sphinx-book-theme`. Sources live under `documentation/`.

### Local preview

Install the toolchain (Doxygen 1.9+ from your package manager, Python deps
from the requirements file):

```sh
# Debian/Ubuntu
sudo apt install doxygen graphviz
# macOS (Homebrew)
brew install doxygen graphviz
```

On Windows, install the official binaries (both add themselves to `PATH`):

- Doxygen — <https://www.doxygen.nl/download.html> (the `doxygen-x.x.x-setup.exe` installer)
- Graphviz — <https://graphviz.org/download/> (the Windows installer; tick *"Add Graphviz to the system PATH"*)

Then install the Python dependencies (works on every OS):

```sh
pip install -r documentation/requirements.txt
```

Build the site:

```sh
cd documentation
make html        # Linux/macOS/MSYS2: doxygen XML + sphinx HTML
make.bat html    # Windows cmd
```

The HTML lands in `documentation/build/html/`. Preview it with the
built-in server:

```sh
make serve       # http://localhost:8000
```

You can run the two stages separately while iterating: `make doxygen`
regenerates the XML under `source/_doxygen/` (only needed when source
comments change), and `make sphinx` rebuilds just the HTML (fast). Use
`make clean` to wipe `build/` and `source/_doxygen/`.

### CI

`.github/workflows/documentation.yml` builds the docs on every push to
`master` and publishes the result to GitHub Pages. The workflow assumes
Pages is configured for the repo with **Source: GitHub Actions**
(Settings → Pages).

---

## Project structure

| Directory | Contents |
|---|---|
| `YseEngine/` | Engine source (compiled into `libyse`); `c_api/` holds the flat C ABI bridge |
| `Tests/` | doctest unit suite (`YSE_BUILD_TESTS=ON`); `Tests/Android/` packages it as a NativeActivity APK |
| `Bench/` | google-benchmark suite (`YSE_BUILD_BENCHMARKS=ON`); CI pushes results to the `bench-history` orphan branch |
| `Demo.Windows.Native/` | Native C++ demos (one executable each) — Windows only |
| `TestResources/` | Audio files referenced by demos |
| `documentation/` | Doxygen + Sphinx + Breathe documentation sources |
| `tools/ci-linux/` | Docker images for local Linux CI reproduction (`Dockerfile`, `Dockerfile.audio`) |
| `dependencies/` | Vendored headers (rtmidi, doctest); PortAudio/libsndfile system packages on desktop |

A deeper architectural reference lives in [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md).

---

## Known issues

Tracked in [GitHub Issues](https://github.com/yvanvds/yse-soundengine/issues).

---

## License

libYSE is distributed under the [Eclipse Public License, v 2.0](LICENSE.md).
