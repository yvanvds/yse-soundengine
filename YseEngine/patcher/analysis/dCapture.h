#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../io/fileScheduler.h"
#include "../pObject.h"
#include "../../dsp/analysis/sampleCapture.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~capture``: stores signal samples and sends them out as
     *         lists, wrapping ``DSP::sampleCapture`` (issue #1128).
     *
     *  Ported from Max's ``capture~``, which shows what it stored in a text
     *  window. Here the engine owns the store and the patch reads it: a
     *  ``dump`` sends it out as lists. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet: the signal to store, or a number used while no signal is
     *    connected (#1036). A bang or ``dump`` sends what is stored;
     *    ``clear`` empties the store; ``write <file>`` saves it as text,
     *    one value per line.
     *  - Outlet 0: on a dump, the stored values oldest first, as lists of
     *    at most ``AtomList::MAX_ATOMS`` values and
     *    ``AtomList::TEXT_CAPACITY`` characters, so any list object holds a
     *    whole one.
     *  - Outlet 1: on a dump, how many values follow, before the first
     *    list.
     *  - Parameter: ``capture``, the creation arguments as Max's: an
     *    optional ``f`` to keep the first samples instead of the most recent
     *    ones, the number of samples to keep (default 4096), then up to 10
     *    positions in each block to store instead of every sample: ``~capture
     *    f 1000 0 64``. Changing them rebuilds the object, so the store is
     *    allocated where no audio thread runs it. The stored samples are not
     *    saved with the patch: ``write`` saves them.
     *
     *  Dropped from Max: ``open``, ``wclose`` and ``mouse``, which drive
     *  Max's text window; a host draws from the dump.
     *
     *  Real time: the store and a snapshot of it are allocated when the
     *  object is built. A message only sets an atomic flag (``write`` also
     *  hands its path over in a slot the senders take with a non-blocking
     *  flag), read at the start of the next block on the audio thread.
     *  ``clear`` applies before that block is stored. A dump copies the
     *  store, as it is at the end of that block, into the snapshot and sends
     *  it from the flush after the traversal (issue #1037), on the audio
     *  thread, at most ``DUMP_VALUES_PER_BLOCK`` values per block, so a
     *  large store goes out over several blocks while recording goes on. A
     *  dump asked for during one starts when it ends. A ``write``'s text is
     *  built in the flush into a buffer reserved here and handed to the
     *  patcher's file scheduler (issue #683). A write that does not fit
     *  ``fileScheduler::BYTES_CAPACITY``, a path too long, a sender that
     *  loses the slot or a write already waiting is refused and counted in
     *  ``Dropped()``. Values that are not finite print as ``0.``.
     */
    PATCHER_CLASS(dCapture, YSE::OBJ::D_CAPTURE)
    _DO_MESSAGES
    _DO_CALCULATE
    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Dump)
    _FLOAT_IN(SetIn)
    _INT_IN(SetInInt)

    void SetParent(pObject* newParent) override;
    void FlushArmedMessages(YSE::THREAD thread) override;

    /** @brief The most values a dump sends per block. */
    static constexpr UInt DUMP_VALUES_PER_BLOCK = 4096;

    /** @brief Messages refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The DSP class. Diagnostics and tests; audio thread only once
     *         it renders. */
    const DSP::sampleCapture& Store() const {
      return fn;
    }

  private:
    void RequestWrite(const char* text, std::size_t length, std::size_t pos);
    bool Save();

    DSP::sampleCapture fn;
    std::vector<std::string> arguments;

    aFlt in{0.f};
    std::atomic<bool> dumpRequested{false};
    std::atomic<bool> clearRequested{false};
    std::atomic<unsigned int> dropped{0};

    // The write's path, handed from a sender to the audio thread. A sender
    // fills it under `writing` while `writeRequested` is false; the audio
    // thread reads it while it is true.
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<bool> writeRequested{false};
    char pathSlot[fileScheduler::PATH_CAPACITY] = {};
    std::size_t pathLength = 0;

    // The audio thread's.
    std::vector<Flt> snapshot; // the store at the dump, sized with it
    UInt dumpAt = 0; // the next snapshot value to send
    UInt dumpEnd = 0; // how many the snapshot holds
    bool dumping = false;
    bool countPending = false;
    bool writePending = false;
    std::string writePath; // reserved
    std::string fileText; // reserved, a write's text
    std::string scratch; // reserved, one list out
  };

} // namespace PATCHER
} // namespace YSE
