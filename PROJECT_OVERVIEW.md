# YSE Sound Engine — Project Overview

**Version:** 1.0.77  
**Language:** C++11  
**Platforms:** Windows, Android, Linux  
**Key Dependencies:** PortAudio (audio I/O), libsndfile (file loading), pthreads

---

## Repository Layout

```
YseEngine/                  # Core C++ sound engine (primary focus)
Demo.Windows.Native/        # 17 C++ console demos
Demo.Windows.WPF/           # WPF GUI demo
Demo.Android.Native/        # Android native demo
Demo.Android.NET/           # Android .NET demo
Demo.Xamarin.Forms/         # Cross-platform Xamarin demo
Yse.Windows.Native/         # Windows static/shared lib build
Yse.Windows.Universal/      # UWP build
NetYse/ YSE.NET.PCL/        # .NET / PCL wrappers
Yse.NET.Standard/           # .NET Standard wrapper
Yse.NET.Android/            # Android .NET bindings
dependencies/               # Vendored external libraries
doxyGen/                    # Doxygen config
TestResources/              # Audio test files (drone.ogg, kick.ogg, …)
CMakeLists.txt              # Cross-platform CMake build
YseEngineWindows.sln        # Visual Studio Windows solution
YseEngineAndroid.sln        # Visual Studio Android solution
```

---

## Sound Engine Architecture (`YseEngine/`)

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
    └── MIDI                   file + device I/O
```

---

### 1. Sound Objects

**Files:** `sound/soundInterface.hpp`, `sound/soundImplementation.cpp`, `sound/soundManager.cpp`

Each public `YSE::sound` wraps a private `SOUND::implementationObject`. State changes are posted as messages to the audio thread; they are never applied directly.

**Creation variants:**
```cpp
sound.create(fileName, channel, loop, volume, streaming);
sound.create(DSP::buffer, channel, loop, volume);
sound.create(dspSourceObject, channel, volume);   // procedural source
sound.create(patcher, channel, volume);           // modular synthesis
```

**Key per-sound properties:**
| Property | Description |
|----------|-------------|
| position (Pos) | 3D world position |
| size (float) | rolloff distance |
| speed (float) | playback speed / pitch |
| volume | with optional fade time |
| spread | multichannel stereo spread |
| relative | move with listener (2D mode) |
| doppler | enable doppler pitch shift |
| occlusion | 0–1 low-pass filter amount |

**Implementation state machine:**  
`CONSTRUCTED → CREATED → LOADED → READY → DONE`  
**Play intent states:** `SI_NONE / SI_PLAY / SI_STOP / SI_PAUSE / SI_TOGGLE / SI_RESTART`

---

### 2. 3D Spatialization

**Files:** `listener.hpp`, `implementations/listenerImplementation.cpp`

`YSE::Listener()` is a singleton representing the listener's point of view. Its velocity is derived automatically from position deltas each update tick.

**Per-frame pipeline for each active sound:**
1. Compute distance from sound position to listener position.
2. Apply inverse-distance volume attenuation (governed by `size`).
3. Compute angle → map to stereo/surround pan coefficients.
4. If `doppler` is set, shift pitch based on relative radial velocity.
5. If an occlusion callback is registered, query it for a 0–1 occlusion value and apply a corresponding low-pass filter.

**Occlusion hook:**
```cpp
system& occlusionCallback(float(*func)(const Pos& source, const Pos& listener));
```
The application provides a raycast function; the engine calls it and applies the result as a filter cutoff.

**Output channel configurations:**  
`CT_MONO CT_STEREO CT_QUAD CT_51 CT_51SIDE CT_61 CT_71 CT_CUSTOM`  
Speaker positions for custom configs are user-defined, enabling arbitrary surround layouts.

---

### 3. Channel System (Hierarchical Mixer)

**Files:** `channel/channelInterface.hpp`, `channel/channelImplementation.cpp`, `channel/channelManager.cpp`

Channels form a **tree** whose root is `MainMix`. Five pre-made leaf channels exist: `FX`, `Music`, `Ambient`, `Voice`, `GUI`. Custom channels can be inserted anywhere in the tree. Volume and effects cascade from leaves to root.

```
MainMix
├── FX
├── Music
├── Ambient
├── Voice
└── GUI
    └── (custom children …)
```

Sounds can be reparented (`moveTo`) at runtime. Channels support virtual sound culling (sounds beyond distance threshold are suspended to save CPU).

---

### 4. Reverb System

**Files:** `reverb/reverbInterface.hpp`, `reverb/reverbImplementation.cpp`, `reverb/reverbManager.cpp`

There is **one** actual reverb processor. Multiple `YSE::reverb` objects each represent a positioned zone with its own parameters; the engine blends them by proximity to the listener.

**Reverb properties:** position, size, rollOff, roomSize, damping, dryWetBalance, modulation.  
**Built-in presets:** `REVERB_OFF, GENERIC, PADDED, ROOM, BATHROOM, STONEROOM, LARGEROOM, HALL, CAVE, SEWERPIPE, UNDERWATER`

---

### 5. DSP System

**Files:** `dsp/dspObject.hpp`, `dsp/buffer.hpp`, `dsp/fileBuffer.hpp`, `dsp/` modules

#### Base abstractions

```cpp
class dspObject {          // filter / effect in a chain
  virtual void process(MULTICHANNELBUFFER&) = 0;
  void link(dspObject& next);          // chain processors
};

