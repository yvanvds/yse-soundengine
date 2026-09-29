<p align="center">
  <img src="logo/yse-logo.svg" alt="libYSE" width="520">
</p>

# libYSE 2.4

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

Beyond sample playback, libYSE hosts a polyphonic **synth subsystem**: a
virtual-analog / wavetable voice, a **DX7-class 6-operator FM voice** (with a
DX7 SysEx bank importer), and an **SFZ sampler voice**. The instrument DSP is
always compiled in — there is no feature switch to enable it. The *factory
sounds* those voices load (SFZ instruments and samples, wavetables, DX7/FM
`.SYX` banks) ship separately as an opt-in download — see
[Content pack](#content-pack-optional-instrument-assets).

**Multi-core rendering.** The mix is rendered as a task graph that the audio
thread works through together with a pool of render workers, so channels and
large groups of voices spread over the CPU. By default the engine starts one
worker per physical core minus one (capped at 8), counting only the cores the
process may run on, and places workers on performance cores first on hybrid
CPUs (Intel P/E, AMD Zen 5c, ARM big.LITTLE); in real-time rendering,
small scenes that cost less than waking a worker are rendered by the audio
thread alone. Set the count
before `init()` with `YSE::System().renderThreads(n)` — or
`yse_system_set_render_threads(sys, n)` from the C API — where `-1` is auto,
`0` renders serially on the audio thread (useful on constrained hardware such
as Android), and `n` is exactly `n` workers. The setting decides only which
thread renders each part of the mix, never how it is summed.

**Also in the box:**

- **Patcher** — a Max/Pd-style graph library with 305 objects (control and
  audio rate), subpatchers, data stores and the `.dict` / `.array` value
  types, MIDI and GUI-control families, loaded from and saved to JSON.
- **Domain clocks and clips** — tempo-relative clocks that drive the
  patcher's timing objects, and clips — looping note sequences in beats,
  played by a per-block transport on a bound clock.
- **Named bus** — a global publish/subscribe bus; patcher slots are
  addressed as `patcher.<name>.<slot>`.
- **Python live coding** — an optional embedded CPython interpreter for the
  live-coding DSL (`YSE_ENABLE_PYTHON`, desktop only).
- **Offline rendering** — `System().initOffline()` + `renderOffline(blocks)`
  run the engine without an audio device.
- **Benchmark suite** — google-benchmark micro and macro benchmarks under
  `Bench/` (see [Bench/README.md](Bench/README.md)).

**What's new in 3.0.** 3.0 changes the C ABI and several defaults (48 kHz
sample rate, `init()` failing without a device, patcher bus prefixes, …).
Read [Upgrading from 2.4 to 3.0](https://yvanvds.github.io/yse-soundengine/upgrading.html)
before you update; the release notes draft is
[docs/release-notes/v3.0.md](docs/release-notes/v3.0.md).

**What is YSE trying to be?** Neither a game-audio engine nor a DAW: an
authored signal graph played by spatial and physical controllers, built
for experimental electronic music and live performance. The full
orientation lives in [docs/project_vision.md](docs/project_vision.md).

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
| `YSE_BUILD_TOOLS` | `OFF` | Build developer tools (`dump_patcher_meta`); turned on by `python yse.py dump-patcher-meta` |
| `YSE_ENABLE_PYTHON` | `OFF` | Embed CPython for the live-coding DSL (desktop only; fails on Android) |
| `YSE_ENABLE_MIDI_DEVICE` | `ON` (desktop) | RtMidi-backed MIDI device backend |
| `YSE_ENABLE_COVERAGE` | `OFF` | gcov/gcovr coverage instrumentation (Linux, GCC or Clang); implies `YSE_BUILD_TESTS` |
| `YSE_LLVM_COVERAGE` | `OFF` | LLVM source-based coverage (Windows/Clang); implies `YSE_BUILD_TESTS` |
| `YSE_FETCH_CONTENT_PACK` | `OFF` | Download the optional instrument [content pack](#content-pack-optional-instrument-assets) (SFZ instruments, wavetables, DX7/FM banks) |
| `YSE_INSTALL_CONTENT_PACK` | `OFF` | Install the content pack under `<prefix>/share/yse/content` |
| `YSE_CONTENT_PACK_DIR` | `content/` | Path where the content pack lives / is assembled |

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

`libyse.so` lands in `build/bin/`. The demos in `Demo.Windows.Native/` are
Windows-only and are not built on Linux; exercise a Linux build through the
test suite (`python yse.py test`) or your own host program.

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
adb shell am start -n net.attrx.yse.tests/android.app.NativeActivity
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
python yse.py build --python     # debug build with the embedded-Python live-coding feature (desktop only)
python yse.py build --content-pack  # debug build + fetch the optional SFZ/DX7/FM content pack
python yse.py test               # build tests-debug preset, run ctest (includes the integration suite)
python yse.py test --python      # tests-debug-python preset — also runs the embedded-interpreter suite
python yse.py test --sanitizer asan  # tests-asan (Linux) / tests-asan-windows preset
python yse.py test --sanitizer tsan  # tests-tsan preset (Linux/clang only)
python yse.py bench              # bench preset (Release) + run yse_benchmarks; --filter <regex>, --json
python yse.py coverage           # coverage build + report (Linux: gcovr; Windows: llvm-cov)
python yse.py run                # run Demo00 from build-debug/bin/ (Windows demos)
python yse.py run Demo05         # run a specific demo
python yse.py debug Demo00       # launch under lldb
python yse.py clean              # remove build directories (asks first; --yes skips the prompt)
python yse.py analyze [path]     # run clang-tidy; path narrows scope (default: full tree)
python yse.py format             # clang-format on YseEngine/ and Tests/
python yse.py dump-patcher-meta  # regenerate the patcher object metadata the docs render
python yse.py package            # build a release archive in dist/ (used by CI)
python yse.py release patch      # bump version, commit, tag, push (maintainers; --dry-run, --no-push)
```

Running `yse_tests` directly without a `--test-suite=` / `--test-case=`
filter is refused; CTest (via `python yse.py test`) is the test entry point.

On Unix you can also `chmod +x yse.py` and use `./yse.py <command>`.
Pass `--help` to any subcommand for full usage.

The script is a thin wrapper over `cmake --preset` / `ctest --preset` calls.
`CMakePresets.json` at the repo root defines every named configuration; IDEs
with CMake Tools support (VS Code, CLion, Visual Studio) discover it
automatically without any extra setup.

Direct `cmake -B build ...` invocations remain fully valid — the presets are
additive and do not change how the build works when invoked directly.

---

## Content pack (optional instrument assets)

libYSE's instrument voices — the virtual-analog / wavetable voice, the
DX7-class 6-operator **FM voice** (plus its DX7 SysEx bank importer), and the
**SFZ sampler voice** — are **always compiled** into `libyse`. There is no
`YSE_ENABLE_FM` / `YSE_ENABLE_SFZ` compile switch; the instrument DSP is always
present.

What *is* optional is the **content pack**: the factory sounds those voices
load at runtime — SFZ instruments and samples, single-cycle wavetables, and
DX7/FM `.SYX` banks. None of it is baked into the binary (`libyse` links no
asset); it is plain data read by the normal file loaders. A small CC0 seed is
committed under `content/`; the larger third-party collections and the DX7
factory banks are pulled only on demand by two opt-in CMake options (both
`OFF` by default):

| Option | Effect |
|---|---|
| `YSE_FETCH_CONTENT_PACK` | Download the third-party pack sources into `content/`, verifying each against its SHA-256 pin when one is set (an unpinned source downloads unverified with a warning; pins are optional — never invented) |
| `YSE_INSTALL_CONTENT_PACK` | Install the assembled pack to `<prefix>/share/yse/content` |

Enable them through the Python workflow (no need to drop to raw `cmake`):

```sh
python yse.py build --content-pack                        # debug build + fetch the pack
python yse.py build --release --content-pack              # release build + fetch
python yse.py build --content-pack --install-content-pack # also install it
```

`--install-content-pack` implies `--content-pack`. The equivalent direct
invocation is `cmake -B build -G Ninja -DYSE_FETCH_CONTENT_PACK=ON`.

**DX7 factory-bank licensing caveat.** The DX7 factory-style ROM voice banks
are **tolerated, but not legally cleared** — Yamaha has never released these
voice data sets under an open license. They are fetched only when
`YSE_FETCH_CONTENT_PACK` is ON, land in `content/fm/dx7-factory/`, and every
consumer treats that folder as optional (deleting it is a clean opt-out). For
an unambiguously CC0 FM bank authored by this project, use
`content/fm/original/yse_originals.syx`. Full provenance and licenses for every
asset are in [CONTENT-LICENSES.md](CONTENT-LICENSES.md).

---

## Documentation

The published site is <https://yvanvds.github.io/yse-soundengine/>. Good
starting points:

- [Threading model](https://yvanvds.github.io/yse-soundengine/intro/threading.html)
  — which thread calls what, and the audio-thread rules
- [Patcher guide](https://yvanvds.github.io/yse-soundengine/patcher/index.html)
  — messages, graphs, subpatchers, host I/O, time, data, and the per-category
  object reference
- [Upgrading from 2.4 to 3.0](https://yvanvds.github.io/yse-soundengine/upgrading.html)

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
`dev` and `master` and on pull requests to `dev` that touch the docs or
the engine sources, and fails on any Doxygen or Sphinx warning. Only a
push to `master` publishes the result to GitHub Pages. The workflow assumes
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
| `Yse.Windows.Native/` | Legacy Visual Studio project for the library (not used by the CMake build) |
| `TestResources/` | Audio files referenced by demos |
| `content/` | Content pack: the committed CC0 seed plus any fetched instrument assets |
| `cmake/` | CMake modules (`YseContentPack.cmake`, `YsePython.cmake`) and the demo template |
| `documentation/` | Doxygen + Sphinx + Breathe documentation sources |
| `docs/` | Project vision, design notes (`docs/design/`) and release notes (`docs/release-notes/`) |
| `tools/ci-linux/` | Docker images for local Linux CI reproduction (`Dockerfile`, `Dockerfile.audio`, `Dockerfile.sanitizers`) |
| `tools/dump_patcher_metadata/` | `dump_patcher_meta` tool behind `python yse.py dump-patcher-meta` |
| `tools/lsan/` | LeakSanitizer suppressions for the embedded CPython interpreter |
| `logo/` | Project logo |
| `dependencies/` | Vendored sources: doctest, rtmidi, portaudio, libsndfile (read-only) |

A deeper architectural reference lives in [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md).

---

## Known issues

Tracked in [GitHub Issues](https://github.com/yvanvds/yse-soundengine/issues).

---

## License

libYSE is distributed under the [MIT License](LICENSE.md).
