<!-- META
last_updated_commit: 18f1bf45
last_updated_at: 2026-09-29
-->

# YSE Sound Engine — Project Overview

**Version:** 2.4.0 on `dev`; the next release is 3.0 (single source of truth: [`YseEngine/system.hpp`](YseEngine/system.hpp); upgrade guide: [documentation/source/upgrading.rst](documentation/source/upgrading.rst), release notes draft: [docs/release-notes/v3.0.md](docs/release-notes/v3.0.md))
**Language:** C++17
**Platforms:** Windows (MSYS2/Clang64, MSVC), Linux (gcc/clang), Android (NDK r27+, API 26+, arm64-v8a + x86_64)
**Build:** CMake 3.20+ via `CMakePresets.json`; Android wraps the same CMake invocation through Gradle in `Tests/Android/`
**Key Dependencies:** PortAudio (desktop audio I/O), Oboe (Android audio I/O), libsndfile (file loading), RtMidi (MIDI device I/O — desktop only, gated by `YSE_ENABLE_MIDI_DEVICE`), pthreads, nlohmann/json (vendored, patcher serialisation), pybind11 (fetched, only with `YSE_ENABLE_PYTHON`), doctest + google-benchmark (vendored / fetched for tests + benches). Full table: [Vendored / Fetched Dependencies](#vendored--fetched-dependencies)
**Vision:** what YSE is trying to be — and the design-review stance every scoping decision assumes — lives in [`docs/project_vision.md`](docs/project_vision.md)

---

## Repository Layout

The top-level directory table lives in [README.md](README.md#project-structure); this is the finer-grained view.

`
YseEngine/                       # Core C++ sound engine — compiled to libyse (SHARED)
  c_api/                         # extern "C" ABI bridge (yse_*) folded into libyse for FFI bindings
    include/yse_c/               # Public C headers (yse_all.h aggregates all subsystems)
  python/                        # Embedded-CPython live-coding runtime + pybind11 yse module (YSE_ENABLE_PYTHON)
Tests/                           # doctest suite (~7000 TEST_CASEs across ~318 .cpp files) — gated by YSE_BUILD_TESTS
  Android/                       # Gradle wrapper that packages libyse_tests.so into a NativeActivity APK
  support/                       # audio_helpers, null_device, alloc_probe, android_asset_bridge, fixtures
  TEST_PLAN.md                   # Historical 2.0 phased roadmap + how the suite runs today
Bench/                           # google-benchmark suite — gated by YSE_BUILD_BENCHMARKS (see Bench/README.md)
Demo.Windows.Native/             # 22 C++ console demos (Demo00–Demo21 + Test01_Pitch + combined Demo)
Yse.Windows.Native/              # Legacy Visual Studio project for the library (not used by the CMake build)
documentation/                   # Doxygen + Sphinx + Breathe (sphinx-book-theme); published to GitHub Pages
  source/intro/                  # install, hello_sound, mental_model, sessions_and_devices, threading
  source/tutorials/              # 16 tutorial pages (sounds, 3D, channels, reverb, synths, mixing, patcher, clocks/clips)
  source/patcher/                # Patcher guide (14 pages) + objects/ (per-category reference rendered from metadata)
  source/live_coding/            # Live-coding DSL page
  source/api/                    # Breathe-driven API reference (16 grouped pages + index)
  source/upgrading.rst           # 2.4 → 3.0 upgrade guide
  source/_data/                  # Committed data snapshots consumed by Sphinx hooks (patcher_objects.json)
  source/_templates/             # Jinja templates rendered by the pre-build hook in conf.py
docs/                            # project_vision.md, design records (design/), release notes (release-notes/)
content/                         # Instrument content pack: committed CC0 seed + fetched assets (pack-manifest.cmake)
cmake/                           # YseContentPack.cmake, YsePython.cmake, demo_main.cpp.in
tools/ci-linux/                  # Docker images for local Linux CI reproduction (Dockerfile, .audio, .sanitizers)
tools/dump_patcher_metadata/     # dump_patcher_meta — emits the patcher_objects.json snapshot used by the docs hook
tools/lsan/                      # LeakSanitizer suppressions for the embedded CPython interpreter
TestResources/                   # Audio files used by demos (drone.ogg, kick.ogg, demo.mid, …)
dependencies/                    # Vendored, read-only: doctest/, portaudio/, rtmidi/, libsndfile{,64}/ (legacy VS build)
logo/                            # SVG artwork (yse-logo.svg, yse-icon.svg)
dist/                            # Release archives written by yse.py package (gitignored)
CMakeLists.txt                   # Root build
CMakePresets.json                # Named configure/build/test presets (see Build System)
yse.py                           # Python CLI wrapper over cmake --preset / ctest --preset
.clang-tidy                      # Opt-in baseline; analyze-before-commit workflow via yse.py analyze
.clang-format                    # Repo style: 2-space indent, K&R/attach braces, 100-col (used by yse.py format)
sonar-project.properties         # SonarCloud analysis configuration
.github/workflows/               # build, format, release, benchmark, documentation (see CI / Distribution)
`

The old .NET/Xamarin wrappers, the WPF demo, the UWP build, and the JUCE backend have all been removed.

---

## Build System

### Recommended entry point: `CMakePresets.json` + `yse.py`

`CMakePresets.json` at the repo root defines every named build configuration. IDEs with CMake Tools support (VS Code, CLion, Visual Studio) auto-discover the presets. `yse.py` wraps them for terminal use:

| Configure preset | Binary dir | Purpose |
|--------|-----------|---------|
| `debug` | `build-debug/` | Library + demos, no tests |
| `release` | `build/` | Optimized release |
| `tests-debug` | `build-tests/` | Debug + `YSE_BUILD_TESTS=ON` |
| `debug-python` / `release-python` | `build-debug-python/` / `build-python/` | `debug` / `release` + `YSE_ENABLE_PYTHON=ON` (desktop only) |
| `tests-debug-python` | `build-tests-python/` | `tests-debug` + `YSE_ENABLE_PYTHON=ON` — runs the embedded-interpreter suite |
| `tests-debug-python-asan` | `build-tests-python-asan/` | Python tests under clang ASan/LSan (its test preset runs label `python`) |
| `tests-asan` | `build-tests-asan/` | clang ASan (Linux); its test preset runs `yse_tests_patcher` + `yse_tests_sendstress` |
| `tests-asan-windows` | `build-tests-asan-windows/` | clang ASan on Windows (adds `-fsized-deallocation`) |
| `tests-tsan` | `build-tests-tsan/` | clang TSan (Linux/clang); its test preset skips `yse_unit_tests` + `yse_tests_contentpack`, the `tests-tsan-full` test preset runs everything |
| `bench` | `build-bench/` | Release + `YSE_BUILD_BENCHMARKS=ON` |
| `coverage` | `build-coverage/` | Linux only — gcc/clang `--coverage` instrumentation |
| `coverage-windows` | `build-coverage/` | Windows/Clang only — LLVM source-based coverage |

Every configure preset has a matching build preset; test presets exist for the `tests-*` and `coverage*` configurations, plus `tests-tsan-full`.

```sh
python yse.py build                # cmake --preset debug + build
python yse.py build --release      # release variant
python yse.py build --python       # debug variant with embedded-Python live-coding (YSE_ENABLE_PYTHON=ON, desktop only)
python yse.py build --content-pack  # also fetch the optional SFZ/DX7/FM content pack (YSE_FETCH_CONTENT_PACK=ON);
                                   # --install-content-pack also installs it (YSE_INSTALL_CONTENT_PACK=ON)
python yse.py test                 # tests-debug preset + ctest (includes the integration suite)
python yse.py test --python        # tests-debug-python preset — also runs the embedded-interpreter suite
python yse.py test --sanitizer asan  # tests-asan (Linux) / tests-asan-windows preset + ctest
python yse.py test --sanitizer tsan  # tests-tsan preset + ctest (Linux/clang only)
python yse.py bench                # bench preset + run yse_benchmarks; --filter <regex>, --json
python yse.py coverage             # coverage preset + report (gcovr → coverage.xml on Linux,
                                   # llvm-profdata+llvm-cov → coverage-llvm.json on Windows)
python yse.py run [Demo]           # run a demo from build-debug/bin/ (default: Demo00)
python yse.py debug Demo00         # launch a demo under lldb
python yse.py clean [--yes]        # remove all build dirs + coverage artefacts
python yse.py analyze [path]       # clang-tidy via compile_commands.json (sonar-scanner fallback)
python yse.py format               # clang-format -i over YseEngine/ and Tests/
python yse.py package              # release archive in dist/ (consumed by CI); --platform, --android-libs
python yse.py release patch        # bump VERSION (patch/minor/major), commit, tag, push; --dry-run, --no-push
python yse.py dump-patcher-meta    # regenerate documentation/source/_data/patcher_objects.json
```

Direct `cmake -B build ...` invocations remain fully valid — the presets are additive.

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `YSE_BUILD_TESTS` | OFF | Build the `Tests/` doctest suite; adds `yse_tests` target and enables CTest |
| `YSE_BUILD_BENCHMARKS` | OFF | Build the `Bench/` google-benchmark suite (fetched via FetchContent, pinned tag) |
| `YSE_BUILD_C_API` | **ON** | Fold the `extern "C"` ABI bridge (yse_*) into `libyse` so language bindings (Dart FFI, Python ctypes, …) can consume the DLL without C++ ABI compatibility |
| `YSE_BUILD_TOOLS` | OFF | Build developer tools under `tools/` (currently `dump_patcher_meta`). Enabled automatically by `python yse.py dump-patcher-meta` |
| `YSE_ENABLE_MIDI_DEVICE` | ON on desktop, OFF on Android | RtMidi-backed MIDI device backend. OFF compiles MIDI device source files out and skips the RtMidi configure-time dependency |
| `YSE_ENABLE_LTO` | OFF | Link-time optimization for Release builds |
| `YSE_NATIVE_ARCH` | OFF | `-march=native` (local dev only — not distributable) |
| `YSE_ENABLE_COVERAGE` | OFF | gcov/gcovr coverage; forces `YSE_BUILD_TESTS=ON`; Linux only (GCC/Clang) |
| `YSE_LLVM_COVERAGE` | OFF | LLVM source-based coverage (`-fprofile-instr-generate`); Clang only — mutually exclusive with `YSE_ENABLE_COVERAGE` |
| `YSE_ENABLE_PYTHON` | OFF | Embed a CPython interpreter for the live-coding DSL (desktop only; fatal error on Android). OFF is byte-for-byte equivalent to a build without the option. See **Python embedding** below |
| `YSE_FETCH_CONTENT_PACK` / `YSE_INSTALL_CONTENT_PACK` | OFF | Download / install the optional instrument content pack (`cmake/YseContentPack.cmake`, `content/pack-manifest.cmake`) — see [README.md](README.md#content-pack-optional-instrument-assets) |

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set unconditionally so `compile_commands.json` is always generated for clangd and SonarCloud.

### Python embedding (`YSE_ENABLE_PYTHON`)

Optional embedded CPython for the live-coding DSL (epic [#119](https://github.com/yvanvds/yse-soundengine/issues/119); this infrastructure is issue [#124](https://github.com/yvanvds/yse-soundengine/issues/124)). **Desktop only** — enabling it on Android is a configure-time `FATAL_ERROR`. **OFF by default**, and when OFF no Python headers, symbols, or runtime cost enter the build.

- **Build.** `cmake/YsePython.cmake` finds a system libpython (≥ 3.10, `Development.Embed`; on MSYS2 Clang64 the toolchain's own) and fetches pybind11 v2.13.6. Linkage and interpreter version follow the host.
- **Runtime.** `INTERNAL::ScriptRuntime` (`YseEngine/python/`) boots an isolated interpreter (no site-packages, no environment, no signal handlers) after the device opens and finalizes it in `system::close`. A dedicated script thread holds the GIL and exchanges eval requests/results with the engine over two lock-free SPSC queues; the `yse` module (`python/yse_module.cpp`) routes `send`/`on`/`schedule`/… through the global named bus. The TU must define `PYBIND11_SIMPLE_GIL_MANAGEMENT`.
- **Design record:** [docs/design/python_embedding.md](docs/design/python_embedding.md) (sourcing, isolation, lifecycle/threading, pybind11 module).
- **CI and presets.** Three presets enable it (`debug-python`, `release-python`, `tests-debug-python`, plus the `tests-debug-python-asan` variant), and `build.yml`'s `build-python` job builds `tests-debug-python` and `tests-debug-python-asan` on Linux/clang and runs `ctest -L python` (the `yse_tests_python` entry, registered only when the option is ON). The ASan leg filters CPython's own interpreter-lifetime state through `tools/lsan/python.supp`. The 50× init/finalize leak case is meaningful only in that isolated process (the suite owns the interpreter).

**Compiler flags (GCC/Clang):** `-Wall -Wextra -Wpedantic`, plus `-O3 -fno-math-errno` (Release). `-ffast-math` is **deliberately not used** — it breaks IEEE 754 semantics in ways that produce subtle DSP bugs (NaN propagation, denormal flushing). Linux/macOS builds use `CXX_VISIBILITY_PRESET hidden` + per-symbol `API` annotations — issue [#34](https://github.com/yvanvds/yse-soundengine/issues/34) was closed in commit `e080c16`.

### Platform dependencies

**MSYS2/Clang64 (Windows):**

```
pacman -S mingw-w64-clang-x86_64-portaudio \
          mingw-w64-clang-x86_64-libsndfile \
          mingw-w64-clang-x86_64-rtmidi
```

**Debian/Ubuntu (Linux):**

```
sudo apt install cmake ninja-build clang \
                 libportaudio-dev libsndfile1-dev librtmidi-dev
```

For `YSE_ENABLE_PYTHON=ON` add the Python embedding headers + libpython:
`mingw-w64-clang-x86_64-python` (MSYS2 Clang64) or `python3-dev` (Debian/Ubuntu).

**Android (NDK):** No pkg-config in the sysroot. libsndfile is fetched from source (1.2.2, WAV-only, no external codec libs), Oboe is fetched at tag 1.9.3, RtMidi is not used. Link options include `-Wl,-z,max-page-size=16384` for Android 15+ 16 KB page-size compatibility (Play Store requirement, Nov 2025+).

---

## CI / Distribution

Five GitHub Actions workflows under `.github/workflows/`. PR-event runs whose head is `dev` or `master` (release/sync PRs) are skipped where the push already ran the same SHAs.

| Workflow | Triggers | What it does |
|----------|----------|--------------|
| `build.yml` | push (master/dev), PR | `build`: Linux Debug + `YSE_ENABLE_COVERAGE=ON`, two full ctest passes (native + forced 48 kHz), gcovr, SonarCloud scan (`yvanvds_yse-soundengine`). `android-gate` + `build-android`: NDK cross-compile of `libyse.so` + `libyse_tests.so` (both ABIs on push/PR to master, x86_64 only on path-matched PRs to dev, #630). `build-python`: `tests-debug-python` and `tests-debug-python-asan` on Linux/clang, `ctest -L python`. `build-sanitizers`: ASan over the patcher suite (+ lifecycle/logsafety), TSan over every ctest entry bar `yse_unit_tests` and `yse_tests_contentpack` (#824), widened on a push to `dev` to the `tests-tsan-full` sweep |
| `format.yml` | push (master/dev), PR | `clang-format --dry-run --Werror` over `YseEngine/` + `Tests/`, pinned to clang-format 22.1.4 |
| `release.yml` | tag `v*`, manual | Builds Linux x64, Windows x64, and Android multi-ABI release archives → `dist/` → uploaded as GH release assets |
| `benchmark.yml` | push (master/dev), PR to master, manual | Runs google-benchmark; results pushed to the `bench-history` orphan branch; PR comments on regressions |
| `documentation.yml` | push to dev/master, PR to dev (docs/engine paths), manual | Doxygen 1.17.0 (pinned) + Sphinx `-W`; GitHub Pages deploy on master only |

Headless audio coverage on GHA is **not** feasible — `snd-aloop`, `PulseAudio null sink`, and JACK-dummy all fail for kernel-module / capability reasons on the Azure-hosted runner kernel. The Linux Docker image at `tools/ci-linux/Dockerfile.audio` reproduces the headless JACK-dummy environment locally (requires `--cap-add IPC_LOCK --ulimit memlock=-1 --shm-size=512m`).

`sonar-project.properties` declares:
- `sonar.projectKey=yvanvds_yse-soundengine`, `sonar.organization=yvanvds`
- `sonar.sources=YseEngine`, `sonar.tests=Tests`
- Exclusions: `dependencies/**`, vendored `YseEngine/dsp/fm/msfa/**` and `YseEngine/utils/json.hpp`, all `build*/`, legacy native projects
- `sonar.cfamily.compile-commands=build/compile_commands.json`
- `sonar.coverageReportPaths=coverage.xml`

---

## Sound Engine Architecture (`YseEngine/`)

The engine is compiled as a **shared library** (`libyse.dll` / `libyse.so`). Two public entry points:

- `yse.hpp` — full C++ API, single header.
- `yse_c/yse_all.h` — flat `extern "C"` ABI for language bindings (Dart FFI, Python ctypes, …).

### Build target structure

- **`yse_objects`** — `OBJECT` library containing every engine source file plus (if `YSE_BUILD_C_API=ON`) the `yse_*` C bridge. Compiled with `YSE_DLL_BUILD` and `POSITION_INDEPENDENT_CODE ON` so the same objects can link into either a SHARED library or a static test binary. Hidden default ELF visibility; public symbols marked with the `API` macro from `headers/defines.hpp`.
- **`yse`** — `SHARED` library that consumes `yse_objects` via `PRIVATE` linkage. Propagates `YSE_DLL` to consumers via `INTERFACE`, which switches `API` to `__declspec(dllimport)` on Windows.

The test executable / shared-lib (on Android, a `.so` loaded by NativeActivity) links `yse_objects` directly, bypassing the DLL export boundary so white-box tests can reach internal symbols.

### Subsystem Map

```
Application
    │
    ├── YSE::System()          global lifecycle, device, config
    ├── YSE::Listener()        3D reference point (singleton)
    │
    ├── sound                  playback objects  ──→ channel (mixer tree)
    ├── reverb                 positional room effects
    ├── player                 polyphonic note sequencer
    ├── patcher                modular DSP graph (Max/MSP-style)
    ├── MIDI                   file + (optionally) device I/O
    └── synth                  polyphonic instrument host (sine / VA / SFZ sampler / DX7 FM voices,
                               per-note 3D handlers) ──→ sound ──→ channel
```

---

### 1. Sound Objects

**Files:** [sound/soundInterface.hpp](YseEngine/sound/soundInterface.hpp), [sound/soundImplementation.cpp](YseEngine/sound/soundImplementation.cpp), [sound/soundManager.cpp](YseEngine/sound/soundManager.cpp)

Each public `YSE::sound` wraps a private `SOUND::implementationObject`. State changes are posted as messages to the audio thread — they are never applied directly.

**Creation variants:**

```cpp
sound.create(fileName, channel, loop, volume, streaming);
sound.create(DSP::buffer, channel, loop, volume);
sound.create(MULTICHANNELBUFFER, channel, loop, volume);
sound.create(dspSourceObject, channel, volume);   // procedural source
sound.create(patcher, channel, volume);           // modular synthesis
```

**Implementation state machine:** `OBJECT_CONSTRUCTED → OBJECT_CREATED → OBJECT_SETTING_UP → OBJECT_SETUP → OBJECT_READY → OBJECT_RELEASE → OBJECT_DELETE_PENDING → OBJECT_DELETE` with a documented use-after-free fence around the `OBJECT_DELETE_PENDING` handshake (see soundManager.cpp comments).

**Play intent states:** `SI_NONE / SI_PLAY / SI_STOP / SI_PAUSE / SI_TOGGLE / SI_RESTART`

**Per-sound properties:** position (Pos), size (rolloff distance), speed (negative → reverse playback), volume (with fade time), spread, relative, doppler, occlusion (0–1 gain duck: `finalGain *= 1 - occlusion`).

---

### 2. 3D Spatialization

**Files:** [listener.hpp](YseEngine/listener.hpp), [implementations/listenerImplementation.cpp](YseEngine/implementations/listenerImplementation.cpp)

`YSE::Listener()` is a singleton representing the listener's point of view. Per-frame pipeline for each active sound: inverse-distance attenuation, angle → stereo/surround pan, doppler shift from radial velocity, optional user-supplied occlusion callback → gain duck (`finalGain *= 1 - occlusion`).

**Occlusion hook:**

```cpp
system& occlusionCallback(float(*func)(const Pos& source, const Pos& listener));
```

The callback runs on the **control thread** (inside `System().update()`), once per occlusion-enabled sound per tick — never on the audio callback thread. The clamped result reaches the audio thread over the sound message queue, so a raycast may lock or allocate (#209). C ABI: `yse_system_set_occlusion_callback(sys, cb, user_data)` (#906), a static trampoline over an atomically swapped `(cb, user_data)` pair.

**Output channel configurations:** `CT_MONO CT_STEREO CT_QUAD CT_51 CT_51SIDE CT_61 CT_71 CT_CUSTOM`. The `.1` layouts follow the platform-standard channel order (`FL FR FC LFE …`) with the LFE at index 3; the LFE output is flagged and excluded from azimuth panning, so positional sounds are never panned into the subwoofer ([#203](https://github.com/yvanvds/yse-soundengine/issues/203)).

---

### 3. Channel System (Hierarchical Mixer)

**Files:** [channel/channelInterface.hpp](YseEngine/channel/channelInterface.hpp), [channel/channelImplementation.cpp](YseEngine/channel/channelImplementation.cpp), [channel/channelManager.cpp](YseEngine/channel/channelManager.cpp)

Channels form a **tree** rooted at `MainMix`, with five pre-made leaves (`FX`, `Music`, `Ambient`, `Voice`, `GUI`) and arbitrary user-created children. Sounds can be reparented via `moveTo` at runtime. Sounds beyond a distance threshold are virtualized to save CPU.

Each channel also carries a pre-fader **insert** DSP chain (`setDSP`) and up to N **aux sends** to **return buses** (`makeReturn` / `send` / `setSendLevel` / `clearSend`), plus pre/post-fader peak metering (linear + dBFS, combined and per-output). See the signal-path diagram below for how inserts and sends sit in the mix. Send levels are ramped internally, so they are click-free to set every control tick.

**Render path — task graph (epic #856).** `deviceManager::renderOneBlock()` calls `CHANNEL::Manager().render(master)`, which runs one block of a task graph on [internal/renderScheduler.h](YseEngine/internal/renderScheduler.h). Each channel is up to `CHANNEL::MAX_SLICES` (16) `voiceSlice` leaves plus a `mixTask`; a return waits for every source and every lower-generation return, and the master's `mixTask` is the root. The graph is rebuilt in place on the audio thread only when the tree, the returns or a channel's slice count change (#859). Sounds are spread over slices by count, then by measured cost (#860, #861). A block lighter than the measured cost of waking a worker is rendered by the audio thread alone (serial gating, #861).

**Mixer bus order (behaviour change in #859).** A channel's mix task sums its voice slices and children *first*, then runs its insert chain, reverb, meters, send taps and fader; before #859 a bus insert processed only the bus's own sounds. Sends write into per-slot `tap` buffers that the return gathers in a fixed order, so the mix is bit-identical for any worker count (`rendergolden` suite). Full as-built description: [render_scheduler.md](docs/design/render_scheduler.md#as-built-render-path-voice-slices-cost-tracking-workers).

---

### 4. Reverb System

**Files:** [reverb/reverbInterface.hpp](YseEngine/reverb/reverbInterface.hpp), [reverb/reverbImplementation.cpp](YseEngine/reverb/reverbImplementation.cpp), [reverb/reverbManager.cpp](YseEngine/reverb/reverbManager.cpp), [internal/reverbDSP.cpp](YseEngine/internal/reverbDSP.cpp), [internal/underWaterEffect.cpp](YseEngine/internal/underWaterEffect.cpp)

One Freeverb-derived reverb processor; multiple `YSE::reverb` objects each represent a positioned zone. The engine blends them by proximity to the listener. A global reverb is available via `System().getGlobalReverb()`.

**Reverb properties:** position, size, rollOff, roomSize, damping, dryWetBalance, modulation.
**Built-in presets:** `REVERB_OFF, GENERIC, PADDED, ROOM, BATHROOM, STONEROOM, LARGEROOM, HALL, CAVE, SEWERPIPE, UNDERWATER`

The underwater treatment is an ordinary insert module (`dsp/modules/underWater.hpp`, issue #327). `system::underWaterFX(channel)` binds the engine's default instance to a channel's insert slot through the normal `setDSP` path, and `setUnderWaterDepth(...)` drives it at control rate (listener depth below the water plane) alongside a matching `REVERB_UNDERWATER` zone; user code can instantiate and drive the module directly instead.

---

### 5. DSP System

**Files:** [dsp/dspObject.hpp](YseEngine/dsp/dspObject.hpp), [dsp/buffer.hpp](YseEngine/dsp/buffer.hpp), [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), `dsp/modules/`

```cpp
class dspObject {          // filter / effect in a chain
  virtual void process(MULTICHANNELBUFFER&) = 0;
  void link(dspObject& next);   // splice `next` in after this node
  void unlink();                // clear the forward edge (#391) — lets a chain be reordered in place
  dspObject& bypass(Bool); dspObject& impact(Flt);
  dspObject& lfoType(LFO_TYPE); dspObject& lfoFrequency(Flt);
};

class dspSourceObject {    // audio generator (replaces file)
  virtual void process(SOUND_STATUS& intent) = 0;
  virtual void frequency(float value) = 0;
};
```

`DSP::buffer` — single-channel float buffer with arithmetic operators.
`MULTICHANNELBUFFER` — `std::vector<DSP::buffer>`; supports mono-to-surround spreading.

| Category | Modules |
|----------|---------|
| Oscillators | sine, cosine, saw, noise, VCF, wavetable |
| Filters | lowPass, highPass, bandPass, biQuad, sampleHold, sweep, phaser, ladderFilter (Moog-style ZDF ladder), raw filters |
| Envelopes | envelope, ADSRenvelope, ramp |
| Modulators | LFO, delay, basicDelay, highpassDelay, lowpassDelay |
| Math | clip, sqrt, rSqrt, wrap, midiToFreq, freqToMidi, dbToRms, rmsToDb |
| Spectral | Hilbert transformer, ring modulator |
| Granular | granulator |
| FM | difference (FM pair); 6-operator DX7-class engine under `dsp/fm/` (`fmVoice`, `fmPatch`, `dx7Sysex`, MSFA core) |
| Mix / channel-strip | parametric EQ, compressor, chorus/flanger, plate reverb (Dattorro), feedback delay — N-channel `dspObject` modules for channel inserts and return buses |
| Fourier | fft.cpp + mayer.cpp (real-input FFT) |

`dspObject` carries an N-channel processing contract (issue #158): `process(std::vector<DSP::buffer>&)` handles the full device layout, so inserts and return effects work at any output width.

[dsp/smoother.hpp](YseEngine/dsp/smoother.hpp) holds the shared one-pole smoother primitives (#614): a sample-rate-aware coefficient (`1 - exp(-1/(τ·fs))`, computed off the audio thread or at block rate) paired with an allocation-free per-sample `onePoleSmooth` step, so parameter smoothing behaves the same at every sample rate. The engine default sample rate is 48 kHz.

---

### 6. Audio Device Layer

**Files:** [device/deviceInterface.hpp](YseEngine/device/deviceInterface.hpp), [device/portaudioDeviceManager.cpp](YseEngine/device/portaudioDeviceManager.cpp), [device/oboeImplementation.cpp](YseEngine/device/oboeImplementation.cpp), [device/androidDeviceManager.cpp](YseEngine/device/androidDeviceManager.cpp)

Abstracts the OS audio API behind a single interface.

- **Desktop:** PortAudio handles Windows (WASAPI/DirectSound/WDM — ASIO is not available with the MSYS2 package; see [issue #38](https://github.com/yvanvds/yse-soundengine/issues/38)) and Linux (ALSA/JACK).
- **Android:** Oboe 1.9.3 negotiates AAudio on API 26+ (our minSdk) and falls back to OpenSL ES on rare devices where AAudio is unavailable. `androidDeviceManager.cpp` is the platform-specific entry point.

The engine monitors for missed callbacks and can auto-reconnect on device dropout (`system::autoReconnect(bool, int delayMs)`; a newly started stream gets a half-second start-up grace, #681). `cpuLoad()` is measured from the audio callback itself (#82).

---

### 7. File Loading & Streaming

**Files:** [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), [internal/abstractSoundFile.cpp](YseEngine/internal/abstractSoundFile.cpp), [internal/lsfSoundfile.cpp](YseEngine/internal/lsfSoundfile.cpp)

- **Buffered (default):** file loaded fully into memory by a slow-pool worker; shared across all sounds using the same path; auto-released when unused.
- **Streaming:** per-sound disk read; for large files where memory is the constraint.
- Custom file-reader callbacks (e.g., network streams) via [internal/customFileReader.cpp](YseEngine/internal/customFileReader.cpp).

libsndfile is the only file backend (`LIBSOUNDFILE_BACKEND` is defined on every platform). On Android, libsndfile is FetchContent-built (WAV only, no external codec libs).
---

### 8. Patcher (Modular DSP Graph)

**Files:** [patcher/patcher.hpp](YseEngine/patcher/patcher.hpp) (public `YSE::patcher`), [patcher/patcherImplementation.cpp](YseEngine/patcher/patcherImplementation.cpp), [patcher/pObject.h](YseEngine/patcher/pObject.h) (object base + the host/audio-thread contract), [patcher/graphState.h](YseEngine/patcher/graphState.h), [patcher/pRegistry.cpp](YseEngine/patcher/pRegistry.cpp); objects live in `patcher/{filters,generatorObjects,genericObjects,guiObjects,io,math,midi,time}/`.

A Max/MSP-style node graph for building synthesis and effect networks in code or from JSON. The registry holds **305 objects** (the maintained count is at the top of [documentation/source/patcher/index.rst](documentation/source/patcher/index.rst)): control objects carry a `.` prefix (`.metro`, `.coll`, `.dict`), signal objects a `~` prefix (`~sine`, `~lp`, `~dac`), and `patcher` is a subpatcher.

```cpp
YSE::patcher patch;
patch.create(1);                                     // one main output
YSE::pHandle* osc = patch.CreateObject("~sine", "440");
YSE::pHandle* dac = patch.CreateObject("~dac");
patch.Connect(osc, 0, dac, 0);
std::string json = patch.DumpJSON();                 // serialize
patch.ParseJSON(json);                               // restore
```

**Per-object behaviour is not documented here.** Each object's constructor declares its description, category, inlet/outlet roles and parameter schema with the `ADD_DESCRIPTION` / `ADD_CATEGORY` / `INLET_DOC` / `OUTLET_DOC` / `PARAM_DOC` macros ([patcher/pObject.h](YseEngine/patcher/pObject.h)). `tools/dump_patcher_metadata` (`python yse.py dump-patcher-meta`) writes that metadata to [documentation/source/_data/patcher_objects.json](documentation/source/_data/patcher_objects.json), and a `conf.py` hook renders it into the per-category object reference under `documentation/source/patcher/objects/` (objects that need the MIDI device backend carry a `requires_midi_device` flag, #870). [Tests/patcher/test_doc_coverage.cpp](Tests/patcher/test_doc_coverage.cpp) fails if a registered object lacks metadata; [Tests/patcher/test_c_api_metadata.cpp](Tests/patcher/test_c_api_metadata.cpp) asserts C API parity.

**Key mechanisms, in one line each** (design records in `docs/design/`, user-facing pages in [documentation/source/patcher/](documentation/source/patcher/)):

- **Wait-free edits** — structural edits build a new immutable `GraphState` and publish it with one atomic swap; retired graphs and objects are reclaimed on the background pool two blocks later (epic #189, [patcher_graphstate.md](docs/design/patcher_graphstate.md)). Live `SetParams` is deferred or rebuilds the object ([patcher_live_params.md](docs/design/patcher_live_params.md)).
- **Bus addressing** — every patcher has a name (`patcher::name()`); `.s`/`.r` and the named stores use `patcher.<name>.<slot>` on the global named bus, spelled only by `patcherImplementation::ScopedAddress()` (#893). Sounds, channels and synths use `sound.` / `channel.` / `synth.<name>.<prop>` ([named_bus_addressing.md](docs/design/named_bus_addressing.md)).
- **Time** — `.metro`, `.delay`, `.qlist`, `.seq` bind to [domain clocks](#12b-domain-clocks) through `PATCHER::clockBridge` and to `timerThread` through `timerBridge`, both wait-free on the audio callback ([patcher_time.md](docs/design/patcher_time.md)).
- **File I/O** — `read`/`write` messages go through `patcher/io/fileScheduler.h` (fixed slot table, disk work on the background pool, completion delivered at the top of `Calculate`) ([patcher_file_io.md](docs/design/patcher_file_io.md)).
- **Load/teardown and subpatchers** — `Loadbang` runs after `ParseJSON` publishes, `Teardown` before `Clear()`/`DeleteObject`; subpatchers are flat storage with a `Container()` annotation, and `~inlet`/`~outlet` forward buffer pointers ([patcher_lifecycle_subpatchers.md](docs/design/patcher_lifecycle_subpatchers.md)).
- **GUI value protocol** — `GetGuiValue()` / `GetGuiValueAt(i)` read state as string cells; state goes back in as an inlet-0 message, no setter (#551, [patcher_gui_protocol.md](docs/design/patcher_gui_protocol.md)).
- **Shared values** — `.value`, `.coll`, `.dict*`, `.array*` share storage by name through `patcher/namedStore.h`; cords carry the name, never the contents ([patcher_shared_values.md](docs/design/patcher_shared_values.md)).
- **MIDI input** — `midi/midiInHub.h` moves RtMidi input-thread messages to the patcher's audio thread through per-port lock-free SPSC queues (#529; see [§11](#11-midi)).

Patcher TUs share warning suppressions for `-Wno-unused-parameter` plus Clang-specific noise from the vendored `utils/json.hpp`.

---

### 9. C ABI Bridge (`YseEngine/c_api/`)

A flat `extern "C"` surface compiled into `libyse` so language bindings (Dart FFI, Python ctypes, …) can consume the DLL without C++ ABI compatibility. Source files are folded into `yse_objects` via `include(c_api/CMakeLists.txt)` — never `add_subdirectory()`'d, the goal is to extend the existing target.

```
c_api/include/yse_c/         # Public C headers — Dart's ffigen entry point
  yse_all.h                  # Aggregate header (includes the rest)
  yse_system.h yse_listener.h yse_channel.h yse_sound.h yse_reverb.h
  yse_device.h yse_dsp.h yse_dsp_modules.h yse_patcher.h yse_midi.h
  yse_clip.h yse_music.h yse_synth.h yse_instrument.h yse_python.h
  yse_bus.h yse_log.h yse_buffer_io.h yse_common.h yse_enums.h
c_api/                       # Wrappers (yse_*.cpp) and the internal helper header yse_c_internal.hpp
```

`yse_bus.h` (issue #389) is the host bus tap: `yse_bus_tap_create(prefix, cb, user_data)` subscribes the host to a bus-address prefix and delivers `(address, value)` frames on the thread that drives `yse_system_update()` — the outbound counterpart to the script-error callback, and the blocking dependency for the Phi live-coding control plane. Issue #904 adds the inbound side: `yse_bus_publish_bang/_int/_float/_string/_list(address, ...)` publish as a control-rate producer (the same `T_GUI` attribution as a script's `yse.send()`), and `yse_bus_subscribe(address, cb, user_data)` / `yse_bus_unsubscribe` register an exact-address subscription delivered like a tap. Every C header is wired into the Sphinx API reference and guarded against drift by [Tests/system/test_api_doc_coverage.cpp](Tests/system/test_api_doc_coverage.cpp) (issue #398).

Callback bridge conventions (atomic-swap callback pointer, no mutex, no malloc on audio-callback-reachable paths, `YSE_C_CALLBACK` on the typedef) are documented in `yse_c_internal.hpp` and enforced by the `c-api-extend` skill.

---

### 10. Threading & Concurrency Model

**Files:** [internal/threadPool.cpp](YseEngine/internal/threadPool.cpp), [internal/renderScheduler.cpp](YseEngine/internal/renderScheduler.cpp), [internal/cpuTopology.cpp](YseEngine/internal/cpuTopology.cpp), [internal/thread.cpp](YseEngine/internal/thread.cpp), [utils/lfQueue.hpp](YseEngine/utils/lfQueue.hpp), [utils/atomicOps.hpp](YseEngine/utils/atomicOps.hpp)

- **Audio callback thread** — managed by PortAudio/Oboe; runs DSP chain at buffer rate. FTZ/DAZ enabled per thread (issue [#81](https://github.com/yvanvds/yse-soundengine/issues/81)).
- **Application thread** — drives `system::update()` each frame; only flags for update, the audio thread drains.
- **Thread pool** — `threadPool` is the background pool only: `threadPoolThread` workers on a lock-free job ring with a timed backoff. The "slow pool" is single-threaded by construction (`slowThreads(1)`) so file loads + the manager setup/delete jobs are serialised.
- **Render scheduler** — `renderScheduler` (`Global().renderer()`) owns the raised-priority render workers and runs the channel task graph once per block with the audio thread as worker 0 (see [§3](#3-channel-system-hierarchical-mixer); epic #856). Worker count (#861): `System().renderThreads(n)` / `yse_system_set_render_threads()` — `-1` auto = physical cores − 1 inside the process affinity mask, capped at `MAX_AUTO_WORKERS` (8); `0` = the audio thread alone; `n` = exactly n (≤ 64). This replaced #650's two-worker stopgap. Workers are placed performance-cores-first by [internal/cpuTopology.h](YseEngine/internal/cpuTopology.h) (#862), spin 50 µs after a block and then park in the kernel (futex / `WaitOnAddress`, #858). Details: [render_scheduler.md](docs/design/render_scheduler.md#as-built-render-path-voice-slices-cost-tracking-workers).
- **Communication** — cross-thread state changes use a lock-free SPSC inbox (`utils/lfQueue.hpp`) between the main and audio threads. The audio thread never takes a mutex on the hot path.
- **Lifecycle fences** — `OBJECT_DELETE_PENDING` handshake prevents the slow-pool deleter from freeing an impl while the audio thread still has it in `toLoad`; `connectedToParent` atomic flag coordinates parent-channel disconnect.
- **Destructor error reporting** — a destructor's teardown is wrapped in `try { ... } catch (...) { ... }` and the handler **must** report through `INTERNAL::EmitNoThrow(code, "message")` ([implementations/logImplementation.h](YseEngine/implementations/logImplementation.h)), never `LogImpl().emit(...)`: `emit()` and a host `logHandler` can throw back out of the handler into `std::terminate()`, and `EmitNoThrow` also survives being called after the log itself is destroyed (#433). Coverage: `Tests/internal/test_log_nothrow.cpp` (`yse_tests_logsafety`).
- **Engine time** — `INTERNAL::time` ([internal/time.h](YseEngine/internal/time.h)) measures update ticks with `std::chrono::steady_clock` (monotonic wall time, RT-safe), replacing `std::clock()` process-CPU time (#667).
- **Atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases in `utils/atomicOps.hpp`).
- **Global named bus** ([internal/namedBus.h](YseEngine/internal/namedBus.h)) — `INTERNAL::Bus()` is the by-name addressing substrate underneath the live-coding DSL (epic #119). Main-thread publishes dispatch synchronously; audio-thread (`T_DSP`) publishes go through a pre-sized SPSC `lfQueue` drained from `system::update()`, allocation-free and lock-free, so only `int` and `float` payloads survive from `T_DSP` (strings and lists are dropped). Registration takes a `std::shared_mutex` and never runs on the audio thread; state lives from `System::init` to `System::close`. **Prefix taps** (#389) match an address prefix on the control thread and back the [`yse_bus.h`](#9-c-abi-bridge-yseenginec_api) host tap. Address grammar: [docs/design/named_bus_addressing.md](docs/design/named_bus_addressing.md), [docs/design/live_coding_dsl.md](docs/design/live_coding_dsl.md).
- **Log sink serialisation** ([implementations/logImplementation.h](YseEngine/implementations/logImplementation.h)) — `logMessage()` delivers each line under a mutex, so a host `logHandler` is never entered from two threads at once and needs no lock of its own, but must not log back from inside (#820). The lock is safe because nothing on the audio callback reaches this class. Coverage: `Tests/internal/test_log_sink_concurrency.cpp` (`yse_tests_logsafety`).
- **Real-time log queue** ([internal/rtLogQueue.h](YseEngine/internal/rtLogQueue.h)) — `INTERNAL::RtLog()` is the only way onto the log from a thread that may be the audio callback (#546). `post()` copies the line into a fixed record on a bounded lock-free `mpmcQueue` and never allocates, locks or blocks; `drain()`, once per `system::update()`, forwards the lines to `logImplementation::emit`. A push onto a full queue is refused and counted, and the next drain logs one overflow line. The patcher's `.print` is the first consumer.

---

### 11. MIDI

**Files:** `midi/` directory

- **File playback** — load and play standard MIDI files; always available.
- **Device I/O** — RtMidi-backed; gated by `YSE_ENABLE_MIDI_DEVICE` (ON by default on Windows/Linux desktop, OFF on Android/Mac). Issue [#35](https://github.com/yvanvds/yse-soundengine/issues/35) was closed in commit `899260b`.
- **Patcher integration** — `patcher/midi/` registers the patcher's MIDI objects (formatters/parsers such as `.midiformat` / `.midiparse`, senders such as `.xnoteout` / `.midiout`, receivers such as `.notein` / `.ctlin` / `.midiin` / `.sysexin`); the per-object reference is the `midi` page of the generated object reference.
- **MIDI input hub** — [midi/midiInHub.h](YseEngine/midi/midiInHub.h) (#529, device builds only) is `midiOutSender` in reverse: each RtMidi input thread copies incoming messages into per-port bounded lock-free SPSC queues, and the patcher's audio thread drains them in `Calculate()`. Neither side allocates or locks; a SysEx dump is queued whole or dropped and counted, never truncated (#950).
- **Shared headers** — [midi/midiSynthRouting.hpp](YseEngine/midi/midiSynthRouting.hpp) maps a raw channel-voice message onto the synth's normalized API (file playback and device input); [midi/midiBytes.hpp](YseEngine/midi/midiBytes.hpp) holds the standard-MIDI-file byte primitives (#698) shared by `midifileImplementation.cpp` and the patcher's `.seq`. The reads are pure and allocation-free; the `Append*` writers grow a `std::string`. The *parsers* above them stay deliberately separate — see [docs/design/patcher_file_io.md](docs/design/patcher_file_io.md).

---

### 12. Music / Composition

**Files:** `music/`, `player/`

Polyphonic note player with scale constraints, motif sequencing, and randomised pitch/velocity/gap ranges. Exposes `YSE::scale`, `YSE::motif`, `YSE::player`, `YSE::note`, `YSE::pNote`, `YSE::chord` as public objects.

---

### 12b. Domain Clocks

**Files:** [clock/domainClock.h](YseEngine/clock/domainClock.h) / [.cpp](YseEngine/clock/domainClock.cpp), [clock/clockManager.h](YseEngine/clock/clockManager.h) / [.cpp](YseEngine/clock/clockManager.cpp)

A set of named musical (beat) clocks derived from the single sample clock (issue #249). Each clock is a **beat accumulator**: `CLOCK::Manager().update(blockSeconds)` runs once per rendered 128-sample block and advances each clock by `blockSeconds × tempo / 60`, so polytemporal relationships stay exact. Tempo is rampable (`setTempo(name, bpm, rampSeconds)`), never clamped (0 pauses, negative runs backward). The manager follows the PLAYER lock-free lifecycle; the audio thread never allocates, locks or frees, and `beatPosition` / `currentTempo` are safe to poll from the UI.

Public surface: `YSE::system::createClock / destroyClock / clockExists / setTempo / beatPosition / currentTempo`, mirrored as `yse_system_*`. `lookup(name)` returns a `std::shared_ptr`, so `destroyClock` retires a clock and a holder still bound reads a frozen beat (#707). Clips bind to these clocks ([§12c](#12c-clip-transport)); the patcher binds through `PATCHER::clockBridge` ([§8](#8-patcher-modular-dsp-graph)). Details: [docs/design/clocks_and_clips.md](docs/design/clocks_and_clips.md).

---

### 12c. Clip Transport

**Files:** [clip/clip.hpp](YseEngine/clip/clip.hpp) (public `YSE::clip` + `YSE::clipEvent`), [clip/clipTransport.h](YseEngine/clip/clipTransport.h) / [.cpp](YseEngine/clip/clipTransport.cpp) (audio-thread timing impl), [clip/clipManager.h](YseEngine/clip/clipManager.h) / [.cpp](YseEngine/clip/clipManager.cpp), [clip/clipInterface.cpp](YseEngine/clip/clipInterface.cpp), [midi/midiOutSender.h](YseEngine/midi/midiOutSender.h) / [.cpp](YseEngine/midi/midiOutSender.cpp) (external MIDI-out sender thread)

A `YSE::clip` loops a flat, immutable list of beat-timed note events (`clipEvent`) against a bound [domain clock](#12b-domain-clocks), evaluated per audio block from the audio thread (issue #250): `CLIP::Manager().update()` runs right after `CLOCK::Manager().update()` and fires the events whose beats fall in the block's `(from, to]` window, so tempo changes bend a clip immediately. `setEvents` swaps a new list in at a block boundary without allocating, locking or freeing on the audio thread, and note-offs for notes that vanished from the new list still arrive. Output goes to `YSE::synth` instances or, with `YSE_ENABLE_MIDI_DEVICE`, to an RtMidi port through the `MIDI::outSender` thread (`clip::connect(midiOut&)`, #350). C ABI: `yse_clip_*`. Details: [docs/design/clocks_and_clips.md](docs/design/clocks_and_clips.md).

---

### 13. Synth (Polyphonic instrument host)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `synthManager.h/.cpp`, `synthImplementation.h/.cpp`, `synthMessage.h`, `dspVoice.hpp` (voice base), `positionHandler.hpp` + `positionHandlers.hpp/.cpp` (per-note 3D). Built-in voices: `sineVoice.hpp/.cpp` (reference sine + ADSR), `vaVoice.hpp/.cpp` (virtual-analog + wavetable → `DSP::ladderFilter` → amp/filter ADSR + LFO, live `vaParams` patch), `samplerVoice.hpp/.cpp` (SFZ sampler), and the FM voice under `dsp/fm/` (`fmVoice`, `fmPatch`, `dx7Sysex` importer, MSFA core in `dsp/fm/msfa/`).

The synth subsystem (epics [#145](https://github.com/yvanvds/yse-soundengine/issues/145)–[#149](https://github.com/yvanvds/yse-soundengine/issues/149)) is now public. A `YSE::synth` owns a pool of voices, note allocation, voice stealing and full keyboard state (pedals, controllers, pitch wheel, aftertouch); a `SYNTH::dspVoice` subclass owns only what one note sounds like. Build the pool with `create().addVoices(prototype, n)`, attach behind a positioned `YSE::sound` via `sound::create(synth&, …)`, then drive with `noteOn` / `noteOff`. Cloning happens off the audio thread on the setup pool (the synth becomes playable a moment after `addVoices`, like a file-backed sound). Voice `process()` / `clone()` follow the RT contract: allocate in the constructor / `clone()` (setup thread), never in `process()` (audio thread).

**Named-bus addressing (issue #388).** A named synth (`name()`, C `yse_synth_set_name`) registers `synth.<name>.note` / `.off` / controller addresses on the [global named bus](#10-threading--concurrency-model), reusing the RT-safe note message path — see [docs/design/named_bus_addressing.md](docs/design/named_bus_addressing.md). All four voice-group builders (`addVoices` and `yse_synth_add_voices_sine/_va/_fm/_sampler`, #390) carry a MIDI-channel + note-range filter, so one transport can drive a multitimbral or key-split rack.

**Per-note 3D positioning (Route 2, #169–#171).** Attach a `SYNTH::positionHandler` prototype with `synth::positionHandler(...)` to give every voice its own 3D position, updated per block — the "swarm". Ship-in handlers: `staticHandler`, `randomSpreadHandler`, `orbitHandler`. Steer the whole swarm from the control thread with `handlerParam(index, value)` (indices 0..2 = centre) or place a note imperatively with `notePosition(...)`; both are bounded, allocation-free messages.

Instruments load from portable formats off the audio thread: `samplerVoice::loadSFZ(path)` (SFZ region model in `dsp/sfzModel.hpp` / `dsp/sfzParser`), and `dx7SysEx::loadBank(path, bank)` → `fmVoice::setPatch(...)` (applied on the next note-on). The C API mirror lives in `c_api/include/yse_c/yse_synth.h` + `yse_instrument.h` (built-in voices / handlers only; custom C-side voices are deferred).

---

### Full Audio Signal Path

```
sound.play()
    └── DSP source (file buffer / dspSourceObject / patcher / synth voice pool)
         └── speed / pitch shifting
              └── user DSP chain (dspObject link list)
                   └── 3D attenuation + pan (distance, angle)  [per-voice with a position handler]
                        └── doppler pitch adjustment
                             └── occlusion low-pass filter
                                  └── channel insert chain (pre-fader, setDSP)
                                       └── channel volume / fader (tree)
                                            ├── aux sends ──→ return bus (makeReturn) ──┐
                                            │                   └── return insert (e.g. plate reverb)
                                            └── reverb blend  ◄─────────────────────────┘
                                                 └── device output (PortAudio / Oboe)
```

Channel routing (epic [#146](https://github.com/yvanvds/yse-soundengine/issues/146)): each `YSE::channel` carries an optional pre-fader **insert** chain (`setDSP`, a linked `dspObject` list) and up to N **aux sends** to **return buses** (`makeReturn` / `send` / `setSendLevel`). Returns are ordinary channels flagged as returns — they keep their own inserts and may send onward to other returns (the send graph must stay acyclic). Mix-grade effect modules for these slots live in `dsp/modules/` (`parametricEQ`, `compressor`, `chorus`, `plateReverb`, `morphingReverb`, `underWater`, `delay/feedbackDelay`).

---

### Key Architectural Patterns

| Pattern | Where used |
|---------|-----------|
| Interface + Implementation (pimpl) | `sound`, `channel`, `reverb`, `synth` — public API decoupled from audio-thread objects |
| Lock-free SPSC inbox | Main → audio handoff for new impls (no mutex on the audio path) |
| Manager singletons | `soundManager`, `channelManager`, `reverbManager`, `motifManager`, `scaleManager`, `playerManager`, `midifileManager` |
| Manager job templates | `managerSetupJob`/`managerDeleteJob` factor the common setup/delete-tick body across sound/channel/reverb |
| Singleton globals | `YSE::System()`, `YSE::Listener()`, `INTERNAL::Global()` |
| Single-threaded "slow pool" | Serialises file loads + manager setup/delete jobs |
| Chain of Responsibility | DSP `link()` — each processor hands buffer to next |
| Proximity blending | Reverb zones blended by listener distance |
| OBJECT-library + SHARED-library split | `yse_objects` consumed by both `yse` (DLL export) and `yse_tests` (direct linkage, full symbol visibility) |
| Atomic-swap callback bridge | C API wraps C++ callbacks with `YSE_C_CALLBACK` typedefs; no mutex on the audio path |
| `noexcept` destructor guard | `try`/`catch (...)` around every teardown body, reporting via `INTERNAL::EmitNoThrow` — the only log call a destructor may make |

---

## Demo Applications

**Location:** [Demo.Windows.Native/](Demo.Windows.Native/) — Windows-only (uses Windows console APIs and relies on RtMidi for MIDI demos).
22 demos (Demo00–Demo21) plus `Test01_Pitch` and a combined `Demo` executable that wires every page through `MenuTop.cpp`. Demo18–Demo21 are the synth & effects end-to-end showcases (issue #180); they read bundled assets from the optional content pack (issue #179) and degrade with a clear message when it is absent.

| Demo | Topic | Demo | Topic |
|------|-------|------|-------|
| Demo00 | Basic playback (`drone.ogg`) | Demo11 | Virtual I/O |
| Demo01 | Sound properties | Demo12 | AudioTest |
| Demo02 | 3D positioning | Demo13 | Patcher synthesis |
| Demo03 | Virtual sounds | Demo14 | Load patcher from JSON |
| Demo04 | Channel mixer | Demo15 | Audio device restart |
| Demo05 | Reverb zones | Demo16 | MIDI device output |
| Demo06 | Device enumeration | Demo17 | MIDI patcher |
| Demo07 | DSP source | Demo18 | FM + MIDI keyboard (DX7 bank) |
| Demo08 | Occlusion | Demo19 | SFZ piano (sustain pedal) |
| Demo09 | Streaming | Demo20 | Swarm (orbiting notes) |
| Demo10 | File position | Demo21 | Mixer (insert chain + send/return reverb) |
| Test01_Pitch | Pitch test | | |

Each standalone executable is generated from a per-target `main_<Demo>.cpp` produced by `configure_file()` against `cmake/demo_main.cpp.in`. Shared menu/page infrastructure lives in the `yse_demo_common` static library.

---

## Tests (`Tests/`)

**Framework:** [doctest](https://github.com/doctest/doctest) v2.4.11 vendored at `dependencies/doctest/doctest.h`.
**Scale:** ~7000 TEST_CASEs across ~318 `.cpp` files.
**Build gate:** `YSE_BUILD_TESTS=ON` (default OFF — demos and Android library builds are unaffected).
**Plan:** [Tests/TEST_PLAN.md](Tests/TEST_PLAN.md) (the 2.0 phased roadmap, kept as history, and how the suite runs today).

All test files compile into a single executable (`yse_tests`) — except on Android where it's built as `libyse_tests.so` loaded by a NativeActivity APK (`Tests/Android/`). Both variants link `yse_objects` directly, bypassing the DLL boundary so internal symbols are reachable without `API` annotations.

```
Tests/
  main.cpp                            # doctest entry point
  test_sanity.cpp                     # smoke test
  Android/                            # Gradle wrapper → NativeActivity APK
  support/
    audio_helpers.hpp                 # makeBuffer, measureRms, peakBinIndex, …
    null_device.hpp                   # engineInit / engineInitWithAudio helpers
    alloc_probe.{hpp,cpp}             # Replaced operator new/new[]; ProbeScope counts RT-path allocations on the arming thread
    test_alloc_probe.cpp              # Self-test: the probe must see every allocation shape it claims to, and only its own thread
    android_asset_bridge.cpp          # Extracts assets/fixtures/ to internal data path
    fixtures/
      test_mono_44100.wav             # 244 B mono PCM
      test_type0.mid                  # 41 B Type-0 MIDI
  channel/  clip/   clock/     dsp/      integration/  internal/  io/      listener/
  midi/     music/  patcher/   python/   reverb/       sound/     synth/   system/   utils/
```

### Per-suite CTest entries

`Tests/CMakeLists.txt` registers **44 CTest entries** (43 without `YSE_ENABLE_PYTHON`, which `yse_tests_python` needs; the entries are desktop-only — on Android the APK runs the suite). CTest is the entry point — running `yse_tests` with no `--test-suite=` / `--test-case=` filter is refused (#712; `YSE_TESTS_ALLOW_MONOLITHIC=1` overrides), and `yse_tests_monolithic_refused` pins that refusal.

- `yse_unit_tests` — the catchall: every suite except the ones that drive `System::close()` / `initOffline()`, mutate process-global engine state, or need a device, which each run in their own process.
- Per-suite entries with CTest labels, e.g. `yse_tests_patcher`, `_patcher_concurrency`, `_channel`, `_rendergolden` (bit-exact multi-worker mix, #857), `_sendstress`, `_synth*`, `_lifecycle`, `_logsafety`, `_devicelayer`, `_offlinesession`, `_capilowcov` / `_capilowcovlife` / `_close_interleaving`, `_clock`, `_clip`, `_probe`, `_contentpack`.
- `yse_tests_integration` (label `integration`) runs in the default ctest set (since 948e1c77); its cases that need a real audio device return early when none opens, so it passes headless. `ctest --preset tests-debug -L integration` runs it alone.

### Audio-thread allocation probe

`Tests/support/alloc_probe` replaces the global `operator new` / `new[]` and counts calls inside a `ProbeScope` (~150 scopes across ~110 test files assert a path does not allocate). On MinGW the test binary links the C++ runtime statically so libc++'s own allocations bind to the replacement (#697); the count is `thread_local` and a scope arms only its own thread (#701), so probed code must run on the arming thread. `yse_tests_probe` fails if either property is lost.

### Test fixture path

Exposed via `YSE_TEST_FIXTURES_DIR` compile definition:
- Desktop: `${CMAKE_CURRENT_SOURCE_DIR}/support/fixtures`
- Android: `/data/user/0/net.attrx.yse.tests/files/fixtures` (populated by `android_asset_bridge.cpp` extracting from APK assets at startup)

---

## Benchmarks (`Bench/`)

**Framework:** google-benchmark, fetched at tag `v1.9.0`.
**Build gate:** `YSE_BUILD_BENCHMARKS=ON`.
**CI:** `.github/workflows/benchmark.yml` runs on push to master/dev and PRs to master; results are pushed to the orphan `bench-history` branch (never merged into mainline) so historical bench data accumulates without polluting `dev`/`master`.

Layout:
```
Bench/
  dsp/         (bench_buffer, bench_delay, bench_filters, bench_math, bench_oscillators, bench_reverb,
               bench_plate_reverb, bench_va_voice, bench_sampler_voice, bench_fm_voice)
  internal/    (bench_mpmcqueue)
  patcher/     (bench_patcher)
  integration/ (bench_mixing, bench_synth_effects, bench_render_heavy, bench_yse_dsl)
  support/     (bench_helpers.hpp)
```

Per-voice benches carry a `sineVoice` baseline and the macro scenarios run offline via `System().renderOffline(blocks)` (#181). `bench_render_heavy` (#857) sweeps heavy scenes over the render worker count through `INTERNAL::Global().setRenderWorkerCount()`, pairs with the bit-exact golden test `Tests/channel/test_render_golden.cpp`, and reports a `serial` counter (#861). Protocol and baselines: [docs/design/render_scheduler.md](docs/design/render_scheduler.md); how to run: [Bench/README.md](Bench/README.md).

---

## Documentation (`documentation/`)

**Tooling:** Doxygen (CI pins 1.17.0; `WARN_AS_ERROR = FAIL_ON_WARNINGS`) → XML → Sphinx + Breathe + sphinx-book-theme → HTML.
**Output:** [github.io/yse-soundengine/](https://yvanvds.github.io/yse-soundengine/) (deployed by `documentation.yml` on push to master; the same job builds with warnings-as-errors on dev PRs and pushes, #868).

```
documentation/
  Doxyfile                         # Doxygen config (XML output → source/_doxygen/xml/)
  requirements.txt                 # Sphinx + Breathe + sphinx-book-theme (upper-bounded; breathe exact-pinned)
  Makefile / make.bat              # `make html`, `make sphinx`, `make doxygen`, `make serve`
  source/
    conf.py                        # Reads VERSION from YseEngine/system.hpp (PR #89)
    index.rst                      # Landing page
    intro/                         # install, hello_sound, mental_model, sessions_and_devices, threading
    tutorials/                     # 16 pages: sounds, 3D, channels, reverb, synths/voices/instruments,
                                   # mixing, per-note 3D, patcher (5 pages), clocks and clips
    patcher/                       # Patcher guide: 14 pages (messages, realtime, subpatchers, host_io, time,
                                   # data, files, gui, midi, extending, c_api, file_format, api, building)
      objects/                     # Per-category object reference, rendered from _data/patcher_objects.json
    live_coding/                   # Live-coding DSL
    api/                           # 16 grouped Breathe pages + index (core, sounds, channels, dsp, dsp_modules,
                                   # effects, synth, devices, midi, music, clips, patcher, player, types, utils, c_api)
    upgrading.rst                  # 2.4 → 3.0 upgrade guide
```

The version string in `conf.py` is auto-synced from `YseEngine/system.hpp` so the published docs always match the released library.

---

## Vendored / Fetched Dependencies

| Library | Source | Used for |
|---------|--------|---------|
| PortAudio headers | `dependencies/portaudio/include/` | Windows-specific extension headers not shipped by the MSYS2 package; the library itself comes from the system |
| RtMidi headers | `dependencies/rtmidi/include/` | Header search path that resolves both `"RtMidi.h"` and the vendored copy; library comes from the system |
| libsndfile (prebuilt) | `dependencies/libsndfile/`, `dependencies/libsndfile64/` | Headers + import libs for the legacy Visual Studio projects only; the CMake build uses the system libsndfile on desktop |
| libsndfile 1.2.2 | FetchContent (Android only) | WAV-only static build, no external codec libs |
| Oboe 1.9.3 | FetchContent (Android only) | Audio I/O — AAudio with OpenSL ES fallback |
| nlohmann/json | `YseEngine/utils/json.hpp` (vendored single header) | Patcher `DumpJSON` / `ParseJSON`, object state, `.dict`/`.array` JSON |
| pybind11 v2.13.6 | FetchContent (`cmake/YsePython.cmake`, only with `YSE_ENABLE_PYTHON=ON`) | The embedded `yse` Python module |
| doctest 2.4.11 | `dependencies/doctest/doctest.h` | Single-header C++ test framework (MIT) |
| google-benchmark 1.9.0 | FetchContent (when `YSE_BUILD_BENCHMARKS=ON`) | Benchmarks |