class dspSourceObject {    // audio generator (replaces file)
  virtual void process(SOUND_STATUS& intent) = 0;
  virtual void frequency(float value) = 0;
};
```

`DSP::buffer` — single-channel float buffer with thread-safe length query and arithmetic operators.  
`MULTICHANNELBUFFER` — vector of buffers; supports mono-to-surround spreading.

#### Available DSP modules

| Category | Modules |
|----------|---------|
| Oscillators | sine, cosine, saw, noise, VCF |
| Filters | lowPass, highPass, bandPass, biQuad, sampleHold, sweep, phaser |
| Envelopes | envelope, ADSRenvelope, ramp |
| Modulators | LFO, delay, basicDelay, highpassDelay, lowpassDelay |
| Math | clip, sqrt, rSqrt, wrap, midiToFreq, freqToMidi, dbToRms, rmsToDb |
| Spectral | Hilbert transformer, ring modulator |
| Granular | granulator |
| FM | difference (FM pair) |

---

### 6. Audio Device Layer

**Files:** `device/deviceInterface.hpp`, `device/portaudioDeviceManager.cpp`

Abstracts the OS audio API behind a single interface. PortAudio handles Windows (ASIO/WASAPI/DirectSound/MME) and Linux; OpenSL ES is used on Android.

```cpp
const std::vector<device>& getDevices();
void openDevice(const deviceSetup&, CHANNEL_TYPE = CT_AUTO);
```

The engine monitors for missed callbacks and can auto-reconnect on device dropout.

---

### 7. File Loading & Streaming

**Files:** `dsp/fileBuffer.hpp`, `internal/abstractSoundFile.cpp`

- **Buffered (default):** file loaded fully into memory; shared across all sounds using the same path; auto-released when unused.
- **Streaming:** per-sound disk read; for large files where memory is the constraint.
- Custom file-reader callbacks are supported (e.g. network streams).

---

### 8. Patcher (Modular DSP Graph)

**Files:** `patcher/patcher.hpp`, `patcher/patcherImplementation.cpp`

A Max/MSP-style node graph for building synthesis and effect networks in code or JSON.

```cpp
pHandle* obj = patcher.CreateObject("pSine", "440");
patcher.Connect(oscHandle, 0, dacHandle, 0);
patcher.DumpJSON();          // serialize
patcher.ParseJSON(content);  // restore
```

**Node categories:** generators (sine, saw, noise), filters, math (+−×÷, counter, random), delays, gates/switches, routing (send/receive), GUI controls (button, slider, toggle), DAC output.

---

### 9. Threading & Concurrency Model

- **Audio callback thread** — managed by PortAudio/OpenSL ES; runs DSP chain at buffer rate.
- **Application thread** — drives `system::update()` each frame.
- **Communication** — all cross-thread state changes use a lock-free message queue; no mutexes in the hot path.
- **Thread-safe atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases).

---

### 10. Supporting Subsystems (Brief)

**Music / Composition** (`music/`, `player/`) — polyphonic note player with scale constraints, motif sequencing, and randomized pitch/velocity/gap ranges.

**MIDI** (`midi/`) — MIDI file loading/playback, live MIDI device I/O (Windows MME), MIDI↔frequency helpers.

---

### Full Audio Signal Path

```
sound.play()
    └── DSP source (file buffer / dspSourceObject / patcher)
         └── speed / pitch shifting
              └── user DSP chain (dspObject link list)
                   └── 3D attenuation + pan (distance, angle)
                        └── doppler pitch adjustment
                             └── occlusion low-pass filter
                                  └── channel volume (tree)
                                       └── reverb blend
                                            └── device output (PortAudio / OpenSL ES)
```

---

### Key Architectural Patterns

| Pattern | Where used |
|---------|-----------|
| Interface + Implementation | `sound`, `channel`, `reverb` — public API decoupled from audio-thread objects |
| Message queue | All sound/channel state changes; keeps audio callback lock-free |
| Manager singletons | `soundManager`, `channelManager`, `reverbManager` — centralized lifecycle |
| Singleton globals | `YSE::System()`, `YSE::Listener()`, `INTERNAL::Global()` |
| Chain of Responsibility | DSP `link()` — each processor hands buffer to next |
| Proximity blending | Reverb zones blended by listener distance |

---

## Other Project Parts (Summary)

**Demo applications** — 17 Windows console demos covering 3D positioning, reverb zones, occlusion, streaming, patcher synthesis, MIDI, and the player system. WPF and Android demos also present.

**C# / .NET wrappers** (`NetYse/`, `YSE.NET.PCL/`, `Yse.NET.Standard/`, `Yse.NET.Android/`) — P/Invoke bindings exposing the engine to .NET Framework, .NET Standard, and Xamarin. Mirrors the C++ API surface.

**Build system** — CMake for Linux/Android, Visual Studio solution for Windows (static and shared lib targets). Android NDK build available via both Android Studio and Visual Studio.
