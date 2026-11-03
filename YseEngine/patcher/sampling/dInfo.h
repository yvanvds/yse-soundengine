#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~info``: reports what a named sample buffer holds, through
     *         ``DSP::sampleBuffer::info`` (issue #1138).
     *
     *  Ported from Max's ``info~``. A control object: it reads a buffer's
     *  details, never its samples.
     *
     *  - Argument: ``buffer-name``, the buffer to report on.
     *  - Inlet 0: ``bang`` reports; ``set <name>`` names another buffer
     *    (``set`` alone names none).
     *  - Outlets, sent right to left: 0 the sample rate, 1 the instrument
     *    data as ``base-note detune key-low key-high velocity-low
     *    velocity-high gain``, 2 and 3 the sustain loop's start and end in
     *    ms, 4 and 5 the release loop's, 6 the length in ms, 7 the file's
     *    name, 8 its channel count, 9 its path. What the buffer does not
     *    have (no file read, no instrument data, a loop the file does not
     *    define) is not sent. Without a file, outlet 8 is the buffer's own
     *    channel count.
     *
     *  ### A bang is background work
     *
     *  Looking a buffer up by name and reading its details both take a
     *  lock, and a message handler may run on the audio thread. So a bang
     *  packs the name into a fixed record and hands it to the patcher's
     *  ``fileScheduler`` as a ``TASK``; the task looks the buffer up with
     *  ``sampleBufferRegistry::find`` on the background pool and writes the
     *  answer into the record, and the outlets fire at the top of a later
     *  block. The task runs after any ``~buffer`` task sent before it in the
     *  same patcher, so ``read`` then ``bang`` reports the new file. The
     *  object holds no buffer: ``set`` only copies a name, and a buffer that
     *  appears after this object is found at the next bang.
     *
     *  Limits that come from the scheduler, as for ``~buffer``: at most
     *  ``fileScheduler::CAPACITY`` requests in flight per patcher, results
     *  only while the patcher renders, nothing outside a patcher. A path
     *  longer than ``fileScheduler::PATH_CAPACITY - 1`` is not sent.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window).
     */
    PATCHER_CLASS(dInfo, YSE::OBJ::D_INFO)
    _DO_MESSAGES
    _NO_CALCULATE
    _BANG_IN(Bang)

    _PARM_CLEAR
    _PARM_PARSE

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief The name a bang reports on, empty for none. Tests. */
    const char* BufferName() const {
      return name;
    }

    /** @brief Bangs that found no buffer of the name. Diagnostics and
     *         tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    // Hand a report request to the scheduler. Any thread, no allocation.
    void Request();

    // Copy `text` (of `length`) into `name`; false, and no change, when it
    // does not fit.
    bool SetName(const char* text, std::size_t length);

    // The fileTaskFunction: background pool, never touches the object.
    struct infoTask;
    static bool RunTask(char* args, std::size_t argBytes);

    std::string bufferName; // the argument

    // The buffer a bang reports on: the argument, or the last `set`. Written
    // by PARM_PARSE (before publication) and the message handlers only.
    char name[fileScheduler::PATH_CAPACITY] = {};

    // An outlet's text, built here so sending it allocates nothing: the
    // capacity is reserved at construction.
    std::string reply;

    std::atomic<std::uint64_t> failed{0};
  };

} // namespace PATCHER
} // namespace YSE
