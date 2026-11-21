#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "../../dsp/sampling/waveformView.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~waveform``: the view and the edits of a waveform display of
     *         a named sample buffer, wrapping ``DSP::waveformView`` (issue
     *         #1151).
     *
     *  Ported from Max's ``waveform~``. The engine holds the view and edits
     *  the buffer; the host draws. A control object: everything happens as
     *  the message arrives, with the audio stopped too.
     *
     *  - Inlets 0 to 3: a number sets the display start, the display length
     *    (0 or less: the whole buffer), the selection start and the
     *    selection end, in milliseconds; a bang sends that value out of the
     *    outlet below. A change sends the link list.
     *  - Inlet 4: a list ``<start> <length> <selection start> <selection
     *    end>`` from another ``~waveform``'s link outlet sets all four; a
     *    change sends the link list on, so linked views in a circle settle.
     *  - Every inlet takes the messages: ``mouse <x> <y> <state>`` (x 0 to 1
     *    across, y -1 to 1 up, state 1 down, 2 drag, 0 up) and ``drag <x>
     *    <y>`` (a mouse drag), which select, loop, move or draw by the mode;
     *    ``mode <none|select|loop|move|draw>``, ``mouseoutput
     *    <none|down|up|downup|continuous>``, ``snap <none|grid|tick|zero>``,
     *    ``unit <ms|samples|phase|bpm>``, ``bpm <tempo> [beats]``,
     *    ``setbpm``, ``line <position|unit>``, ``undo``, ``name`` / ``set
     *    [buffer] [channel]``, ``crop``, ``normalize [peak]``,
     *    ``replacebuffercontents <file>``, and every parameter by its name.
     *  - Outlets: 0 display start, 1 display length, 2 selection start, 3
     *    selection end (floats, ms; 2 and 3 when ``outmode`` says), 4 the
     *    mouse ``<x> <amplitude> <state>``, 5 the link list (and ``bpm
     *    <tempo> <beats>`` after a ``setbpm``).
     *  - Parameters: ``name``, ``chanoffset`` (from 1), ``setmode`` (0
     *    none, 1 select, 2 loop, 3 move, 4 draw; default 1), ``outmode`` (0
     *    none to 4 continuous, the default), ``snapto`` (0 none, 1 grid, 2
     *    zero), ``grid`` (1000), ``ticks`` (0), ``offset`` (0 ms),
     *    ``setunit`` (0 ms, 1 samples, 2 phase, 3 bpm), ``bpm`` (120),
     *    ``beats`` (4), ``vzoom`` (1), ``voffset`` (0), ``clipdraw`` (0),
     *    ``quiet`` (0), ``zoom_orientation`` (0 up, 1 down). Messages are
     *    not saved.
     *
     *  **GUI value**, read only, ``CELLS`` cells: ``<serial> <buffer id>
     *  <channel> <display start> <display length> <selection start>
     *  <selection end> <line> <mode> <unit> <snap> <grid ms> <ticks>
     *  <offset> <bpm> <beats> <vzoom> <voffset>``. ``serial`` counts the
     *  changes, edits included. The buffer id is the one
     *  ``DSP::sampleBufferRegistry::findById`` takes (0: none); the host
     *  draws its samples with ``DSP::sampleBuffer::peaksMs`` over the
     *  display. A display length of 0 is the whole buffer; a line of -1 is
     *  none.
     *
     *  **Edits.** Drawing writes in place through a
     *  ``DSP::sampleBufferWriter``: no copy, no lock, no allocation; every
     *  reader of the buffer sees it at once. ``crop``, ``normalize`` and
     *  ``replacebuffercontents`` change the buffer's storage, which
     *  allocates, so they are tasks on the patcher's ``fileScheduler``, as
     *  ``~buffer``'s messages are: they find the buffer by id and run in
     *  order in the background. ``name`` and ``set`` rebind through a
     *  ``bufferBinding`` (#1256); the binding is taken by the next message,
     *  or by the block that delivers the rebind. A rebind is not saved.
     *
     *  **Threads.** A message may come from any thread. Handlers share the
     *  view and the writer under one ``exchange(true)`` guard and send
     *  their outlets while holding it: a message that arrives meanwhile,
     *  from another thread or back down a cord, is dropped and counted
     *  (``Dropped()``), which is also what ends a link chain that comes
     *  back. The GUI value is atomics written under the guard; host reads
     *  take no lock. The wrapper contains no sample math.
     *
     *  Not mapped: the colours, the shadow and reflection, ``style``,
     *  ``invert``, ``selectalpha``, ``labels`` / ``ruler``, ``vlabels``,
     *  ``vticks``, ``zoomstyle`` and ``zoomthresh`` are drawing, and
     *  ``buftime`` is how often the host redraws, both the host's;
     *  ``allowdrag`` and ``norulerclick`` say which mouse actions the host
     *  turns into messages (a file dropped on it is a
     *  ``replacebuffercontents``).
     */
    PATCHER_CLASS(dWaveform, YSE::OBJ::D_WAVEFORM)
    _NO_MESSAGES
    _NO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    // Read only: no _HAS_GUI_CELLS, which promises the write half.
    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief The cells of the GUI value. */
    static constexpr unsigned int CELLS = 18;

    /** @brief Messages dropped because another was being handled.
     *         Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Edit tasks that ran and failed. Diagnostics and tests. */
    unsigned int FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

    /** @brief The view. Only while no message is being handled (tests). */
    const DSP::waveformView& View() const {
      return fn;
    }

    /** @brief The buffer shown, or null. Only while no message is being
     *         handled (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

  private:
    struct editTask;

    // Run `body` under the guard with the writer pinned and the view shaped
    // by it, publish the GUI value and send what the event asks for. Any
    // thread; dropped when the guard is busy.
    template <class Body> void Handle(YSE::THREAD thread, Body&& body);
    void Send(const DSP::waveformView::event& e, YSE::THREAD thread);
    void Publish();
    // Words to any inlet: one message.
    void Message(const std::string& text, YSE::THREAD thread);
    bool Submit(editTask& task);
    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    bufferBinding<DSP::sampleBufferWriter> binding; // the guard holder's
    DSP::waveformView fn; // the guard holder's

    std::string bufferName;
    aFlt chanoffset;
    aFlt setmode;
    aFlt outmode;
    aFlt snapto;
    aFlt grid;
    aFlt ticks;
    aFlt offset;
    aFlt setunit;
    aFlt bpm;
    aFlt beats;
    aFlt vzoom;
    aFlt voffset;
    aFlt clipdraw;
    aFlt quiet;
    aFlt zoomOrientation;

    // The GUI value: cells 2 on, then the serial and the buffer id.
    std::atomic<float> cells[CELLS];
    std::atomic<std::uint32_t> serial{0};
    std::atomic<std::uint64_t> bufferId{0};

    // Lists sent, reserved at construction so composing one allocates
    // nothing. The guard holder's.
    std::string mouseText;
    std::string linkText;

    std::atomic<bool> busy{false};
    std::atomic<unsigned int> dropped{0};
    std::atomic<unsigned int> failed{0};
  };

} // namespace PATCHER
} // namespace YSE
