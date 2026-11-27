#pragma once
// Voice instancing (issue #1040): `~poly`, its boundary objects `.in`, `.out`,
// `~in`, `~out`, and `~thispoly`. The design is in
// docs/design/patcher_lifecycle_subpatchers.md, "Voices: ~poly"; the user
// guide in documentation/source/patcher/subpatchers.rst.
#include "../pObject.h"
#include "../graphState.h"
#include "../../dsp/routing/voiceAllocator.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace YSE {
  namespace PATCHER {

    class dPoly;

    /**
     *  @brief An event a ``~poly`` sends one of its voices without a cord
     *         (issue #1040), to every object in the voice that is a
     *         pObject::VoiceEventSink.
     *
     *  - ``MUTE``: the voice was muted (``data[0]`` 1) or unmuted (0) by the
     *    ~poly's ``mute`` message; ``~thispoly`` reports it.
     *  - ``MIDI``: a MIDI channel message the ~poly's ``midievent`` gave this
     *    voice (issue #756): ``data[0..count)`` are its bytes, the status
     *    byte (with its channel) first, then one or two data bytes.
     *  - ``MPE``: an ``mpeevent`` (issue #756), its seven ints as
     *    ``.mpeparse``'s rightmost outlet sends them: the zone's master
     *    channel, the zone (1 lower, 2 upper), the voice (-1 the whole zone),
     *    the channel, then the message's bytes from ``data[4]``, status
     *    first; ``count`` is 6 or 7.
     *
     *  ``.polymidiin`` takes the MIDI and MPE kinds. Trivially copyable,
     *  built on the stack.
     */
    struct voiceEvent {
      enum class KIND : std::uint8_t { MUTE, MIDI, MPE };
      KIND kind = KIND::MUTE;
      int count = 0;
      int data[7] = {};
    };

    /**
     *  @brief The root of one voice of a ``~poly`` (issue #1040): the
     *         container of that voice's copies, holding its number and its
     *         mute word.
     *
     *  Made by the patcher with the copies; not a registered type, never
     *  saved, never seen by a host, no pins. The mute word has
     *  gSubpatcher's two bits and is read the way a subpatcher's is
     *  (GraphState::muteChain, issue #1137): a muted voice is one whose root
     *  has both set. The ~poly also skips a muted voice's whole render.
     */
    class polyVoice : public pObject {
    public:
      /** @brief The type a voice root reports; registered nowhere. */
      static constexpr const char* TYPE = "~poly voice";

      polyVoice(dPoly* host, int number) : pObject(false), host_(host), number_(number) {}
      const char* Type() const override {
        return TYPE;
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}

      /** @brief The ~poly this voice belongs to. */
      dPoly* Host() const {
        return host_;
      }
      /** @brief The voice number, from 1. */
      int Number() const {
        return number_;
      }

      /** @brief Mutes (both bits) or unmutes the voice. Any thread. */
      void SetMute(bool on) {
        mute_.store(on ? ALL : 0, std::memory_order_release);
      }
      bool Muted() const {
        return mute_.load(std::memory_order_acquire) == ALL;
      }
      /** @brief The mute word, for GraphState::muteChain. */
      const std::atomic<std::uint8_t>* MuteWord() const {
        return &mute_;
      }

    private:
      static constexpr std::uint8_t ALL = 3; // gSubpatcher MUTE_CONTENTS | MUTE_NESTED
      dPoly* host_;
      int number_;
      std::atomic<std::uint8_t> mute_{0};
    };

    /**
     *  @brief ``~poly``: plays N copies of the subpatch inside it, voices that
     *         notes are allocated to (issue #1040). MSP's ``poly~``.
     *
     *  A container like ``patcher`` and ``~pfft``: objects are put inside it
     *  with SetContainer. What is inside is the voice's *definition* (the
     *  template) and does not run itself; the patcher makes ``voices`` copies
     *  of it, one per voice, and the ~poly renders each copy in its step of
     *  the block and sums what their ``~out`` objects put out. An edit inside
     *  the ~poly rebuilds every voice.
     *
     *  **Pins** come from the parameters, as ~pfft's do: ``inlets`` inlets,
     *  each a signal for the ``~in`` objects with its index and messages for
     *  the ``.in`` objects with its index; ``outlets`` signal outlets (the
     *  sum of every voice's ``~out`` with that index), then ``messages``
     *  message outlets (what a voice's ``.out`` sends), then one outlet with
     *  the number of the voice that sent it, sent first.
     *
     *  **Messages**, on any inlet, apply to that inlet's ``.in`` objects:
     *
     *  - ``target N``: where the messages that follow go: voice N (from 1),
     *    0 every voice (the default), a negative number none.
     *  - ``note <message>``: the message to the first voice that is not busy
     *    (DSP::voiceAllocator: the lowest free voice; with ``steal`` the one
     *    started longest ago). A voice is busy while its ``~thispoly`` says
     *    so, and from the moment it is given a note until the end of the
     *    next block, so a chord sent at once goes to different voices.
     *  - ``midinote <pitch> <velocity>``: a velocity above 0 is a note-on
     *    and goes where ``note`` would; the voice then holds the pitch, and
     *    the velocity-0 note-off for it goes to that voice.
     *  - ``midievent <status> <data> [<data>] [<detune>]`` (issue #756): a
     *    MIDI channel message, as bytes, for the voices' ``.polymidiin``.
     *    A note-on goes where ``midinote`` would and the voice holds the
     *    pitch; its note-off (status 128, or a note-on with velocity 0) goes
     *    to that voice; poly pressure to the voices holding the key; every
     *    other channel message to the target. ``detune`` is not used.
     *  - ``mpeevent <master> <zone> <voice> <channel> <status> <data>
     *    [<data>]`` (issue #756): ``.mpeparse``'s rightmost outlet. The
     *    message goes straight to voice ``voice`` (-1: every voice), no
     *    allocation: in MPE a note's channel is its voice. A voice the ~poly
     *    does not have gets nothing.
     *  - ``allnotesoff``: ``<pitch> 0`` to every voice holding a midinote,
     *    and a note-off to every voice holding a ``midievent`` note.
     *  - ``mute <voice> <0|1>``: mutes a voice (0: every voice). A muted
     *    voice does not compute and adds nothing to the outlets; its
     *    ``~thispoly`` reports the change.
     *  - ``voices <n>`` (issue #1294): the voice count, 1..128, at run time.
     *    Voices are added or removed on the control side (a deferred job, as
     *    a ``~send``'s ``set`` is: patcherImplementation::
     *    RequestSignalBusSync), one publish; the voices that stay keep their
     *    copies and so their running state, mute, busy, exclude, bypass and
     *    automute, and the notes they hold. A removed voice's note is
     *    forgotten; an added voice starts as a new one. Run-time state: the
     *    ``voices`` argument is what is saved.
     *  - ``busymap [<outlet>]`` / ``mutemap [<outlet>]`` (issue #1294): a list
     *    with one 0 or 1 per voice, from voice 1, out of message outlet
     *    ``outlet`` (0 by default): busy as ``note`` sees it (~thispoly's
     *    flag, or given a note this block), or muted.
     *  - ``exclude <voice> <0|1>`` (issue #1294): takes a voice (0: every
     *    voice) out of allocation: ``note``, ``midinote`` and a ``midievent``
     *    note-on neither choose nor steal it. Its held notes still end.
     *  - ``bypass <voice> <0|1>`` (issue #1294): the voice (0: every voice)
     *    does not render and adds the ~poly's inlet N signal to signal outlet
     *    N instead. A muted voice adds nothing, bypassed or not. Messages
     *    still reach it.
     *  - ``steal <0|1>``: whether ``note``, ``midinote`` and ``midievent``
     *    take a busy voice when none is free.
     *  - ``setvalue <voice> <message>``: the message to one voice (0: all),
     *    without changing the target.
     *  - ``list <message>``: the message as it is, to the target, so it may
     *    start with one of these words.
     *  - Anything else (bang, numbers, lists, other words) goes to the target.
     *
     *  Parameters: ``name`` (free text), ``voices`` (1, 1..128), ``inlets``
     *  (1, 0..16), ``outlets`` (1, 0..16), ``messages`` (0, 0..16), ``steal``
     *  (0). Changing one rebuilds the object and its voices.
     *
     *  **Real time.** Rendering walks each voice's compiled order; nothing
     *  allocates. The routing words parse in place and take a test-and-set
     *  guard (a concurrent or re-entrant one is dropped and counted, as on
     *  ``.poly``); plain data needs no guard. Copying and wiring voices is
     *  control-thread work under the patcher's mutex.
     */
    PATCHER_CLASS(dPoly, YSE::OBJ::D_POLY)
    _NO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    /** @brief The most voices one ~poly can have. */
    static constexpr int MAX_VOICES = 128;
    /** @brief The most inlets, signal outlets or message outlets. */
    static constexpr int MAX_PINS = 16;
    /** @brief The longest message ``note``, ``setvalue`` and ``list`` pass on
     *         without its leading word; a longer one is dropped. */
    static constexpr std::size_t MAX_MESSAGE = 512;

    /** @brief The number of voices the published graph has. Any thread. */
    int Voices() const {
      return live.load(std::memory_order_acquire);
    }
    /** @brief The number of voices asked for: the argument, or the last
     *         ``voices`` message. The patcher builds this many. Any thread. */
    int WantedVoices() const {
      return wanted.load(std::memory_order_acquire);
    }
    /** @brief Patcher, control side, before a publish that adds voices
     *         [@p from, @p to) (from 0): their per-voice state (busy, pending,
     *         exclude, bypass, automute) back to a new voice's. Nothing in the
     *         graph being replaced renders those voices. */
    void ClearVoices(int from, int to);
    /** @brief Patcher, control side, after a publish: the graph has @p count
     *         voices. Voices() reports it, a target above it becomes it, and
     *         the allocator follows on the next routing word (under the
     *         guard), forgetting the notes of removed voices. */
    void PublishVoices(int count);
    int Inlets() const {
      return static_cast<int>(inputs.size());
    }
    int SignalOutlets() const {
      return static_cast<int>(results.size());
    }
    int MessageOutlets() const {
      return static_cast<int>(outputs.size()) - SignalOutlets() - 1;
    }

    /** @brief Whether voice @p voice (from 1) said it is busy (~thispoly). */
    bool Busy(int voice) const;
    /** @brief ~thispoly's busy flag for voice @p voice (from 1). Any thread. */
    void SetBusy(int voice, bool on);
    /** @brief Where plain messages go: 0 all voices, N voice N, -1 none. */
    int Target() const {
      return target.load(std::memory_order_relaxed);
    }
    /** @brief Whether voice @p voice (from 1) is out of allocation. */
    bool Excluded(int voice) const;
    /** @brief Whether voice @p voice (from 1) passes its input through. */
    bool Bypassed(int voice) const;
    /** @brief ~thispoly's ``automute`` for voice @p voice (from 1). Any
     *         thread. */
    bool AutoMute(int voice) const;
    void SetAutoMute(int voice, bool on);
    /** @brief Mutes or unmutes voice @p voice (from 1; 0 every voice) as the
     *         ``mute`` message does, its ~thispoly reporting it. Any thread. */
    void MuteVoice(int voice, bool on, YSE::THREAD thread);
    /** @brief The pitch voice @p voice (from 1) holds after a midinote, or -1. */
    int PitchOfVoice(int voice) const;
    /** @brief Routing words refused: concurrent or re-entrant. Monotonic. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief A voice's ``.out`` @p index sends: the voice number on the last
     *         outlet, then the message on message outlet @p index. Any thread. */
    void VoiceBang(int voice, int index, YSE::THREAD thread);
    void VoiceInt(int voice, int index, int value, YSE::THREAD thread);
    void VoiceFloat(int voice, int index, float value, YSE::THREAD thread);
    void VoiceList(int voice, int index, const std::string& value, YSE::THREAD thread);

  private:
    void Configure();
    // A routing word; false when `word` is not one.
    bool Route(std::string_view word, const std::string& text, std::size_t rest, int inlet,
               YSE::THREAD thread);
    // `text` from `from` to its end, to the .in objects with index `inlet` of
    // voice `voice` (0: every voice) of `d`, sent as the typed message it
    // spells: bang, int, float or list.
    void DeliverPart(const GraphState::polyDomain& d, int voice, int inlet, const std::string& text,
                     std::size_t from, YSE::THREAD thread);
    void Mute(const GraphState::polyDomain& d, int voice, bool on, YSE::THREAD thread);
    // `event` to the VoiceEventSink objects of voice `voice` (0: every
    // voice) of `d`.
    void DeliverEvent(const GraphState::polyDomain& d, int voice, const voiceEvent& event,
                      YSE::THREAD thread);
    // The `midievent` and `mpeevent` words (issue #756).
    void MidiEvent(const GraphState::polyDomain& d, const std::string& text, std::size_t rest,
                   YSE::THREAD thread);
    void MpeEvent(const GraphState::polyDomain& d, const std::string& text, std::size_t rest,
                  YSE::THREAD thread);
    // `busymap` / `mutemap` (issue #1294), under the guard.
    void Map(const GraphState::polyDomain& d, bool mute, int outlet, YSE::THREAD thread);
    // Sets `flags[voice - 1]` (0: every voice of the published count).
    void SetFlag(std::atomic<bool>* flags, int voice, bool on);
    // A voice given a note: unmuted first when its ~thispoly automutes.
    void Start(const GraphState::polyDomain& d, int voice, YSE::THREAD thread);
    bool Enter();
    void Leave();
    bool Available(int voice) const;
    // The allocator's skip test (issue #1294): the excluded voices, by index.
    auto Skip() const {
      return [this](int voice) { return excluded[voice].load(std::memory_order_acquire); };
    }

    std::string name;
    // The `voices` argument (saved). The run-time count is `wanted` / `live`.
    int voiceCount = 1;
    int ins = 1;
    int outs = 1;
    int messages = 0;
    int stealArgument = 0;

    // Under the guard: not thread-safe.
    DSP::voiceAllocator pool{MAX_VOICES};
    // Per voice (index voice - 1): ~thispoly's busy flag, and "given a note
    // this block", cleared after each render.
    std::unique_ptr<std::atomic<bool>[]> busy;
    std::unique_ptr<std::atomic<bool>[]> pending;
    // Per voice (issue #1294): `exclude`, `bypass`, ~thispoly's `automute`.
    // Kept here, not in the copies, so a rebuild keeps them.
    std::unique_ptr<std::atomic<bool>[]> excluded;
    std::unique_ptr<std::atomic<bool>[]> bypassed;
    std::unique_ptr<std::atomic<bool>[]> automute;
    // The voice count asked for (any thread), and the one published (the
    // patcher, after each publish). The allocator's size follows `live`
    // under the guard (Enter).
    std::atomic<int> wanted{1};
    std::atomic<int> live{1};
    // Per voice (index voice - 1), under the guard: the channel (0-15) of
    // the `midievent` note-on the voice holds, or -1 when it holds a
    // `midinote` (or nothing), so allnotesoff ends each the way it began.
    int noteChannel[MAX_VOICES] = {};
    std::vector<std::unique_ptr<DSP::buffer>> results;
    std::atomic<int> target{0};
    // The leading word's remainder, reserved to MAX_MESSAGE at construction.
    // Under the guard.
    std::string scratch;
    std::atomic<bool> guard{false};
    std::atomic<std::uint64_t> dropped{0};
  };

  /**
   *  @brief ``.in``: inside a ``~poly``, the messages sent to one of its
   *         inlets (issue #1040). MSP's ``in``.
   *
   *  No inlets; one outlet, everything the ~poly passes to this voice on
   *  inlet ``index``. Elsewhere it sends nothing.
   */
  PATCHER_CLASS(gVoiceIn, YSE::OBJ::G_VOICE_IN)
  _NO_MESSAGES
  _NO_CALCULATE

  int Index() const {
    return index.load();
  }
  /** @brief The ~poly's delivery. */
  void Send(const std::string& text, std::size_t from, YSE::THREAD thread);
  void SendBang(YSE::THREAD thread);
  void SendInt(int value, YSE::THREAD thread);
  void SendFloat(float value, YSE::THREAD thread);
  void SendList(const std::string& value, YSE::THREAD thread);

