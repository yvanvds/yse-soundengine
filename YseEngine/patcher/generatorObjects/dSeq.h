#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../io/fileScheduler.h"
#include "../pObject.h"
#include "../../dsp/generators/eventSequencer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~seq``: messages stored at phase positions and sent when a
     *         driving phase crosses them, wrapping ``DSP::eventSequencer``
     *         (issue #1069).
     *
     *  Ported from Max's ``seq~``. Not the control ``.seq``, which plays
     *  MIDI files. The wrapper only maps pins and messages onto the DSP
     *  class.
     *
     *  - Inlet: the driving phase, a signal, normally a ``~phasor``
     *    (unconnected reads 0). A number, a list or any message that is not
     *    one of the commands below is an event: recorded into the current
     *    sequence at the current phase while ``record`` or ``overdub`` is
     *    on, ignored otherwise. Commands: ``add <seq> <phase> <message>``,
     *    ``clear [<seq>|all]`` (or ``erase``; the current sequence without
     *    an argument), ``delete <seq> <phase> [<to>] [<message>]``,
     *    ``dump [<seq>]``, ``offset <n>``, ``overdub <0|1>``,
     *    ``play <0|1>``, ``record <0|1>``, ``read <file>``, ``write
     *    <file>``, ``seq`` / ``seqnum <seq>``. A bang reports the state.
     *  - Outlets: the events as they fire, each the message it was stored
     *    as; ``dump``'s ``seq phase message`` lists; the state on a bang,
     *    ``seq play record overdub events-in-seq events-in-all``.
     *  - No parameters, as Max's ``seq~`` has no arguments. The sequences
     *    are not saved with the patch: ``write`` saves them.
     *
     *  Everything leaves after the block's traversal (issue #1037), from the
     *  audio thread, right outlet first. The events of one block go in the
     *  order they fired; their sample offsets are the DSP class's
     *  (``fired()``), which a message cannot carry. ``dump``, the state and
     *  a ``write`` answer at the end of the next block, from the audio
     *  thread, because the sequences live there: a recorded event's phase is
     *  only known there.
     *
     *  Real time: every command and event is turned on the sending thread
     *  into a command in a queue allocated when the object is built, its
     *  text copied in, and applied in order at the start of the next block,
     *  so nothing allocates or locks. Senders take a non-blocking flag among
     *  themselves; a sender that loses it, a full queue or a malformed
     *  message is refused and counted in ``Dropped()``. Events crossed in
     *  one block beyond the DSP class's fired capacity are counted in
     *  ``Missed()``. ``read`` and ``write`` go through the patcher's file
     *  scheduler (issue #683): the file is read on the background pool and
     *  parsed into the sequences at the top of a block; a write's text is
     *  built on the audio thread into a buffer reserved here.
     */
    PATCHER_CLASS(dSeq, YSE::OBJ::D_SEQ)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Bang)
    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    void FlushArmedMessages(YSE::THREAD thread) override;

    /** @brief Messages refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Events crossed but not sent, the block's fired list being
     *         full. Diagnostics and tests. */
    unsigned int Missed() const {
      return missed.load(std::memory_order_relaxed);
    }

    /** @brief The DSP class. Diagnostics and tests; audio thread only once
     *         it renders. */
    const DSP::eventSequencer& Sequencer() const {
      return fn;
    }

  private:
    struct command {
      int op = 0;
      Int sequence = 0;
      Flt a = 0.f;
      Flt b = 0.f;
      UInt length = 0;
      char text[DSP::eventSequencer::TEXT_CAPACITY] = {};
    };

    void Command(const char* text, std::size_t length, std::size_t pos, std::size_t word,
                 THREAD thread);
    void Event(const char* text, std::size_t length);
    void Post(int op, Int sequence, Flt a, Flt b, const char* text, std::size_t length);
    void Load(const char* bytes, std::size_t count);
    bool Save();

    DSP::eventSequencer fn;

    std::vector<command> queue;
    std::atomic<UInt> head{0}; // written by the sender, under `writing`
    std::atomic<UInt> tail{0}; // written by the audio thread
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};
    std::atomic<unsigned int> missed{0};
    std::atomic<int> offset{0};

    // The audio thread's: what the flush sends after this block.
    bool dumpPending = false;
    bool dumpAll = true;
    Int dumpSequence = 0;
    bool statePending = false;
    bool writePending = false;
    std::string writePath; // reserved
    std::string fileText; // reserved, a write's text
    std::string scratch; // reserved, one message out
  };

} // namespace PATCHER
} // namespace YSE
