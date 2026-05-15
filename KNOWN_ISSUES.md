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

## `scale::getNearest` dereferences `end()` for empty / above-last queries

**Category:** Memory safety / Music

[YseEngine/music/scale/scaleImplementation.cpp:115-128](YseEngine/music/scale/scaleImplementation.cpp#L115-L128)
calls `std::lower_bound(pitches.begin(), pitches.end(), pitch)` and then
immediately dereferences the returned iterator (`if (*high == pitch) ...`).
Two inputs make `lower_bound` return `pitches.end()`:

1. The scale is empty (`pitches.size() == 0`).
2. The query pitch is strictly greater than the highest pitch in the scale.

In both cases `*high` is UB — on Windows MSYS2 Clang64 builds this typically
reads stale memory; on coverage builds it has been observed to segfault.

**Repro:** `YSE::scale s; s.getNearest(60.f);` (empty), or
`YSE::scale s; s.add(60.f, 0); s.getNearest(70.f);` (above-last).

**Workaround in tests:** Phase E's getNearest tests in
[Tests/music/test_scale.cpp:128-135](Tests/music/test_scale.cpp#L128-L135)
only exercise the below-first and within-range paths. The empty and
above-last branches are deferred until the fix lands.

**Fix:** Add an early-return at the top of `getNearest`:
```cpp
if (pitches.empty()) return pitch;       // or NaN — caller-policy decision
auto high = std::lower_bound(pitches.begin(), pitches.end(), pitch);
if (high == pitches.end()) return pitches.back();
```

---

## `getMidiOutPort` caches a partially-initialised `RtMidiOut` when `openPort` throws

**Category:** Resource leak / MIDI

[YseEngine/midi/midiDeviceManager.cpp:114-126](YseEngine/midi/midiDeviceManager.cpp#L114-L126)
emplaces a freshly-constructed `RtMidiOut*` into `midiOutPorts` *before* calling
`openPort(ID)`.  When `openPort` throws (e.g. invalid port id), the catch arm
returns `nullptr` but the map entry stays alive, holding a port-less
`RtMidiOut`.  The next call with the same ID hits the `count(ID) > 0` branch
and returns that cached pointer — which behaves like a no-op device (its
`sendMessage` writes to nowhere), masking the original failure.

**Repro:** `MIDI::DeviceManager().getMidiOutPort(9999)` returns `nullptr` on
the first call and a non-null, useless pointer on the second.  Verified by
Phase G's getMidiOutPort cache test in
[Tests/midi/test_devicemanager.cpp](Tests/midi/test_devicemanager.cpp).

**Fix:** Either insert into the cache only after `openPort` succeeds, or
delete + erase the entry inside the catch arm:
```cpp
try {
  auto * port = new RtMidiOut();
  port->openPort(ID);                 // throws on invalid id
  midiOutPorts.emplace(ID, port);
  return port;
} catch (RtMidiError& error) {
  MIDI::GenerateMidiError(error);
  return nullptr;
}
```

---

## `SOUND::Manager` has unsynchronised cross-thread access to its impl lists

**Category:** Memory safety / Concurrency

The sound manager runs on three threads with no locking between them:

1. **Main / user thread** — calls `SOUND::Manager().update()`, which iterates
   `inUse` and `toLoad` and may call `remove_if` on either
   ([soundManager.cpp:141-188](YseEngine/sound/soundManager.cpp#L141-L188)).
2. **Slow thread pool** — runs `setupJob::run()`, which **iterates the same
   `toLoad` list** concurrently
   ([soundManager.h:59-63](YseEngine/sound/soundManager.h#L59-L63)),
   and `deleteJob::run()`, which calls `remove_if` on `implementations`
   ([soundManager.h:90-92](YseEngine/sound/soundManager.h#L90-L92)).
3. **Audio callback thread (PortAudio)** — calls
   `implementationObject::dsp()`, which dereferences `source_dsp` (a pointer
   to the user-supplied `dspSourceObject`) at
   [soundImplementation.cpp:592-594](YseEngine/sound/soundImplementation.cpp#L592-L594).

`std::forward_list` is not thread-safe, and the atomic in
`forward_list<std::atomic<implementationObject*>>` protects only the pointer
value — not the list nodes. Concurrent iteration + `remove_if` on `toLoad` is
a data race; iteration through `inUse` while `dsp()` runs on the audio thread
is a second race; and `source_dsp` can dangle if a caller passes a
stack-local DSP source whose lifetime ends before the audio callback fires
again.

**Repro:** Run the full test binary on a system that has a real default
output device (i.e. any developer workstation; CI has none, so
`Pa_GetDefaultOutputDevice()` returns `paNoDevice` and the audio callback
is never opened). Outcomes are non-deterministic across runs:

- `SEGFAULT` (typically at process exit, sometimes mid-test)
- `Exit code 0xC0000374` (`STATUS_HEAP_CORRUPTION`)
- `Exit code 0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`) preceded by
  `libc++abi: Pure virtual function called!`
- Hang on `Pa_OpenStream` when running the binary again from the same shell
  (Windows MME holds the device for ~hundreds of ms after `Pa_CloseStream`).

**Workarounds in tests:** Three layers, all on the test side — no engine
changes:

1. [Tests/support/null_device.hpp](Tests/support/null_device.hpp):
   `engineInit()` calls `System().pause()` immediately after `System().init()`
   so the audio callback is closed for the entire unit suite. A separate
   `engineInitWithAudio()` (idempotent via a local static gate) is used by
   the integration tests that genuinely need the audio thread running.
2. [Tests/CMakeLists.txt](Tests/CMakeLists.txt): the `yse_unit_tests` ctest
   entry passes `--test-suite-exclude=integration` so the unit run doesn't
   re-open the audio stream (the integration suite has its own DISABLED
   ctest entry and is opted into explicitly via
   `python yse.py test --integration`).
3. [Tests/sound/test_sound_impl.cpp](Tests/sound/test_sound_impl.cpp): all
   `SilentSource` / `NopDsp` instances are file-scope statics so that even
   if a stray audio callback fires past `Pa_StopStream`, it lands on a
   still-live object whose `process()` is a no-op.

These eliminate the segfault under the normal `python yse.py test`
workflow but do not fix the underlying engine race; consecutive
back-to-back runs of the binary from the same shell can still surface heap
corruption or hangs intermittently. The integration suite, when explicitly
invoked, exercises the live audio callback and remains flaky for the same
reason.

**Fix:** Add a `std::mutex` to `SOUND::managerObject` and take it around every
modification or iteration of `toLoad`, `inUse`, and `implementations`. The
audio-callback path (`dsp()`) needs a lock-free or RCU-style structure to
read the live-impl list — taking a mutex on the audio thread is not
acceptable. `CHANNEL::managerObject` and `REVERB::managerObject` use the
same `setupJob`/`deleteJob` pattern via `Global().addSlowJob` and likely
have analogous races worth auditing once the sound side is fixed.

---

## suble notes from claude code we picked up. Maybe worth looking into

The IDE diagnostics on the TEST_SUITE("dsp") { line are IntelliSense false positives — the macro expands to a namespace block that Clang compiles cleanly (as confirmed by the build above).