private:
  aInt index{0};
  // A part of a ~poly message, reserved to dPoly::MAX_MESSAGE.
  std::string part;
};

/**
 *  @brief ``.out``: inside a ``~poly``, sends messages out of one of its
 *         message outlets, after the voice's number on its last outlet
 *         (issue #1040). MSP's ``out``.
 */
PATCHER_CLASS(gVoiceOut, YSE::OBJ::G_VOICE_OUT)
_NO_MESSAGES
_NO_CALCULATE

_BANG_IN(SetBang)
_INT_IN(SetInt)
_FLOAT_IN(SetFloat)
_LIST_IN(SetList)

void JoinVoice(polyVoice* voice) override {
  voice_ = voice;
}

private:
aInt index{0};
polyVoice* voice_ = nullptr;
}
;

/**
 *  @brief ``~in``: inside a ``~poly``, the signal on one of its inlets
 *         (issue #1040). MSP's ``in~``.
 *
 *  The ~poly loads it with its inlet's buffer before rendering the voice;
 *  it passes that buffer on without a copy. Elsewhere it sends silence.
 */
PATCHER_CLASS(dVoiceIn, YSE::OBJ::D_VOICE_IN)
_NO_MESSAGES
_DO_CALCULATE

int Index() const {
  return index.load();
}
/** @brief The ~poly's load before the voice renders; null for silence. */
void Load(DSP::buffer* buffer) {
  loaded = buffer;
}

