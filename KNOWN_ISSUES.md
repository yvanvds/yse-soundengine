# Known Issues

## Symbol visibility — resolved

**Category:** Build / ABI hygiene

`YseEngine/headers/defines.hpp` now has a `YSE_WINDOWS && YSE_CLANG` branch
that emits `__declspec(dllexport)` when building the DLL (`YSE_DLL_BUILD`) and
`__declspec(dllimport)` for consumers (`YSE_DLL`).  Every public class, free
function, and extern variable reachable from `yse.hpp` carries the `API` macro.
`-Wl,--export-all-symbols` has been removed.  The dead MSVC compiler branches
(`YSE_MSVC`, `YSE_VC8_OR_EARLIER`, `YSE_VC7_OR_EARLIER`) were also deleted.

Engine sources are compiled into an OBJECT library (`yse_objects`) with
`YSE_DLL_BUILD` defined.  The shipped shared library (`yse`) links that OBJECT
library and propagates `YSE_DLL` via `INTERFACE` to all downstream consumers.
The test executable links the same OBJECT library directly — bypassing the DLL
export boundary — so white-box tests can reach internal symbols without API
annotations.  Both build configurations produce an identical `libyse.dll`
export table (verified with `llvm-readobj --coff-exports`).

**Remaining gap:** Linux and macOS builds use `CXX_VISIBILITY_PRESET default`
(ELF export-all) rather than explicit `-fvisibility=hidden` + per-symbol
`__attribute__((visibility("default")))`.  Adding visibility attributes for
ELF targets is a separate follow-up.

---

## RtMidi required on every desktop platform

**Category:** Dependency

The MIDI device backend (`midi/midiDeviceManager.cpp`, `midi/device.cpp`,
and the `MIDI::DeviceManager()` call in `system.cpp`) is guarded by
`#if YSE_WINDOWS || YSE_LINUX` and links against RtMidi symbols. RtMidi is
therefore a mandatory dependency on both Windows and Linux; CMake configuration
fails with a clear error if it is missing. Android still compiles the MIDI
device files out and does not need RtMidi.

The Linux path uses RtMidi's ALSA backend. It is functional but lightly tested
in practice — please report any ALSA-specific issues you encounter.

Install:
- Windows (MSYS2 Clang64): `pacman -S mingw-w64-clang-x86_64-rtmidi`
- Debian/Ubuntu: `sudo apt install librtmidi-dev`
- Fedora/RHEL: `sudo dnf install rtmidi-devel`

**Follow-up:** Gate the MIDI device backend behind a `YSE_ENABLE_MIDI_DEVICE`
option; this would let desktop builds without RtMidi still produce a library
(without MIDI output support).

---

## Demo run-directory requirement

**Category:** Runtime / Working directory

Demo binaries use the hard-coded relative path `../../TestResources/...` for
audio files.  This resolves correctly only when the binary is run from
`build/bin/` (two levels below the project root where `TestResources/` lives).

**Workaround:** `cd build/bin && ./Demo00.exe`

**Follow-up:** Replace hard-coded paths with a CMake-injected
`YSE_TEST_RESOURCES_DIR` compile definition, or use a runtime executable-path
lookup (`GetModuleFileName` / `/proc/self/exe`).

---

## ASIO PortAudio backend not available on MSYS2

**Category:** Feature / Platform

The MSYS2 PortAudio package is built without the ASIO backend (ASIO requires
the proprietary Steinberg SDK).  `portaudioDeviceManager.cpp` called
`PaAsio_GetAvailableLatencyValues` (a macro for `PaAsio_GetAvailableBufferSizes`)
inside `#ifdef __WINDOWS__` guards.  The guards were changed to
`#if defined(PA_USE_ASIO)` so the ASIO path is compiled out by default.  The
fallback uses `info->defaultHighOutputLatency`, which works correctly for
WASAPI/DirectSound/WDM.

Defining `PA_USE_ASIO` at compile time will re-enable the ASIO path if
PortAudio is built with ASIO support.

---


## Demo03 `_cprintf_s` replaced with `printf`

**Category:** Source bit-rot fixed

`Demo03_Virtual.cpp` called `_cprintf_s`, an MSVC CRT function that is
declared in MinGW headers but not provided by the MinGW runtime.  The call
was replaced with `printf`, which is functionally equivalent for this use
case (in-place status line refresh via `\r`).

---



## `interpolate4::operator()` does not resize its output buffer

**Category:** Memory safety / DSP

