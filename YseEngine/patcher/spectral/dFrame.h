#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/spectral/frameList.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~frame``: a list as a frame, wrapping ``DSP::frameSource``
     *         (issue #1163).
     *
     *  Ported from MSP's ``frame~``. The wrapper only maps the pins,
     *  messages and parameters onto the DSP class and tells it which bins a
     *  block holds:
     *
     *  - Inside a ``~pfft`` (``CurrentSpectralPass()`` not null), a block is
     *    a pass, bins ``firstBin`` .. ``+ count - 1`` of the spectral frame.
     *  - Anywhere else a block is a frame, as MSP treats the signal vector
     *    outside a ``pfft~``: sample ``i`` is value ``i``.
     *
     *  - Inlet 0: a list sets the values from the offset on (a number is a
     *    list of one); ``fill <value>`` sets what the bins no value was set
     *    for put out; ``mirror <0|1>`` mirrors the bins above Nyquist in a
     *    full-spectrum ``~pfft``. None of it is saved.
     *  - Inlet 1: the bin the next lists start at (default 0), so the lists
     *    ``~framesnap`` sends a large frame in, with their first bins on this
     *    inlet, build the frame up again.
     *  - Outlet: the frame, a signal.
     *  - Parameter: ``bins``, the most values kept (default 4096). Changing
     *    it rebuilds the object, so the storage is allocated on the control
     *    thread.
     *
     *  Dropped from MSP: ``anything`` (a list that starts with a word), for
     *  which there is nothing to set.
     *
     *  Real time: a message edits the sender's copy of the values and hands
     *  it to the audio thread through a triple buffer, so neither side ever
     *  waits for the other. The audio thread picks up the newest at the start
     *  of a frame (pass 0 inside a ``~pfft``, any block elsewhere), so a frame
     *  is never half old and half new. One message thread at a time is
     *  assumed, as for every object's messages.
     */
    PATCHER_CLASS(dFrame, YSE::OBJ::D_FRAME)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    /** @brief The values the audio thread puts out. Diagnostics and tests;
     *         audio thread only once it renders. */
    const DSP::frameSource& Source() const {
      return slots[front];
    }

  private:
    void Allocate(UInt capacity);
    // Hands the sender's copy to the audio thread.
    void Publish();

    static constexpr unsigned int DIRTY = 4;

    int bins = static_cast<int>(DSP::frameSource::DEFAULT_CAPACITY);

    // The message thread's: its copy of the values, the next list's offset
    // and room to read a list into.
    DSP::frameSource staged;
    UInt offset = 0;
    std::vector<Flt> scratch;

    // The triple buffer: the audio thread reads slots[front], the message
    // thread fills slots[back], and `middle` holds the third, with DIRTY set
    // while it is newer than the audio thread's.
    DSP::frameSource slots[3];
    unsigned int front = 0;
    unsigned int back = 2;
    std::atomic<unsigned int> middle{1};
  };

  /**
   *  @brief ``~framesnap``: a frame as lists, wrapping ``DSP::frameSnapshot``
   *         (issue #1163).
   *
   *  Ported from MSP's ``framesnap~``. Frames as for ``~frame``: inside a
   *  ``~pfft`` its spectral frames, anywhere else every block.
   *
   *  - Inlet 0: the frames, a signal (unconnected, 0). A bang reports the
   *    last whole frame; a non-zero number starts interval reporting, 0
   *    stops it. Also takes ``start`` (with no interval, a bang), ``stop``,
   *    ``active <0|1>``, ``interval <ms>`` and ``sampleinterval
   *    <samples>``.
   *  - Inlet 1: the interval in milliseconds; 0 stops interval reporting.
   *  - Outlet 0: a report, the frame's values bin by bin, as lists of at
   *    most ``AtomList::MAX_ATOMS`` values and ``AtomList::TEXT_CAPACITY``
   *    characters, so any list object holds a whole one.
   *  - Outlet 1: before each list, the bin it starts at.
   *  - Parameters: ``interval`` in ms (MSP's argument and attribute,
   *    default 0: only a bang reports) and ``bins``, the largest frame kept
   *    (default 4096). Changing one rebuilds the object.
   *
   *  The interval counts the time the frames stand for: a block outside a
   *  ``~pfft``, the hop inside one. Several reports due within one block
   *  are one, as for ``~snapshot``. A bang or ``start`` reports the frame
   *  whole at the end of the next block (inside a ``~pfft``, of the next
   *  frame).
   *
   *  Real time: a report copies the frame into a snapshot reserved with the
   *  storage and sends it from the flush after the traversal (issue #1037),
   *  on the audio thread, at most ``VALUES_PER_BLOCK`` values per block, so
   *  a larger frame goes out over several blocks; a report due while one is
   *  going out is skipped (a bang waits for it to end). Not ported: the Max
   *  time formats of the interval (``list`` and ``anything``), because YSE
   *  has no transport time format.
   */
  PATCHER_CLASS(dFramesnap, YSE::OBJ::D_FRAMESNAP)
  _DO_MESSAGES
  _DO_CALCULATE

  _PARM_CLEAR
  _PARM_PARSE

  _BANG_IN(Report)
  _FLOAT_IN(SetFloat)
  _INT_IN(SetInt)

  void FlushArmedMessages(YSE::THREAD thread) override;

  /** @brief The most values a report sends per block. */
  static constexpr UInt VALUES_PER_BLOCK = 4096;

  /** @brief The DSP class. Diagnostics and tests; audio thread only once it
   *         renders. */
  const DSP::frameSnapshot& Snapshot() const {
    return fn;
  }

private:
  // Time passes by `samples`; a report starts when one is due.
  void Tick(double samples);

  DSP::frameSnapshot fn;

  aFlt interval{0.f}; // ms; used while sampleInterval is 0
  int bins = static_cast<int>(DSP::frameSnapshot::DEFAULT_CAPACITY);
  aInt sampleInterval{0}; // samples; overrides interval when > 0
  std::atomic<bool> active{true};
  std::atomic<bool> reportRequested{false};

  // The audio thread's.
  double samplesLeft = 0.0; // to the next interval report
  std::vector<Flt> report; // the frame reported, sized with the storage
  UInt reportSize = 0;
  UInt reportAt = 0; // the next value to send
  bool reporting = false;
  std::string scratch; // reserved, one list out
};

} // namespace PATCHER
} // namespace YSE