private:
aInt index{0};
DSP::buffer* loaded = nullptr;
DSP::buffer silence;
}
;

/**
 *  @brief ``~out``: inside a ``~poly``, the signal one of its signal outlets
 *         sums over the voices (issue #1040). MSP's ``out~``.
 */
PATCHER_CLASS(dVoiceOut, YSE::OBJ::D_VOICE_OUT)
_NO_MESSAGES
_DO_CALCULATE
_DO_RESET

int Index() const {
  return index.load();
}
/** @brief Adds this block's input to @p sum. Render thread. */
void AddTo(DSP::buffer& sum) const;

private:
aInt index{0};
DSP::buffer* in = nullptr;
}
;

/**
 *  @brief ``~thispoly``: inside a ``~poly``, a voice's busy and mute state
 *         and its number (issue #1040). MSP's ``thispoly~``.
 *
 *  Inlet: a number sets the voice busy (non-zero) or free (0): a busy voice
 *  gets no ``note`` / ``midinote``. A signal does the same each block
 *  (busy while any sample is non-zero). ``mute 0|1`` unmutes / mutes the
 *  voice. ``automute 0|1`` (issue #1294): while on, the voice is muted
 *  whenever it is not busy and unmuted when it becomes busy or the ~poly
 *  gives it a note (so it runs a block to say it is busy). A bang sends the
 *  voice count, then the voice number.
 *
 *  Outlets: the voice number (from 1; 0 outside a ~poly), the mute state
 *  (when it changes through this object or the ~poly's ``mute``), the
 *  voice count (1 outside a ~poly).
 */
