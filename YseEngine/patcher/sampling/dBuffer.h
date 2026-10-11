#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~buffer``: a named sample buffer, wrapping
     *         ``DSP::sampleBuffer`` (issue #1039).
     *
     *  Ported from Max's ``buffer~``. The buffer itself is a
     *  ``DSP::sampleBuffer`` in ``DSP::sampleBufferRegistry``, so the names
     *  are process-wide: every patcher, every ``~poly`` voice and the C++
     *  host see the same ``drums``. A ``~buffer`` with no name has a private
     *  buffer that nothing else can reach.
     *
     *  - Arguments: ``name [file] [duration-ms] [channels]``, as in Max. A
     *    number in the second place is the duration. The arguments are
     *    applied when the object is created, on the control thread: the
     *    size first, then the file (read synchronously, so a loaded patch
     *    has its samples). A ``~buffer`` with only a name adopts whatever
     *    the buffer already holds.
     *  - Messages (left inlet): ``read``, ``replace``, ``readagain``,
     *    ``write``, ``writewave``, ``writeaiff``, ``writeflac``, ``clear``,
     *    ``fill``, ``apply``, ``normalize``, ``setsize``, ``sizeinsamps``,
     *    ``crop``, ``crop_samples``, ``duplicate``, ``sr``, and the queries
     *    ``getsamps``, ``getsize``, ``getsr``, ``getchans``.
     *  - Outlet 0: a bang when a ``read``, ``replace`` or ``readagain`` has
     *    finished.
     *  - Outlet 1: the answer to a query, as ``samps 44100``, ``size
     *    1000.``, ``sr 44100.``, ``chans 2``.
     *
     *  ### Every message is background work
     *
     *  A message handler runs on whichever thread dispatched it, often the
     *  audio thread, and every one of these messages allocates (a change
     *  builds new storage) or touches the disk. So a handler only packs the
     *  message into a fixed record and hands it to the patcher's
     *  ``fileScheduler`` as a ``TASK`` (wait-free, no allocation). The
     *  background pool runs the tasks one at a time in the order they were
     *  sent, so ``clear`` then ``fill 1`` then ``getsamps`` happen in that
     *  order and the query sees both. The result (a bang, a query's answer)
     *  comes back at the top of a later block, like any file result.
     *
     *  The task finds the buffer by its ``id`` in the registry, never through
     *  this object, which a live edit may delete while the task runs.
     *
     *  Limits that come from the scheduler: at most
     *  ``fileScheduler::CAPACITY`` messages in flight per patcher (more are
     *  refused and counted in ``FileIO()->Dropped()``), results only while
     *  the patcher renders, and nothing at all for an object outside a
     *  patcher or with the engine stopped.
     *
     *  Not ported: ``set`` / ``name`` (renaming from a message would look a
     *  name up on the audio thread; change the argument instead), the
     *  display window (``open``, ``wclose``, ``bang``), ``import`` /
     *  ``importreplace`` (``read`` already reads every format the engine
     *  decodes), ``readraw``, ``samptype``, ``replacechannel``, ``trim``,
     *  ``clearlow``, ``enumerate``, ``printmodtime``, ``fill sinc``, and the
     *  file-format attributes (the format follows the message or the file's
     *  extension).
     */
    PATCHER_CLASS(dBuffer, YSE::OBJ::D_BUFFER)
    _DO_MESSAGES
    _NO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief The buffer this object names. Control thread, tests. */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return buffer;
    }

    /** @brief Tasks that ran and failed (a missing file, a crop outside the
     *         buffer). Diagnostics and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    // Control thread: bind `bufferName`, then apply the size and file
    // arguments.
    void Bind();
    void ApplyArguments();

    // Pack `task` and hand it to the scheduler. Any thread, no allocation.
    struct bufferTask;
    bool Submit(bufferTask& task);

    // The fileTaskFunction: background pool, never touches the object.
    static bool RunTask(char* args, std::size_t argBytes);

    std::string bufferName;
    std::vector<std::string> rest; // [file] [duration] [channels]

    std::shared_ptr<DSP::sampleBuffer> buffer;
    std::uint64_t bufferId = 0; // set on the control thread before publication

    // `readagain`: the last read or replace, as it was sent. Written by the
    // message handlers only.
    char lastPath[fileScheduler::PATH_CAPACITY] = {};
    float lastStart = 0.f;
    float lastDuration = -1.f;
    int lastChannels = -1;
    bool lastReplace = false;

    // A query's answer, built here so sending it allocates nothing: the
    // capacity is reserved at construction.
    std::string reply;

    std::atomic<std::uint64_t> failed{0};
  };

} // namespace PATCHER
} // namespace YSE
