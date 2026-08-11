#pragma once
#include "headers/defines.hpp"
// The MIDI *input* family (issue #529). Gated on the RtMidi-backed device
// backend alone — not on YSE_WINDOWS, the way the older mMidi* senders are.
// YSE_ENABLE_MIDI_DEVICE is ON for Windows and Linux and OFF for Android and
// macOS, which is exactly the set of platforms on which a MIDI input port
// exists at all; the six sender objects' extra YSE_WINDOWS guard predates that
// option and is a separate question (see the issue's non-goals). When the
// option is OFF this whole file is empty and the objects are not registered,
// matching mMidiOut.
#if YSE_ENABLE_MIDI_DEVICE
#include "../pObject.h"
#include "../../midi/midiInHub.h"

// Declares one member of the MIDI-input family. The whole body lives in the two
// base classes below; a member only picks its outlets, its status byte and its
// documentation. Mirrors RUNNING_EXTREMUM_CLASS in math/gRunningExtremum.h.
#define MIDI_IN_CLASS(className, baseName, typeName)                                               \
  class className : public baseName {                                                              \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  protected:                                                                                       \
    void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;                    \
  };

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body of the MIDI-input family (issue #529).
     *
     *  ### What the family is for
     *
     *  Before it, the patcher could **send** MIDI and could not receive any:
     *  the seven `mMidi*` objects are formatters and senders, so no keyboard,
     *  no controller and no external sequencer could reach a patch at all —
     *  which is to say the most common way anyone actually plays a synthesiser
     *  was missing. These eight objects are the way in.
     *
     *  ### The threading, which is the whole design
     *
     *  MIDI arrives on RtMidi's own input thread; patcher objects are driven
     *  from the audio callback. The two never meet directly. `MIDI::inHub`
     *  copies each incoming message into a bounded lock-free SPSC queue on the
     *  RtMidi side, and `Calculate()` drains that queue on the audio side —
     *  `midiOutSender` in reverse, and with the same rule on both ends: no
     *  allocation, no lock, no I/O.
     *
     *  `Calculate()` is what runs the drain, and getting it *called* is the one
     *  structural thing this family needed from the patcher. The render
     *  traversal is a push from the DSP start points and a control object is
     *  only ever reached because a message arrived at an inlet — but these
     *  objects have no inlets and are not DSP objects, so nothing in the graph
     *  would ever reach them. `pObject::WantsBlockPoll()` is the answer: an
     *  object that says yes is listed in the block's `GraphState` and drained
     *  at the top of `patcherImplementation::Calculate`, beside the value,
     *  deferred-message and file-completion drains. A message that arrived
     *  between two blocks therefore reaches the patch in the block after it,
     *  and whatever it triggers is rendered by that same block.
     *
     *  What the drain costs is bounded on purpose: at most one queue's worth of
     *  events per block, so a flood on the wire cannot make the audio callback
     *  run long. A queue that overflows drops the excess and says so through
     *  the log once per episode rather than silently, since silent loss on a
     *  MIDI input is indistinguishable from a broken cable.
     *
     *  ### The port
     *
     *  Every member takes a `port` creation argument, the index into
     *  `YSE::system::getMidiInDeviceName(...)`, default 0. The hub opens the
     *  device the first time anything subscribes and closes it when the last
     *  subscriber goes. Several objects may share one port — that is the normal
     *  case, a `.notein` and a `.ctlin` on the same keyboard — and each gets its
     *  own queue, so one slow consumer cannot starve another.
     *
     *  A port that will not open (no hardware, or a backend that allows one
     *  client per port and a host that already holds it) leaves an object that
     *  is perfectly valid and simply never receives anything. That is
     *  deliberate: a patch must load the same way on a machine with no
     *  controller plugged in, and it is also what lets the whole family be
     *  tested without hardware, by injecting into the hub directly.
     *
     *  Changing `port` with a live `SetParams` re-parse **rebuilds** the object
     *  rather than patching the field, which is what a parse callback buys
     *  here (`Parameters::NeedsRebuild`). It has to: the subscription is taken
     *  when the object joins its patcher, and a field written on the audio
     *  thread cannot move a device port.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` pops from a wait-free queue and sends ints out outlets.
     *  Nothing on that path allocates, locks or blocks. Subscribing and
     *  unsubscribing take the hub's mutex and may open a device — both are
     *  control-thread only, from `SetParent` and the destructor.
     */
    class mMidiInBase : public pObject {
    public:
      ~mMidiInBase() override;

      mMidiInBase(const mMidiInBase&) = delete;
      mMidiInBase& operator=(const mMidiInBase&) = delete;
      mMidiInBase(mMidiInBase&&) = delete;
      mMidiInBase& operator=(mMidiInBase&&) = delete;

      /** @brief Control thread. Joining a patcher is what opens the port: the
       *         creation arguments have been parsed by now and the object is
       *         not yet published, so this is the one moment at which a device
       *         may be opened. Re-parenting re-subscribes, as `.r` does with
       *         its bus address. */
      void SetParent(pObject* newParent) override;

      /** @brief Audio thread. Drain this block's events. */
      void Calculate(YSE::THREAD thread) override;

      void SetMessage(const std::string&, float) override {}

      /** @brief Yes — see the class comment. This is what gets `Calculate()`
       *         called at all for an object with no inlets. */
      bool WantsBlockPoll() const override {
        return true;
      }

      /** @brief The hub subscription, or `kNoHandle` when the object has no
       *         patcher yet or the hub refused one. Diagnostic / test surface. */
      YSE::MIDI::inHub::Handle Subscription() const {
        return subscription.load(std::memory_order_acquire);
      }

      /** @brief The port this object listens to, as the arguments left it. */
      unsigned int Port() const;

    protected:
      mMidiInBase();

      /** @brief One message (or one chunk of a long one) has arrived. Called
       *         from `Calculate()` on the audio thread, once per event, in
       *         arrival order. */
      virtual void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) = 0;

      /** @brief Control thread, after the creation arguments are parsed. Clamps
       *         them into range; overridden further down to reach `channel`. */
      virtual void ParseParams();

      /** Index into the machine's MIDI input devices. Atomic because
       *  `Parameters` writes it as an `ATOMIC_INT`; only ever read on the
       *  control thread, a re-parse being a rebuild rather than a store. */
      aInt port;

    private:
      void Unsubscribe();

      // Written on the control thread (SetParent / the destructor) and read on
      // the audio thread by Calculate. Atomic for that crossing alone: it is
      // one word and never contended.
      std::atomic<YSE::MIDI::inHub::Handle> subscription{YSE::MIDI::inHub::kNoHandle};
    };

    /**
     *  @brief The six members that carry a MIDI channel (issue #529).
     *
     *  ### The channel argument filters; it never removes an outlet
     *
     *  Max drops the channel outlet from `notein`, `ctlin`, `bendin`, `pgmin`,
     *  `touchin` and `polyin` when a channel argument is given, on the grounds
     *  that an object listening to one channel has nothing to report about it.
     *  That is **not** reproduced, and the deviation is deliberate rather than
     *  an omission: an object whose outlet *count* depends on its arguments has
     *  no stable outlet numbering, so `.notein` and `.notein 3` would be two
     *  shapes under one name — a patch that gained an argument would silently
     *  re-point every cord leaving the box, and a saved patch's connection
     *  indices would mean different things before and after an edit.
     *
     *  So the channel outlet is always there. With no argument (or `0`, Max's
     *  omni) it carries the channel each message came in on; with an argument
     *  it carries that same constant, which costs a patch nothing and keeps the
     *  wiring honest.
     *
     *  ### Channels are 1..16
     *
     *  The wire carries a nibble, 0..15. Every outlet in this family reports
     *  1..16, which is Max's numbering, the numbering
     *  `MIDI::M_CHANNEL` uses on the sending side, and the numbering printed on
     *  the front of every piece of hardware. The one place the raw nibble
     *  survives is `.midiin`, which reports bytes and must not interpret them.
     */
    class mMidiChannelInBase : public mMidiInBase {
    public:
      /** @brief The channel filter: 0 for every channel, 1..16 for one. */
      int ChannelFilter() const {
        return channel.load();
      }

    protected:
      mMidiChannelInBase();

      /** @brief Whether a message on wire nibble `nibble` (0..15) passes the
       *         filter. */
      bool Accepts(unsigned char nibble) const;

      /** @brief The nibble as this family reports it: 1..16. */
      static int ChannelNumber(unsigned char nibble) {
        return static_cast<int>(nibble) + 1;
      }

      void ParseParams() override;

      aInt channel;
    };

    // ─── the family ───────────────────────────────────────────────────────────
    // Raw bytes and system real time take no channel; the other six do.

    MIDI_IN_CLASS(mMidiIn, mMidiInBase, YSE::OBJ::M_IN)
    MIDI_IN_CLASS(mRtIn, mMidiInBase, YSE::OBJ::M_RTIN)

    MIDI_IN_CLASS(mNoteIn, mMidiChannelInBase, YSE::OBJ::M_NOTEIN)
    MIDI_IN_CLASS(mBendIn, mMidiChannelInBase, YSE::OBJ::M_BENDIN)
    MIDI_IN_CLASS(mPgmIn, mMidiChannelInBase, YSE::OBJ::M_PGMIN)
    MIDI_IN_CLASS(mTouchIn, mMidiChannelInBase, YSE::OBJ::M_TOUCHIN)
    MIDI_IN_CLASS(mPolyIn, mMidiChannelInBase, YSE::OBJ::M_POLYIN)

    /**
     *  @brief `.ctlin` — the one member with a second filter of its own.
     *
     *  Max's `ctlin` takes a controller number after the channel and, when it
     *  is given, reports only that controller. The default is **-1**, not 0:
     *  controller 0 is Bank Select MSB, a real controller a patch may well want
     *  on its own, so 0 could not double as "any".
     */
    class mCtlIn : public mMidiChannelInBase {
    public:
      mCtlIn();
      const char* Type() const override {
        return YSE::OBJ::M_CTLIN;
      }
      CREATE(mCtlIn)

      /** @brief The controller filter: -1 for every controller, 0..127 for one. */
      int ControllerFilter() const {
        return controller.load();
      }

    protected:
      void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;
      void ParseParams() override;

    private:
      aInt controller;
    };

  } // namespace PATCHER
} // namespace YSE
#endif