PATCHER_CLASS(dThisPoly, YSE::OBJ::D_THISPOLY)
_NO_MESSAGES
_DO_CALCULATE

_BANG_IN(SetBang)
_INT_IN(SetInt)
_FLOAT_IN(SetFloat)
_LIST_IN(SetList)

void JoinVoice(polyVoice* voice) override {
  voice_ = voice;
}
bool VoiceEventSink() const override {
  return true;
}
void DeliverVoiceEvent(const voiceEvent& event, YSE::THREAD thread) override;

private:
void SetBusyFlag(bool on, YSE::THREAD thread);
polyVoice* voice_ = nullptr;
}
;

/**
 *  @brief ``.polymidiin``: inside a ``~poly``, the MIDI its voice is given by
 *         the ~poly's ``midievent`` and ``mpeevent`` messages (issue #756).
 *         Max's ``polymidiin``.
 *
 *  No inlets: what it sends arrives without a cord, as a voiceEvent of kind
 *  MIDI or MPE (pObject::VoiceEventSink). One outlet: each message as a
 *  numeric byte list, ``<status> <data> [<data>]``, the status byte with its
 *  channel, which ``.mpeparse`` and ``.midiparse`` inside the voice decode.
 *  Elsewhere it sends nothing.
 *
 *  **Real time.** Delivered on the thread that runs the ~poly's handler; a
 *  three-byte list fits small-string storage, so nothing allocates.
 */
PATCHER_CLASS(mPolyMidiIn, YSE::OBJ::M_POLYMIDIIN)
_NO_MESSAGES
_NO_CALCULATE

bool VoiceEventSink() const override {
  return true;
}
void DeliverVoiceEvent(const voiceEvent& event, YSE::THREAD thread) override;
}
;

} // namespace PATCHER
} // namespace YSE