[YseEngine/dsp/interpolate4.cpp:37-71](YseEngine/dsp/interpolate4.cpp#L37-L71)
holds a private `buffer out` member that is default-constructed
(STANDARD_BUFFERSIZE = 128 samples) and never resized.  `operator()` writes
`in.getLength()` floats into `out` without first calling `out.resize(in.getLength())`,
so any input longer than 128 samples corrupts the heap.  This propagates to
every consumer — most visibly `sweepFilter::process()`
([YseEngine/dsp/modules/filters/sweep.cpp:83-97](YseEngine/dsp/modules/filters/sweep.cpp#L83-L97)),
which forwards `result` (sized to the incoming `MULTICHANNELBUFFER` channel)
straight through the interpolator.

**Repro:** Construct a `sweepFilter`, feed it a `MULTICHANNELBUFFER` with a
channel buffer larger than 128 samples, call `process()`; on Windows MSYS2
Clang64 builds this consistently surfaces as exit code `0xC0000374`
(`STATUS_HEAP_CORRUPTION`).

**Workaround in tests:** Phase A's sweepFilter tests in
[Tests/dsp/test_module_filters.cpp](Tests/dsp/test_module_filters.cpp) only
exercise the 128-sample path.  A regression test for the resize path is
deferred until the fix lands.

**Fix:** Add `if (in.getLength() != out.getLength()) out.resize(in.getLength());`
at the top of `interpolate4::operator()` (mirroring the pattern used by
`oscillator::operator()` in [oscillators.cpp:244-251](YseEngine/dsp/oscillators.cpp#L244-L251)).

---

## `dVcf::Calculate` dereferences a null `out2` pointer

**Category:** Memory safety / DSP

[YseEngine/patcher/filters/dVcf.cpp:51-61](YseEngine/patcher/filters/dVcf.cpp#L51-L61)
declares `DSP::buffer * out2 = nullptr;` and then immediately passes `*out2`
to `vcf::operator()(in, center, out2&)`
([oscillators.cpp:316-318](YseEngine/dsp/oscillators.cpp#L316-L318)), which
calls `out2.getLength()` and `out2.getPtr()` on the null reference.  This is
undefined behaviour; on Windows MSYS2 Clang64 builds it consistently
segfaults as soon as both `in` and `center` are connected.

**Repro:** Instantiate `YSE::PATCHER::dVcf`, feed valid buffers to inlets 0
and 1, call `Calculate(T_DSP)`.

**Workaround in tests:** Phase C's dVcf tests in
[Tests/patcher/test_dsp_objects.cpp](Tests/patcher/test_dsp_objects.cpp) only
exercise the metadata and the two null-input early-return paths.  A real
filter-behaviour regression test is deferred until the fix lands.

**Fix:** Either give `dVcf` an owned `DSP::buffer out2;` member (mirroring
how `pBandpass`/`pHighpass` keep their working buffers) and pass it through,
or change `vcf::operator()` to take an optional pointer for the second
output.  The two-output design is the source of the awkwardness — the
existing `// TODO` comment at
[oscillators.hpp:101](YseEngine/dsp/oscillators.hpp#L101) already flags this.

---

## `patcherImplementation::oscHandle` was uninitialised — resolved

**Category:** Memory safety / Patcher

[YseEngine/patcher/patcherImplementation.h](YseEngine/patcher/patcherImplementation.h)
declares `oscHandler * oscHandle;` as a raw pointer.  The constructor used to
omit it from the initialiser list, so `oscHandle` started out as garbage.  The
four `PassBang` / `PassData` overloads then guard their fallback with
`if (oscHandle != nullptr) oscHandle->Send(...)` — when no matching `gReceive`
exists in the patcher, that branch dereferences whatever happened to be in the
field's storage, which is a non-deterministic SIGSEGV.

**Repro:** Create a `YSE::patcher`, add a `gSend` with one dataName and a
`gReceive` with a different dataName, then call `gSend.SetIntData(0, ...)`.
With nothing registered via `SetOscHandler`, the dispatch falls through to the
uninitialised pointer.

**Fix:** Initialise `oscHandle(nullptr)` in the
[patcherImplementation constructor](YseEngine/patcher/patcherImplementation.cpp).
The new mismatched-name test in
[Tests/patcher/test_generic_objects.cpp](Tests/patcher/test_generic_objects.cpp)
("gSend -> gReceive: mismatched dataName drops messages silently") pins the
regression.

---

## suble notes from claude code we picked up. Maybe worth looking into

The IDE diagnostics on the TEST_SUITE("dsp") { line are IntelliSense false positives — the macro expands to a namespace block that Clang compiles cleanly (as confirmed by the build above).
