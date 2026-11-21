#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/fileRecorder.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sfrecord``: records signals to a sound file on disk,
     *         wrapping ``DSP::fileRecorder`` (issue #1156).
     *
     *  Ported from Max's ``sfrecord~``. The wrapper only maps pins, messages
     *  and arguments onto the DSP class.
     *
     *  - Arguments: ``channels buffersize beginramp endramp bitdepth dither
     *    quantization nchans sortloop``. ``channels`` and ``buffersize`` are
     *    Max's ``number-of-input-channels`` (1 to 64) and ``buffer-size``
     *    (the ring between the audio thread and the disk, in frames; 0 for
     *    ``DSP::fileRecorder::DEFAULT_RING_FRAMES``); the rest are Max's
     *    attributes of the same names.
     *  - Inlets 0 to ``channels - 1``: the signals, input ``i`` into file
     *    channel ``i + 1``; unconnected, silence. In inlet 0 a number other
     *    than 0 starts recording into the open file and 0 stops it, closing
     *    the file; the words ``open``, ``start``, ``stop``, ``record``,
     *    ``loop``, ``samptype``, ``bitdepth`` (``format``), ``beginramp``,
     *    ``endramp``, ``dither``, ``quantization``, ``nchans`` and
     *    ``sortloop`` arrive there too.
     *  - Outlet 0: the time since the recording started, in ms, held at its
     *    length after it stops. Outlet 1 (not in Max): the frames of the
     *    recording lost because the disk fell behind, sent when the count
     *    changes.
     *
     *  ### The disk is the background's
     *
     *  ``open`` (and ``start`` with a file) copy their words into a fixed
     *  record handed to the patcher's ``fileScheduler`` (wait-free, no
     *  allocation), the name through the host's path resolver (issue #1255)
     *  as a file written. The task finds the recorder by id, so it survives
     *  the object's deletion. A start waits until those tasks have finished,
     *  so ``open take.wav, 1`` records into take.wav. Each block queues a
     *  task that writes what the recorder holds while it holds something,
     *  one at a time; that task also closes the file after a stop.
     *
     *  **Saving.** The arguments are saved; messages are not.
     *
     *  Not ported: ``print`` (Max's status in its console; the outlet
     *  reports lost frames), ``resample`` (the engine has no band-limited
     *  resampler for a stream) and ``open`` without a name (a dialog, the
     *  host's).
     */
    PATCHER_CLASS(dSfrecord, YSE::OBJ::D_SFRECORD)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetRecord)
    _INT_IN(SetRecordInt)

    ~dSfrecord() override;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief The DSP class. Its audio-thread state only while no block
     *         runs (tests). */
    DSP::fileRecorder& Recorder() {
      return *recorder;
    }

    /** @brief The number of signal inlets. */
    int Channels() const {
      return channels;
    }

    /** @brief Requests refused by the scheduler, or whose file could not be
     *         created. Diagnostics and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    struct sfTask;
    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    void ShapePorts();
    void Apply();
    bool Submit(sfTask& task, bool resolve);
    void Post(int what, float ms = 0.f);
    // `open` / `start`: the file (and type, and channels) after the word
    // ending at `pos`. False without a file.
    bool OpenFrom(const char* text, std::size_t length, std::size_t pos, bool withChannels);

    std::shared_ptr<DSP::fileRecorder> recorder; // made at construction, never replaced
    std::uint64_t id = 0; // how a task finds the recorder
    std::array<DSP::buffer*, DSP::fileRecorder::MAX_CHANNELS> blocks{};

    // Arguments. `channels` and `bufferSize` are written by PARM_PARSE
    // only, before any audio thread sees the object.
    int channels = 1;
    int bufferSize = 0;
    aFlt beginRamp;
    aFlt endRamp;
    aInt bitDepth;
    aInt dither;
    aInt quantization;
    aInt nchans;
    aInt sortLoop;

    // What the next `open` makes: set by samptype and bitdepth.
    aInt sampleType;
    aInt byteSwap;

    // A start or stop for the next block without a file task in flight;
    // the last one wins.
    std::atomic<int> request{0};
    aFlt requestMs;

    std::atomic<int> inFlight{0};
    std::atomic<bool> drainQueued{false};
    std::atomic<std::uint64_t> failed{0};
    std::uint64_t lastDropped = 0; // the audio thread's
  };

} // namespace PATCHER
} // namespace YSE
