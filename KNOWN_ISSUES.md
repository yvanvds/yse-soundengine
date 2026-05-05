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

## RtMidi required on Windows

**Category:** Dependency

`system.cpp` calls `MIDI::DeviceManager()` (inside `#if YSE_WINDOWS`) and
`midiDeviceManager.cpp` / `device.cpp` link against RtMidi symbols.  These
cannot be compiled-out without source changes, so RtMidi is a mandatory
dependency on Windows.

Install with: `pacman -S mingw-w64-clang-x86_64-rtmidi`

**Follow-up:** Gate the MIDI device backend behind a `YSE_ENABLE_MIDI_DEVICE`
option; this would let Windows builds without RtMidi still produce a library
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

## `<experimental/filesystem>` updated to `<filesystem>`

**Category:** Source bit-rot fixed

`YseEngine/utils/fileFunctions.cpp` used `<experimental/filesystem>` and
`std::experimental::filesystem::path`, which is not available in modern
libc++ (C++17 standardized `<filesystem>`).  The include and namespace were
updated to the standardized form.

---

## Demo03 `_cprintf_s` replaced with `printf`

**Category:** Source bit-rot fixed

`Demo03_Virtual.cpp` called `_cprintf_s`, an MSVC CRT function that is
declared in MinGW headers but not provided by the MinGW runtime.  The call
was replaced with `printf`, which is functionally equivalent for this use
case (in-place status line refresh via `\r`).

---

## patcher/midi objects not in original source list

**Category:** Source omission fixed

The seven `patcher/midi/mMidi*.cpp` files were absent from the original
`CMakeLists.txt` but are registered unconditionally in `patcher/pRegistry.cpp`.
Omitting them caused linker errors.  They are now included in the engine source
list.

---

## `internal/AudioTest.cpp` re-included

**Category:** Source omission fixed

`internal/AudioTest.cpp` was commented out in the original `CMakeLists.txt`.
`system.cpp` calls `YSE::INTERNAL::Test()` inside `#ifdef __WINDOWS__`, so
the DLL failed to link without it.  The file is now compiled into the library.

---

## `buffer::maxValue()` — SIMD unroll assigns wrong element

**Category:** DSP bug

In `YseEngine/dsp/buffer.cpp`, the 8-wide unrolled loop in `maxValue()` correctly
checks `ptr1[1..7] > max` but then always assigns `ptr1[0]` to `max` instead of
the element that was compared.  As a result `maxValue()` can return a wrong (too
small) value for any buffer with 8 or more samples where the true maximum is not
in position 0.

The scalar tail loop (`while (l--)`) is correct and runs for the leftover samples
after the last full group of 8.

**Workaround:** Use `getMaxAmplitude()` from `dsp/math_functions.h`, which
iterates correctly; or call `maxValue()` only on buffers whose length is not a
multiple of 8, relying solely on the scalar tail.

**Unit-test impact:** `Tests/test_buffer.cpp` deliberately uses buffers of length
< 8 for `maxValue()` assertions to stay on the correct scalar path.

---

## `buffer` constructor leaves `cursor` and `sampleRateAdjustment` uninitialised

**Category:** Latent bug / UB

`YseEngine/dsp/buffer.cpp`'s primary constructor
```cpp
buffer::buffer(UInt length, UInt overflow)
    : storage(length + overflow), overflow(overflow) {}
```
does not initialise the public `cursor` raw pointer or the protected
`sampleRateAdjustment` field.  Reading either member on a freshly constructed
`buffer` is undefined behaviour.

In the current engine this is safe because all internal code that uses `cursor`
sets it explicitly before reading, and `sampleRateAdjustment` is only read after
a device-open call (which sets it via `setSampleRateAdjustment()`).

**Follow-up:** Add `cursor(storage.data()), sampleRateAdjustment(1.0f)` to the
initialiser list so the class is unconditionally safe to inspect.


## suble notes from claude code we picked up. Maybe worth looking into

The IDE diagnostics on the TEST_SUITE("dsp") { line are IntelliSense false positives — the macro expands to a namespace block that Clang compiles cleanly (as confirmed by the build above).
