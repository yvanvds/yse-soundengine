/*
  ==============================================================================

    synthImplementation.cpp
    Audio-thread synth implementation — voice allocator, stealing policy, mix,
    and lifecycle. See synthImplementation.h and docs/design/synth_core.md
    (§2 / §4 / §8).

  ==============================================================================
*/

#include "synthImplementation.h"
#include "../internalHeaders.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace YSE {
  namespace SYNTH {

    // Engine-owned steal declick length, in seconds (~5 ms). A stolen voice's
    // tail is force-faded over this window so stealing never clicks, regardless
    // of the user voice's own (possibly seconds-long) release. See §4.
    static const Flt kStealFadeSec = 0.005f;

    // Velocity scaling applied to notes that START while the soft pedal (CC 67)
    // is held (§5). Fixed; affects only new notes, never sounding voices.
    static const Flt kSoftPedalGain = 0.7f;

    // A controller value at or above this counts as a pedal "down" when a pedal
    // arrives as a raw CC (64/66/67) rather than via the typed pedal setters.
    static const Flt kPedalThreshold = 0.5f;

    implementationObject::implementationObject(interfaceObject* head)
      : head(head), objectStatus(OBJECT_CONSTRUCTED), messages(1024), output(*this, 1) {}

    implementationObject::~implementationObject() {
      // If the interface somehow outlives us (e.g. engine teardown clears the
      // implementations list), null its back-pointer so it does not dereference
      // freed storage. In the normal path the interface is already gone (that
      // is what drove us to OBJECT_RELEASE via sync()). The unique_ptr voices
      // free here, on the setup pool's delete job — never on the audio thread.
      interfaceObject* h = head.load(std::memory_order_acquire);
      if (h != nullptr) {
        h->pimpl = nullptr;
      }
    }

    // ---- control thread ---------------------------------------------------

    void implementationObject::sendMessage(const messageObject& message) {
      // Non-allocating push: a flood of note events drops on overflow rather
      // than growing the queue, so no thread ever allocates for MIDI (§6).
      if (!messages.try_push(message)) {
        INTERNAL::LogImpl().emit(E_DEBUG, "SYNTH: note inbox full, message dropped");
      }
    }

    void implementationObject::addVoiceGroup(dspVoice* prototype, int numVoices, int channel,
                                             int lowestNote, int highestNote) {
      if (prototype == nullptr || numVoices <= 0) return;
      std::scoped_lock lk(buildMutex);
      if (objectStatus.load(std::memory_order_acquire) >= OBJECT_SETUP) {
        // Voice groups are immutable once built: the audio thread reads them
        // lock-free. Growing the pool at runtime is an explicit non-goal (§1);
        // reject rather than race the reader.
        INTERNAL::LogImpl().emit(E_WARNING, "SYNTH: addVoices() after setup is not supported");
        return;
      }
      pendingGroups.push_back({prototype, numVoices, channel, lowestNote, highestNote});
    }

    void implementationObject::setNoteCallback(NoteCallback func) {
      // Release-store so the audio thread's acquire-load sees a fully-published
      // function pointer. nullptr simply disables the hook.
      noteCallback.store(func, std::memory_order_release);
    }

    int implementationObject::getNumVoices() const {
      return numVoicesTotal.load(std::memory_order_relaxed);
    }

    Flt implementationObject::getChannelPitchWheel(int channel) const {
      return channels[channelIndex(channel)].pitchWheel;
    }

    Flt implementationObject::getChannelController(int channel, int number) const {
      if (number < 0 || number > 127) return 0.f;
      return channels[channelIndex(channel)].controller[static_cast<size_t>(number)];
    }

    Flt implementationObject::getChannelAftertouch(int channel) const {
      return channels[channelIndex(channel)].aftertouch;
    }

    bool implementationObject::getSustain(int channel) const {
      return channels[channelIndex(channel)].sustain;
    }

    bool implementationObject::getSostenuto(int channel) const {
      return channels[channelIndex(channel)].sostenuto;
    }

    bool implementationObject::getSoftPedal(int channel) const {
      return channels[channelIndex(channel)].softPedal;
    }

    DSP::dspSourceObject& implementationObject::getOutputSource() {
      return output;
    }

    // ---- lifecycle --------------------------------------------------------

    bool implementationObject::tryClaimForSetup() {
      OBJECT_IMPLEMENTATION_STATE expected = OBJECT_CREATED;
      return objectStatus.compare_exchange_strong(expected, OBJECT_SETTING_UP);
    }

    void implementationObject::setup() {
      // Runs on the single-threaded setup pool — the ONLY place voices are
      // allocated. clone() does the user's `new`.
      std::scoped_lock lk(buildMutex);
      for (auto& req : pendingGroups) {
        voiceGroup g;
        g.channel = req.channel;
        g.lowNote = req.lowNote;
        g.highNote = req.highNote;
        g.slots.resize(static_cast<size_t>(req.numVoices)); // default: free (note -1, STOPPED)
        g.voices.reserve(static_cast<size_t>(req.numVoices));
        for (int k = 0; k < req.numVoices; ++k) {
          g.voices.emplace_back(req.prototype->clone());
        }
        groups.push_back(std::move(g));
      }
      pendingGroups.clear();

      int total = 0;
      for (auto& g : groups)
        total += static_cast<int>(g.voices.size());
      numVoicesTotal.store(total, std::memory_order_relaxed);

      stealFadeSamples = std::max(1, static_cast<int>(SAMPLERATE * kStealFadeSec));

      // Publish groups to the audio thread. Storing inside the lock closes the
      // window where an addVoiceGroup() could append a request this setup will
      // never build (it is serialised on buildMutex and now sees OBJECT_SETUP).
      objectStatus.store(OBJECT_SETUP, std::memory_order_release);
    }

    bool implementationObject::readyCheck() {
      if (objectStatus.load(std::memory_order_acquire) == OBJECT_SETUP) {
        objectStatus.store(OBJECT_READY, std::memory_order_release);
        return true;
      }
      return false;
    }

    void implementationObject::sync() {
      // Audio-thread lifecycle tick. The note inbox is drained in
      // renderBlock(), not here; sync() only watches for interface teardown.
      if (head.load(std::memory_order_acquire) == nullptr) {
        objectStatus.store(OBJECT_RELEASE, std::memory_order_release);
      }
    }

    void implementationObject::removeInterface() {
      head.store(nullptr, std::memory_order_release);
    }

    OBJECT_IMPLEMENTATION_STATE implementationObject::getStatus() {
      return objectStatus.load(std::memory_order_acquire);
    }

    void implementationObject::setStatus(OBJECT_IMPLEMENTATION_STATE value) {
      objectStatus.store(value, std::memory_order_release);
    }

    // ---- audio-thread render path -----------------------------------------

    void implementationObject::renderBlock(SOUND_STATUS& masterIntent) {
      const OBJECT_IMPLEMENTATION_STATE st = objectStatus.load(std::memory_order_acquire);
      const bool ready = (st == OBJECT_SETUP || st == OBJECT_READY);

      // 1. Drain the inbox so every note event for this block takes effect
      //    before any audio is produced. Events arriving before the synth is
      //    ready are discarded (they have no group to target yet).
      messageObject m;
      while (messages.try_pop(m)) {
        if (ready) parseMessage(m);
      }

      // 2. Clear the aggregate.
      for (auto& b : output.samples)
        b = 0.f;

      if (!ready) return;

      // 3. Honour the sound-level master intent as a gate over the whole pool.
      switch (masterIntent) {
      case SS_WANTSTOPLAY:
      case SS_WANTSTORESTART:
        masterIntent = SS_PLAYING;
        break;
      case SS_PLAYING:
      case SS_PLAYING_FULL_VOLUME:
        break;
      case SS_WANTSTOSTOP:
        // Hard cut: the owning sound has already faded to silence via its own
        // fader by the time it reaches WANTSTOSTOP, so freeing here is
        // click-free. Frees the whole pool.
        freeAllVoices();
        masterIntent = SS_STOPPED;
        return;
      case SS_WANTSTOPAUSE:
        masterIntent = SS_PAUSED;
        return;
      default: // SS_STOPPED / SS_PAUSED — stay silent, keep voice state.
        return;
      }

      // 4. Render + mix every active voice.
      const int blockLen = static_cast<int>(output.samples[0].getLength());
      for (auto& g : groups) {
        for (size_t i = 0; i < g.slots.size(); ++i) {
          voiceSlot& slot = g.slots[i];
          dspVoice* v = g.voices[i].get();

          if (slot.stealing) {
            // Keep rendering the OLD note, faded out by the engine ramp.
            v->process(slot.intent);
            mixVoiceRamp(*v, slot);
            slot.stealPos += blockLen;
            if (slot.stealPos >= stealFadeSamples) {
              // Fade complete: re-arm this slot with the new note. It begins on
              // the next block — the small (~5 ms) latency a stolen note pays.
              slot.stealing = false;
              slot.stealPos = 0;
              slot.note = slot.pendNote;
              slot.channel = slot.pendChannel;
              slot.age = slot.pendAge;
              slot.keyDown = true; // the new note's key is down
              slot.heldBySustain = false;
              slot.heldBySostenuto = false;
              v->frequency(static_cast<Flt>(slot.pendNote));
              v->velocity(slot.pendVelocity);
              primeVoicePitchWheel(*v, slot.pendChannel); // start in tune (§5)
              slot.intent = SS_WANTSTOPLAY;
            }
          } else if (slot.intent != SS_STOPPED) {
            v->process(slot.intent); // may settle the intent to SS_STOPPED
            mixVoice(*v, 1.f);
            if (slot.intent == SS_STOPPED) {
              slot.note = -1; // release tail finished — slot is free again
            }
          }
        }
      }
    }

    void implementationObject::mixVoice(dspVoice& voice, Flt gain) {
      Flt* dst = output.samples[0].getPtr();
      const UInt len = output.samples[0].getLength();
      for (auto& vb : voice.samples) {
        Flt* src = vb.getPtr();
        const UInt n = std::min(len, vb.getLength());
        for (UInt s = 0; s < n; ++s)
          dst[s] += src[s] * gain;
      }
    }

    void implementationObject::mixVoiceRamp(dspVoice& voice, voiceSlot& slot) {
      Flt* dst = output.samples[0].getPtr();
      const UInt len = output.samples[0].getLength();
      const Flt fade = static_cast<Flt>(stealFadeSamples);
      for (auto& vb : voice.samples) {
        Flt* src = vb.getPtr();
        const UInt n = std::min(len, vb.getLength());
        for (UInt s = 0; s < n; ++s) {
          Flt g = 1.f - static_cast<Flt>(slot.stealPos + static_cast<int>(s)) / fade;
          if (g < 0.f) g = 0.f;
          dst[s] += src[s] * g;
        }
      }
    }

    void implementationObject::freeAllVoices() {
      for (auto& g : groups) {
        for (auto& slot : g.slots) {
          slot.intent = SS_STOPPED;
          slot.note = -1;
          slot.stealing = false;
          slot.stealPos = 0;
          slot.keyDown = false;
          slot.heldBySustain = false;
          slot.heldBySostenuto = false;
        }
      }
    }

    // ---- keyboard / allocator ---------------------------------------------

    void implementationObject::parseMessage(const messageObject& message) {
      switch (message.ID) {
      case NOTE_ON:
        handleNoteOn(message.noteOn.channel, message.noteOn.note, message.noteOn.velocity);
        break;
      case NOTE_OFF:
        handleNoteOff(message.noteOff.channel, message.noteOff.note);
        break;
      case ALL_NOTES_OFF:
        handleAllNotesOff(message.allOff.channel);
        break;
      case PITCH_WHEEL:
        handlePitchWheel(message.wheel.channel, message.wheel.value);
        break;
      case CONTROLLER:
        handleController(message.cc.channel, message.cc.number, message.cc.value);
        break;
      case AFTERTOUCH:
        handleAftertouch(message.touch.channel, message.touch.note, message.touch.value);
        break;
      case SUSTAIN:
        handleSustain(message.pedal.channel, message.pedal.down);
        break;
      case SOSTENUTO:
        handleSostenuto(message.pedal.channel, message.pedal.down);
        break;
      case SOFTPEDAL:
        handleSoftPedal(message.pedal.channel, message.pedal.down);
        break;
      }
    }

    void implementationObject::handleNoteOn(int channel, int note, Flt velocity) {
      // §5 step 1: let the onNoteEvent hook rewrite note/velocity in flight,
      // before any keyboard bookkeeping or allocation. The rewritten values are
      // what the held-note state and allocator use.
      float n = static_cast<float>(note);
      float v = velocity;
      NoteCallback cb = noteCallback.load(std::memory_order_acquire);
      if (cb != nullptr) cb(true, &n, &v);
      note = static_cast<int>(std::lround(n));
      velocity = v;

      // §5 step 2: soft pedal scales the velocity of notes started while it is
      // held (it never retroactively changes sounding voices).
      if (channelFor(channel).softPedal) velocity *= kSoftPedalGain;

      // §5 steps 3-4: allocate into every matching group; each allocation primes
      // the new voice with the channel's current pitch-wheel value.
      for (auto& g : groups) {
        if (groupMatches(g, channel, note)) {
          allocateInGroup(g, channel, note, velocity);
        }
      }
    }

    void implementationObject::handleNoteOff(int channel, int note) {
      // §5 step 1: the hook may rewrite the note to the same value a matching
      // NOTE_ON produced, so the release finds the right voice.
      float n = static_cast<float>(note);
      float v = 0.f;
      NoteCallback cb = noteCallback.load(std::memory_order_acquire);
      if (cb != nullptr) cb(false, &n, &v);
      note = static_cast<int>(std::lround(n));

      // The key is up now. Whether the voice actually releases depends on the
      // pedals (§5): sustain defers all releases; sostenuto defers only notes it
      // captured. A deferred voice keeps sounding until the pedal lets go.
      channelState& cs = channelFor(channel);
      for (auto& g : groups) {
        for (auto& slot : g.slots) {
          if (slot.stealing || slot.note != note || slot.channel != channel || !slot.keyDown)
            continue;
          slot.keyDown = false;
          if (cs.sustain) slot.heldBySustain = true;
          // heldBySostenuto is already set (or not) from when the pedal went
          // down; a note played after sostenuto engaged is not captured.
          releaseIfUnheld(slot);
        }
      }
    }

    void implementationObject::handleAllNotesOff(int channel) {
      // A bulk, unconditional release of every sounding voice on the channel(s)
      // (§4): voices enter their normal release tail rather than being cut. Any
      // pedal claim is dropped so nothing lingers.
      for (auto& g : groups) {
        for (auto& slot : g.slots) {
          if (slot.stealing || slot.intent == SS_STOPPED || slot.intent == SS_WANTSTOSTOP) continue;
          if (channel == 0 || slot.channel == channel) {
            slot.keyDown = false;
            slot.heldBySustain = false;
            slot.heldBySostenuto = false;
            slot.intent = SS_WANTSTOSTOP;
          }
        }
      }
    }

    void implementationObject::releaseIfUnheld(voiceSlot& slot) {
      // A voice releases only once its key is up and no pedal still claims it.
      if (!slot.keyDown && !slot.heldBySustain && !slot.heldBySostenuto && !slot.stealing &&
          slot.intent != SS_STOPPED && slot.intent != SS_WANTSTOSTOP) {
        slot.intent = SS_WANTSTOSTOP;
      }
    }

    // ---- keyboard: pitch wheel / controllers / aftertouch / pedals --------

    void implementationObject::primeVoicePitchWheel(dspVoice& voice, int channel) {
      voice._pitchWheel.store(channelFor(channel).pitchWheel, std::memory_order_relaxed);
    }

    void implementationObject::handlePitchWheel(int channel, Flt value) {
      channelFor(channel).pitchWheel = value;
      // Forward to every voice currently sounding on this channel so a bend
      // moves the notes already down, not just future ones (§5).
      for (auto& g : groups) {
        for (size_t i = 0; i < g.slots.size(); ++i) {
          voiceSlot& s = g.slots[i];
          if (s.channel == channel && s.intent != SS_STOPPED) {
            g.voices[i]->_pitchWheel.store(value, std::memory_order_relaxed);
          }
        }
      }
    }

    void implementationObject::handleController(int channel, int number, Flt value) {
      // CC 64/66/67 ARE the pedals — intercept them (§5/§6). A raw CC crosses
      // the same threshold the typed pedal setters use.
      switch (number) {
      case 64:
        handleSustain(channel, value >= kPedalThreshold);
        return;
      case 66:
        handleSostenuto(channel, value >= kPedalThreshold);
        return;
      case 67:
        handleSoftPedal(channel, value >= kPedalThreshold);
        return;
      default:
        break;
      }
      // Every other CC is stored as the channel's last value for that number.
      // The core does not itself map CCs to synthesis parameters (that is the
      // instrument/modulation-matrix concern, #148); it only records them.
      if (number >= 0 && number <= 127) {
        channelFor(channel).controller[static_cast<size_t>(number)] = value;
      }
    }

    void implementationObject::handleAftertouch(int channel, int note, Flt value) {
      channelFor(channel).aftertouch = value; // last channel pressure
      // note == -1 broadcasts to every voice on the channel; otherwise it
      // reaches only the voice(s) sounding that note (§5).
      for (auto& g : groups) {
        for (size_t i = 0; i < g.slots.size(); ++i) {
          voiceSlot& s = g.slots[i];
          if (s.channel != channel || s.intent == SS_STOPPED) continue;
          if (note == -1 || s.note == note) {
            g.voices[i]->_aftertouch.store(value, std::memory_order_relaxed);
          }
        }
      }
    }

    void implementationObject::handleSustain(int channel, bool down) {
      channelState& cs = channelFor(channel);
      cs.sustain = down;
      if (down) return; // pedal down: future NOTE_OFFs defer — nothing to do now
      // Pedal up: drop the sustain claim on every voice on this channel; any
      // whose key is up (and not still held by sostenuto) releases now.
      for (auto& g : groups) {
        for (auto& slot : g.slots) {
          if (slot.channel == channel && slot.heldBySustain) {
            slot.heldBySustain = false;
            releaseIfUnheld(slot);
          }
        }
      }
    }

    void implementationObject::handleSostenuto(int channel, bool down) {
      channelState& cs = channelFor(channel);
      cs.sostenuto = down;
      if (down) {
        // Snapshot the currently-held keys: only these voices are captured.
        for (auto& g : groups) {
          for (auto& slot : g.slots) {
            if (slot.channel == channel && slot.keyDown) slot.heldBySostenuto = true;
          }
        }
        return;
      }
      // Pedal up: release any captured voice whose key is no longer held (unless
      // sustain still holds it); clear the capture.
      for (auto& g : groups) {
        for (auto& slot : g.slots) {
          if (slot.channel == channel && slot.heldBySostenuto) {
            slot.heldBySostenuto = false;
            releaseIfUnheld(slot);
          }
        }
      }
    }

    void implementationObject::handleSoftPedal(int channel, bool down) {
      // Only affects the velocity of FUTURE notes — no effect on sounding voices.
      channelFor(channel).softPedal = down;
    }

    void implementationObject::allocateInGroup(voiceGroup& group, int channel, int note,
                                               Flt velocity) {
      // 1. Prefer a free (STOPPED) slot.
      for (size_t i = 0; i < group.slots.size(); ++i) {
        voiceSlot& s = group.slots[i];
        if (!s.stealing && s.intent == SS_STOPPED) {
          s.note = note;
          s.channel = channel;
          s.age = ++ageCounter;
          s.keyDown = true;
          s.heldBySustain = false;
          s.heldBySostenuto = false;
          group.voices[i]->frequency(static_cast<Flt>(note));
          group.voices[i]->velocity(velocity);
          primeVoicePitchWheel(*group.voices[i], channel); // start in tune (§5)
          s.intent = SS_WANTSTOPLAY;
          return;
        }
      }

      // 2. No free slot — steal. Prefer the oldest voice already in release;
      //    else the oldest voice overall. Slots mid-steal are not candidates.
      int stealIdx = -1;
      uint64_t bestAge = UINT64_MAX;
      for (size_t i = 0; i < group.slots.size(); ++i) {
        voiceSlot& s = group.slots[i];
        if (!s.stealing && s.intent == SS_WANTSTOSTOP && s.age < bestAge) {
          bestAge = s.age;
          stealIdx = static_cast<int>(i);
        }
      }
      if (stealIdx < 0) {
        bestAge = UINT64_MAX;
        for (size_t i = 0; i < group.slots.size(); ++i) {
          voiceSlot& s = group.slots[i];
          if (!s.stealing && s.age < bestAge) {
            bestAge = s.age;
            stealIdx = static_cast<int>(i);
          }
        }
      }
      if (stealIdx < 0) {
        // Every voice is already mid-steal; drop rather than click.
        return;
      }

      // Begin the click-free steal: keep the old note rendering, fade it out,
      // and record the new note to arm when the fade completes.
      voiceSlot& s = group.slots[static_cast<size_t>(stealIdx)];
      s.stealing = true;
      s.stealPos = 0;
      s.pendNote = note;
      s.pendChannel = channel;
      s.pendVelocity = velocity;
      s.pendAge = ++ageCounter;
    }

  } // namespace SYNTH
} // namespace YSE
