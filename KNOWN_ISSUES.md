# Known Issues

## Symbol visibility (all symbols exported — hardening is a follow-up)

**Category:** Build / ABI hygiene

The `API` export macro in `YseEngine/headers/defines.hpp` uses
`__declspec(dllexport/dllimport)` under MSVC and
`__attribute__((visibility("default")))` under macOS.  With Clang on MinGW
(MSYS2), `YSE_MSVC` is not set and the macro expands to nothing.  Windows PE
shared libraries do **not** export symbols implicitly (unlike Linux ELF), so
the linker flag `-Wl,--export-all-symbols` is added in `YseEngine/CMakeLists.txt`
to force full export.

**Follow-up:** Add a `YSE_CLANG` + `YSE_WINDOWS` branch to the `API` macro
that emits `__declspec(dllexport)` when building the DLL and
`__declspec(dllimport)` for consumers. Remove `--export-all-symbols` once the
API surface is explicitly annotated.

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
