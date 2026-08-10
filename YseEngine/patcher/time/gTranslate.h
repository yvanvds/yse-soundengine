
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``translate``: "convert from any of the fixed or relative
     *         Max time values to any other" — here, between the patcher's
     *         millisecond world and a named domain clock's beats (issue #516).
     *
     *  ### Why the patcher needs one
     *
     *  Since #688 the patcher speaks two kinds of time and cannot mix them. A
     *  ``.delay 500`` waits half a second; a ``.timepoint main 4`` waits for a
     *  beat; a ``.metro clock main`` counts on a tempo that a ``.setclock``
     *  (#515) may be ramping while it counts. Every patch that has both
     *  eventually needs the exchange rate between them — "how long is a
     *  sixteenth at the tempo we are at *now*" — and today the only way to get
     *  it is an ``.expr`` with the tempo wired in by hand, which is wrong the
     *  moment the tempo moves. This object is that exchange rate as an object.
     *
     *  ### The formats, and the two Max has that a domain clock cannot
     *
     *  Five, in two families:
     *
     *  - **Fixed** — ``ms``, ``hz`` (a repetition rate: 2 Hz is 500 ms) and
     *    ``samples`` (at the live ``SAMPLERATE``). These need no clock at all.
     *  - **Tempo-relative** — ``beats`` and ``ticks`` (Max's 1/480th of a
     *    quarter note, so 480 ticks is one beat). These need no clock *either*,
     *    as long as the other end of the conversion is also relative.
     *
     *  A conversion **across** the two families is the only one that needs a
     *  tempo, and it reads it from the bound clock at the moment of conversion
     *  — never from a number stored when the object was built, which is #516's
     *  own requirement and the reason a tempo ramp cannot silently invalidate
     *  the answer. With no clock, or a clock at tempo 0, **nothing at all is
     *  emitted**: at tempo 0 there is no exchange rate in either direction, and
     *  a 0 would be indistinguishable from a real answer — ``.when``'s and
     *  ``.setclock``'s rule, for their reason. A *negative* tempo is not
     *  refused: a domain clock's tempo is not clamped, so a domain running
     *  backwards converts at a negative rate, which is the honest reading of
     *  "how far does this clock get in 500 ms".
     *
     *  Max lists two more formats and neither survives:
     *
     *  - **``bars.beats.units``** — Max's *default* input format — cannot be
     *    read or written, because a bar needs a meter and a
     *    ``CLOCK::domainClock`` is a bare beat accumulator with no bar, no
     *    meter and no downbeat. That is the objection ``timeValue.h`` (#705)
     *    already records, and the same one that costs ``.transport`` its
     *    ``seek``, ``.when`` its bars.beats.units and ``.tempo`` its
     *    ``quantize``. ``beats`` takes its place as the object's default
     *    *relative* format, being the unit the rest of the engine actually
     *    speaks.
     *  - **``hh:mm:ss``** is a *spelling* of milliseconds rather than a unit
     *    the engine has, and writing one out means emitting a symbol — a list
     *    outlet hands a ``std::string`` on, which allocates, on a handler that
     *    may be running on the audio callback. ``ms`` reads the same quantity.
     *
     *  ### Note values are read, but are not a format
     *
     *  Max's ``notevalues`` is a format you select. Here a note value is
     *  accepted *wherever a value is* — ``4n``, ``8nt``, ``4nd``, and Max's
     *  ``<n> ticks`` beside them, through ``timeValue.h``'s shared reader, so
     *  this object spells them exactly as ``.delay`` and ``.metro`` do. There
     *  is therefore no ``in notevalues``: a note value says what it is, so
     *  there would be nothing for the setting to change. And there is no ``out
     *  notevalues`` either, for a different reason — almost no duration has a
     *  note-value spelling, so writing one would mean quantising, and choosing
     *  a rounding is a decision this object must not make silently on the way
     *  through. A format word it does not know leaves the setting where it was.
     *
     *  So the input format decides one thing only: **what a bare number
     *  means**. A message that spells its own unit overrides it.
     *
     *  ### One inlet, one outlet, and a value that is kept
     *
     *  Max's shape. Any number or time value is converted and sent out
     *  immediately; ``bang`` converts **the last value again**, which is the
     *  point of keeping it: the same input at a new tempo is a new answer, and
     *  a ``.metro`` into this inlet is how a patch tracks a ramp. The value is
     *  canonicalised — to milliseconds or to beats — at the moment it arrives,
     *  so a later ``in`` change re-points the object for what comes next rather
     *  than retro-reading what already came.
     *
     *  Max's ``@listen`` (re-emit whenever the tempo changes) is deliberately
     *  absent, and the engine is the reason: YSE tempi *glide*, so a
     *  ``.setclock`` ramping over two seconds changes tempo every audio block
     *  and ``listen`` would empty a message per block into the patch. Max never
     *  meets that because a Max transport's tempo is a step. A ``.metro`` into
     *  this inlet is the same behaviour at a rate the patch chose.
     *  ``@listmode`` goes with it: converting a whole list at once means
     *  building a list to send, which allocates.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven entirely by its
     *  inlet. No handler allocates, locks or blocks when it turns out to be
     *  running on the audio callback — the command words are matched against
     *  the message in place (a ``substr`` would allocate), the time value is
     *  read by ``timeValue.h``'s and ``pListArgs.h``'s allocation-free readers,
     *  the conversion is arithmetic, and the tempo is reached through the
     *  patcher's ``clockBridge``, which is two acquire loads. The blocking
     *  by-name route is taken only when the object has established it is *not*
     *  on the callback, decided by ``patcherImplementation::CallingThread``
     *  (#690) rather than by the ``THREAD`` tag, which is dispatch semantics
     *  and not thread identity.
     *
     *  It never creates a clock and never destroys one — ``.when``'s and
     *  ``.tempo``'s reader contract (#513): a name nothing has claimed simply
     *  converts nothing across families until a ``.transport``, a ``.setclock``
     *  or the host brings the clock into being.
     */
    PATCHER_CLASS(gTranslate, YSE::OBJ::G_TRANSLATE)
    _NO_MESSAGES
    _NO_CALCULATE
    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Again)
    _INT_IN(TranslateInt)
    _FLOAT_IN(TranslateFloat)
    _LIST_IN(Command)

    /**
     *  @brief The time formats this object reads and writes.
     *
     *  ``MS``, ``HZ`` and ``SAMPLES`` are *fixed* — a wall-clock duration.
     *  ``BEATS`` and ``TICKS`` are *tempo-relative*. Crossing between the two
     *  groups is the only conversion that needs a clock. See the class notes
     *  for the two Max formats that are absent and why.
     */
    enum class Format {
      MS,
      BEATS,
      TICKS,
      HZ,
      SAMPLES,
    };

    /** @brief Control thread. Bind the named clock — never create it. */
    void SetParent(pObject* parent) override;

    /** @brief What a bare number arriving at the inlet means. ``ms`` by
     *         default, which is what a bare number already means everywhere
     *         else in the patcher — Max defaults to ``bars.beats.units``, which
     *         does not exist here. */
    Format InFormat() const {
      return (Format)informat.load(std::memory_order_relaxed);
    }

    /** @brief The format values are sent out in. ``ticks`` by default, as in
     *         Max. */
    Format OutFormat() const {
      return (Format)outformat.load(std::memory_order_relaxed);
    }

    /** @brief The clock whose tempo bridges the two families, or ``""``. The
     *         storage belongs to the patcher's bridge and never changes, so
     *         this is safe from any thread — and it is the *live* name, so it
     *         follows a ``clock <name>`` where the creation argument does
     *         not. */
    const char* ClockName() const;

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for
     *         a nameless or unparented object, for a bare ``clock``, and for a
     *         bridge that was full. */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief Whether a value has arrived for ``bang`` to convert again. */
    bool HasValue() const {
      return hasStored.load(std::memory_order_relaxed);
    }

    /**
     *  @brief The format @p length characters at @p text name, or false.
     *
     *  Strict and allocation-free: the whole token must be the word, since a
     *  ``clock ms.driver`` must not be read as a format. Public so the tests
     *  can pin the vocabulary without going through a message.
     */
    static bool ReadFormat(const char* text, std::size_t length, Format& out);

    /** @brief Whether @p format is measured in beats rather than in
     *         milliseconds — the family split the tempo bridges. */
    static bool IsRelative(Format format) {
      return format == Format::BEATS || format == Format::TICKS;
    }

  private:
    // Whether the handler currently running is on the audio callback — the
    // question a `THREAD` tag cannot answer (#690). False for a standalone
    // object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // The bound clock's tempo in BPM, by whichever route this thread may use.
    // False when there is no clock to read, in which case no cross-family
    // conversion happens and nothing is emitted.
    bool ReadTempo(YSE::THREAD thread, float& bpm) const;

    // A value in `format` as the family's canonical unit — milliseconds for a
    // fixed format, beats for a relative one. False when the value has no
    // canonical form (a rate of 0 Hz is no repetition at all).
    static bool ToCanonical(Format format, double value, double& canonical);

    // The reverse. False for the same kind of reason: 0 ms is no rate.
    static bool FromCanonical(Format format, double canonical, double& value);

    // Keep `canonical` (in `relative`'s unit) as the value a bang re-converts,
    // then convert and send it. Every input handler ends here.
    void Take(double canonical, bool relative, YSE::THREAD thread);

    // A bare number, read in the input format and handed to Take. The int and
    // float inlets and the numeric list are all this.
    void TakeNumber(double value, YSE::THREAD thread);

    // Convert the kept value into the output format and send it. Silent when
    // there is nothing kept, when the conversion needs a tempo there is no
    // clock for, and when the arithmetic does not land on a finite number.
    void Emit(YSE::THREAD thread);

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge. Binds
    // wait-free on whichever thread the message arrived on; a name that does
    // not fit, a bridge that is full, or a standalone object all leave the
    // object where it was, silently, since this may be the audio thread.
    void SetClock(const char* name, std::size_t length);

    // The creation arguments, whole. One LIST parameter rather than three
    // scalars because the tokens are not positional in the way Parameters::Set
    // is: `.translate ms beats` and `.translate main ms beats` are both legal
    // and the clock name may be any word that is not a format.
    std::vector<std::string> args;

    // The clock named by the creation arguments, and the only thing SetParent
    // binds. Written by ParseParams on the control thread before the object is
    // published and never again; `clock <name>` re-binds without touching it,
    // so this stays what the patch file says.
    std::string clockname;

    // The two formats, held as ints because `in <format>` and `out <format>`
    // may arrive on any thread. Format::MS and Format::TICKS by default.
    std::atomic<int> informat{(int)Format::MS};
    std::atomic<int> outformat{(int)Format::TICKS};

    // The last value received, in its family's canonical unit, and which
    // family that is. What `bang` converts again — at the tempo of the moment,
    // not the one it arrived at.
    std::atomic<double> stored{0.0};
    std::atomic<bool> storedRelative{false};
    std::atomic<bool> hasStored{false};

    // The patcher-wide binding for the tempo's clock, or 0. Taken in SetParent
    // and by `clock <name>`; clockBridge hands the same handle to every object
    // naming the same clock, so this shares a slot with the `.setclock` driving
    // it rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};
  };
}
}
