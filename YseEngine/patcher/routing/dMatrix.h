#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/routing/mixMatrix.hpp"
#include "matrixControl.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~matrix``: a signal routing and mixing matrix, wrapping
     *         ``DSP::mixMatrix`` (issue #1134).
     *
     *  Ported from Max's ``matrix~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class; the connection messages go
     *  through ``matrixControl``, which hands them to the audio thread
     *  lock-free.
     *
     *  - Inlets 0 to N-1: the input signals (unconnected read silence). Inlet
     *    0 also takes the connection messages, which number inputs and
     *    outputs from 0: ``<in> <out> <gain> [<ramp ms>]`` (gain 0
     *    disconnects), ``connect <in> <out> [<out> ...] [<gain>]`` (a float
     *    as the last item is the gain, else the default gain),
     *    ``disconnect <in> <out> [<out> ...]``, ``clear``, ``ramp <ms>``
     *    (alias ``ramptime``), ``exclusive <0-3>``, ``dump``, ``dumptarget``
     *    and ``dumpconnections``. ``print`` and ``dictionary`` are accepted
     *    and do nothing: the patcher has no console, and a dictionary named
     *    in a message cannot be bound on the thread that delivers it (a
     *    dictionary is resolved by name on the control thread, when an object
     *    is created), nor is the format Max reads documented.
     *  - Outlets 0 to M-1: each output, the sum of the inputs connected to it.
     *  - Outlet M: the dumps, one ``<in> <out> <gain>`` list per crosspoint,
     *    by input and then by output, sent after the block in which the
     *    request was taken, at most ``DUMP_LINES_PER_BLOCK`` per block. A
     *    line has the form of the list that sets a crosspoint, so a dump fed
     *    back in after ``clear`` restores the routing. ``dump`` reports the
     *    current gain of every crosspoint that is connected or still fading
     *    out; ``dumptarget`` and ``dumpconnections`` the gain every connected
     *    crosspoint is heading for. A request during a dump waits for it.
     *  - Parameter: ``arguments``, the creation arguments: inputs and outputs
     *    (1-128 each, default 1) and the gain ``connect`` uses (default 1),
     *    with ``ramp <ms>`` (default 10) and ``exclusive <0-3>`` among them,
     *    as the patcher has no attribute syntax. The sizes rebuild the
     *    object, so the DSP class is only ever allocated on an object no
     *    audio thread is running. The arguments are saved; the connections
     *    and later messages are not.
     */
    PATCHER_CLASS(dMatrix, YSE::OBJ::D_MATRIX)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _LIST_IN(SetList)

    /** @brief The most dump lines sent after one block. */
    static constexpr UInt DUMP_LINES_PER_BLOCK = 256;

    /** @brief The number of signal inputs. */
    UInt Inputs() const {
      return fn.inputs();
    }

    /** @brief The number of signal outputs. */
    UInt Outputs() const {
      return fn.outputs();
    }

    /** @brief The DSP class, for tests. Audio thread only while running. */
    const DSP::mixMatrix& Matrix() const {
      return fn;
    }

    void FlushArmedMessages(YSE::THREAD thread) override;

  private:
    // Rebuild the DSP class, the control side and the pins from the creation
    // arguments. Control thread, before the object is wired or published.
    void ShapePorts();

    DSP::mixMatrix fn;
    matrixControl control;
    // This block's inputs, null for an unconnected one. Audio thread.
    std::array<DSP::buffer*, DSP::mixMatrix::MAX_CHANNELS> sources{};

    // The dump being sent: one gain per crosspoint, input-major, NaN for a
    // crosspoint the dump leaves out. Sized in ShapePorts; audio thread.
    std::vector<Flt> snapshot;
    std::size_t dumpAt = 0;
    bool dumping = false;
    int pendingDumps = matrixControl::DUMP_NONE;
    std::string line; // reserved, one dump line

    std::vector<std::string> arguments;
  };

} // namespace PATCHER
} // namespace YSE
