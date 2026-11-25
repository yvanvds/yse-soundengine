#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/sfzSampler.hpp"
#include "../../utils/mpmcQueue.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sfizz``: plays an SFZ instrument from MIDI-style notes,
     *         wrapping ``DSP::sfzSampler`` (issue #1159).
     *
     *  Ported from Max's ``sfizz~``, on the engine's own SFZ model
     *  (``dsp/sfzModel.hpp`` and ``SYNTH::samplerVoice``) rather than the
     *  sfizz library. The wrapper only maps pins, messages and arguments
     *  onto the DSP class.
     *
     *  - Arguments: ``filename voices volume base ref mid``: Max's argument
     *    and its attributes of those names. The file may be left out: a
     *    first argument that is a number is the voice count.
     *  - Inlet 0: an int is a key, played at the velocity inlet 1 holds (0 a
     *    note off); a float is truncated to one; a list is ``key velocity``.
     *    The words ``read <file>``, ``readagain``, ``midievent <bytes>``,
     *    ``voices``, ``volume``, ``base``, ``ref``, ``mid``, ``scale`` and
     *    ``scalename <file.scl>`` arrive there too.
     *  - Inlet 1: the velocity, 0 to 127, kept for the next key.
     *  - Outlets 0 and 1: the left and right signals.
     *
     *  ### The disk is the background's
     *
     *  ``read``, ``readagain``, ``voices`` and ``scalename`` copy their
     *  words into a fixed record handed to the patcher's ``fileScheduler``
     *  (wait-free, no allocation); file names go through the host's path
     *  resolver (issue #1255) when one is installed. The task finds the
     *  sampler by id, so it survives the object's deletion, and calls the
     *  class's control side, which parses the instrument, decodes its
     *  samples, builds the voices and hands them to the audio thread. A
     *  ``.scl`` file is read and parsed in its task, its degrees handed back
     *  in the reply. The pool the audio thread let go is freed by a further
     *  task. The file argument loads when the object joins its patcher (on
     *  the control thread, or as a task while the host resolves paths), as
     *  ``~buffer``'s does.
     *
     *  ### Notes on any thread
     *
     *  Keys and ``midievent`` go into a lock-free queue (``QUEUE_CAPACITY``;
     *  a full queue drops, counted in ``DroppedEvents``) that the next block
     *  plays, in order, once no file task is in flight, so ``read
     *  piano.sfz, 60`` plays the piano. ``volume``, ``base``, ``ref``,
     *  ``mid`` and ``scale`` are atomics the next block hands to the class.
     *
     *  **Saving.** The arguments are saved, and the file a ``read`` loaded
     *  in place of the argument's; the other messages are not.
     *
     *  Not ported: ``dictionary``, ``pathmode`` and ``defaultpathname``
     *  (Max's dictionary form of an instrument; YSE reads .sfz files and
     *  resolves their names through the host), ``map`` / ``mapname`` (Scala
     *  keyboard maps; ``mid``, ``ref`` and ``base`` place a scale on the
     *  keys), ``outputs`` (the SFZ model has no ``output`` opcode: one
     *  stereo pair), ``silent`` (the object posts nothing; the parser's
     *  warnings go to the engine log), ``drag`` and ``mouse`` (the host's),
     *  ``read`` without a name (a dialog, the host's) and the signal inlet
     *  for audio-rate MIDI (no such source in YSE).
     */
    PATCHER_CLASS(dSfizz, YSE::OBJ::D_SFIZZ)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _INT_IN(SetKey)
    _FLOAT_IN(SetKeyFloat)
    _LIST_IN(SetList)
    _INT_IN(SetVelocity)
    _FLOAT_IN(SetVelocityFloat)

    ~dSfizz() override;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    /** @brief Note events waiting for the next block, at most. */
    static constexpr std::size_t QUEUE_CAPACITY = 256;

    /** @brief The DSP class. Its audio-thread state only while no block
     *         runs (tests). */
    DSP::sfzSampler& Sampler() {
      return *sampler;
    }

    /** @brief Requests refused by the scheduler, or whose file could not be
     *         read. Diagnostics and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

    /** @brief Note events lost to a full queue. Diagnostics and tests. */
    std::uint64_t DroppedEvents() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    struct sfTask;
    // One key or MIDI message on its way to the next block.
    struct noteEvent {
      std::int32_t status = 0; // 0: a key at `velocity`; else a MIDI status byte
      std::int32_t data1 = 0;
      std::int32_t data2 = 0;
      float velocity = 0.f; // 0 to 1, for a key
    };

    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    bool Submit(sfTask& task, bool resolve);
    // A task naming the file after the word ending at `pos`.
    bool FileTask(int op, const char* text, std::size_t length, std::size_t pos);
    void Push(const noteEvent& event);
    void Key(int key, float velocity127);
    void Midievent(const char* text, std::size_t length, std::size_t pos);
    void Scale(const char* text, std::size_t length, std::size_t pos);
    void LoadArgumentFile();

    std::shared_ptr<DSP::sfzSampler> sampler; // made at construction, never replaced
    std::uint64_t id = 0; // how a task finds the sampler

    // Arguments. `filename` is written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    std::string filename;
    aInt voices;
    aFlt volume;
    aFlt base;
    aInt ref;
    aInt mid;
    std::string pendingFile; // the argument's file, until SetParent loads it

    aFlt velocity; // inlet 1, 0 to 127

    // The scale for the next block: `degreeCount` values of `degrees`,
    // published by a bump of `scaleVersion`. The last writer wins.
    std::array<aFlt, DSP::sfzSampler::MAX_DEGREES> degrees{};
    aInt degreeCount;
    std::atomic<std::uint32_t> scaleVersion{0};
    std::uint32_t appliedScale = 0; // the audio thread's

    YSE::mpmcQueue<noteEvent> events{QUEUE_CAPACITY};

    // File tasks requested and not yet delivered, the ones notes wait for.
    std::atomic<int> inFlight{0};
    std::atomic<bool> collectQueued{false};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
