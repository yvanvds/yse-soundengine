
#include "gTranslate.h"
#include "../../clock/clockManager.h"
#include "../../headers/constants.hpp"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include "timeValue.h"
#include <cmath>
#include <cstddef>

using namespace YSE::PATCHER;

#define className gTranslate

namespace {

  using Format = YSE::PATCHER::gTranslate::Format;

  // Milliseconds in a minute — the whole of the exchange rate between the two
  // families, since a beat is a quarter note and a domain clock's tempo is in
  // quarter notes per minute.
  constexpr double MS_PER_MINUTE = 60000.0;

  struct formatWord {
    const char* word;
    std::size_t length;
    Format format;
  };

  // The vocabulary, and all of it. Max's `bars.beats.units`, `hh:mm:ss` and
  // `notevalues` are deliberately not here — see the header for the reason each
  // one is out, which is different in all three cases.
  constexpr formatWord kFormats[] = {
      {"ms", 2, Format::MS}, {"beats", 5, Format::BEATS},     {"ticks", 5, Format::TICKS},
      {"hz", 2, Format::HZ}, {"samples", 7, Format::SAMPLES},
  };

  // The Max format words this object knows about and refuses. Recognising them
  // is not politeness: the creation arguments read any token that is not a
  // format as the clock name, so without this a `.translate notevalues ms`
  // would quietly bind a domain clock called "notevalues".
  constexpr const char* kRefusedFormats[] = {
      "notevalues", "notevalue", "bbu", "bars.beats.units", "hh:mm:ss",
  };

  bool WordIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  bool IsRefusedFormat(const char* text, std::size_t length) {
    for (const char* word : kRefusedFormats) {
      std::size_t wordLength = 0;
      while (word[wordLength] != '\0')
        wordLength++;
      if (WordIs(text, length, word, wordLength)) return true;
    }
    return false;
  }

  // The first separator-delimited token of @p text at or after @p offset, as
  // the half-open range [begin, end). False when there is nothing there. A
  // range rather than a substring, because a substr would allocate on whichever
  // thread the message arrived on — `.change`'s reader, for its reason.
  bool LeadingToken(const std::string& text, std::size_t offset, std::size_t& begin,
                    std::size_t& end) {
    begin = offset;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  constexpr char kInletDoc[] =
      "Takes a time value and sends it back out in the other format. A bare int or float is read "
      "in the input format — milliseconds unless the object was told otherwise — and a value that "
      "spells its own unit overrides that: '4n', '8nt' and '4nd' are Max's note values, and '1440 "
      "ticks' is Max's tick count, both read through the same reader '.delay' and '.metro' use, so "
      "the three objects agree on what a note value is. A bang converts the *last* value again, at "
      "the tempo of the moment rather than the one it arrived at, which is what makes a '.metro' "
      "into this inlet a patch that tracks a tempo ramp. 'in <format>' and 'out <format>' are "
      "Max's two attributes under their own names, 'clock <name>' re-points the object at another "
      "domain clock and a bare 'clock' unbinds it. The formats are 'ms', 'beats', 'ticks', 'hz' "
      "and 'samples'; a word that is not one of those leaves the setting exactly where it was, "
      "including Max's 'bars.beats.units' and 'hh:mm:ss', which a domain clock cannot express, and "
      "'notevalues', which is read as an input spelling rather than selected as a format. Nothing "
      "at all is emitted when the conversion cannot be made: no clock, a clock at tempo 0 or a "
      "rate of 0 Hz all produce silence rather than a zero that would be indistinguishable from a "
      "real answer.";

} // namespace

CONSTRUCT() {
  // Max's shape: one inlet that takes everything, one outlet.
  ADD_IN_0;
  REG_BANG_IN(Again);
  REG_INT_IN(TranslateInt);
  REG_FLOAT_IN(TranslateFloat);
  REG_LIST_IN(Command);

  ADD_OUT_FLOAT; // the value in the output format

  // One LIST parameter rather than three scalars: the tokens are not positional
  // the way Parameters::Set is, since the clock name may be any word that is
  // not a format and may come before or after the two formats.
  ADD_PARAM(args);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_DESCRIPTION(
      "Converts a time value from one time format to another — Max's translate, and the exchange "
      "rate between the patcher's two kinds of time (issue #516). Since the patcher gained named "
      "domain clocks it speaks both wall-clock time (a '.delay 500' waits half a second) and "
      "musical time (a '.timepoint main 4' waits for a beat), and a patch that has both eventually "
      "needs to ask how long a sixteenth is at the tempo it is running at now. '.translate ms "
      "beats main' answers that: send it 500 and it sends back how many beats of the clock called "
      "'main' fit in half a second. Five formats, in two families — 'ms', 'hz' (a repetition rate: "
      "2 Hz is 500 ms) and 'samples' at the live sample rate are fixed wall-clock times, while "
      "'beats' and 'ticks' (Max's 1/480th of a quarter note) are relative to a tempo. A conversion "
      "within one family needs no clock at all; a conversion across them reads the tempo from the "
      "bound clock at the moment it converts, never from a number cached when the object was "
      "built, so a tempo ramp cannot silently invalidate the answer. Note values are accepted "
      "wherever a value is — '4n', '8nt', '4nd' — through the same reader '.delay' and '.metro' "
      "use, so the input format only ever decides what a *bare number* means. Max's "
      "'bars.beats.units' and 'hh:mm:ss' are absent, the first because a bar needs a meter and a "
      "domain clock is a bare beat accumulator with none, the second because it is a spelling of "
      "milliseconds that would have to be written out as a symbol; 'beats' takes the first one's "
      "place as the default relative format. Bang converts the last value again at the current "
      "tempo, which is how a patch tracks a ramp. With no clock, a clock at tempo 0 or a rate of 0 "
      "Hz nothing at all is emitted, not a zero. The object never creates a clock and never "
      "destroys one — '.when''s reader contract — and no handler allocates, locks or blocks when "
      "it turns out to be running on the audio callback: the message words are matched in place, "
      "the conversion is arithmetic, and the tempo is two acquire loads through the patcher's "
      "clock bridge.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "value", kInletDoc,
            "a number, a note value, '<n> ticks', bang, 'in|out <format>', 'clock <name>'");
  OUTLET_DOC(0, "converted",
             "The value in the output format, sent as soon as it arrives and again on every "
             "bang. Ticks by default, as in Max. Nothing is emitted when the conversion "
             "cannot be made — across the two families with no clock bound, or on a clock "
             "sitting at tempo 0, where there is no exchange rate in either direction and a "
             "zero would be indistinguishable from a real answer. A negative tempo is not "
             "refused: a domain clock running backwards converts at a negative rate, which "
             "is the honest reading of how far it gets in a given number of milliseconds.",
             "");
  PARAM_DOC("in out clock", "ms ticks",
            "The input format, the output format, and the clock whose tempo bridges them. The "
            "first two tokens that name a format are the input and the output, in that order, and "
            "the first token that names no format is the clock name — so '.translate ms beats "
            "main' and '.translate main ms beats' are the same object. The formats are 'ms', "
            "'beats', 'ticks', 'hz' and 'samples'. The defaults are 'ms' in and 'ticks' out: Max "
            "defaults its input to 'bars.beats.units', which needs a meter a domain clock does not "
            "have, and a bare number already means milliseconds everywhere else in the patcher. "
            "Max's absent format words are recognised and refused rather than mistaken for a clock "
            "name. Without a clock the object still converts within a family — milliseconds to "
            "samples, beats to ticks — and stays silent across them; the clock is bound, never "
            "created, so naming one the host has not made yet leaves the object quiet until it "
            "appears.",
            "[<format>] [<format>] [<clock name>]");
}

// ─── the creation arguments ─────────────────────────────────────────────────

PARM_CLEAR() {
  // The whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one still holding the previous creation argument.
  args.clear();
  clockname.clear();
  informat.store((int)Format::MS, std::memory_order_relaxed);
  outformat.store((int)Format::TICKS, std::memory_order_relaxed);
  hasStored.store(false, std::memory_order_relaxed);
  stored.store(0.0, std::memory_order_relaxed);
  storedRelative.store(false, std::memory_order_relaxed);
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford a reason for what it refuses. Scanned rather than taken positionally
  // — `.change`'s reading of the same problem — because the clock name is any
  // token that is not a format and may sit on either side of them.
  int formats = 0;
  for (const std::string& token : args) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens.
    if (token.empty()) continue;

    Format parsed = Format::MS;
    if (ReadFormat(token.c_str(), token.size(), parsed)) {
      if (formats == 0) {
        informat.store((int)parsed, std::memory_order_relaxed);
      } else if (formats == 1) {
        outformat.store((int)parsed, std::memory_order_relaxed);
      }
      formats++;
      continue;
    }

    if (IsRefusedFormat(token.c_str(), token.size())) {
      // A Max format word this object cannot honour. Named rather than silently
      // taken as the clock name, which is what it would otherwise become.
      INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot use the '" +
                                            token + "' time format; ignored");
      continue;
    }

    if (clockname.empty()) clockname = token;
  }
}

// ─── the clock: bound, never created ────────────────────────────────────────

void gTranslate::SetParent(pObject* parent) {
  pObject::SetParent(parent);

  // A standalone object has no patcher and therefore no bridge. It still
  // converts within a family — that needs no tempo — and stays silent across
  // them, which is the same answer a parented object gives for a clock nobody
  // has made.
  if (parent == nullptr) return;
  if (clockname.empty()) return;

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Bind only. `.transport` and `.setclock` create clocks and this object does
  // not — the reader contract #513 settled — so there is no CLOCK::Manager()
  // call here. Binding is idempotent by name, so this shares a slot with the
  // object driving the clock rather than costing one of its own.
  binding.store(clocks->Bind(clockname.c_str(), clockname.size()), std::memory_order_relaxed);
}

const char* gTranslate::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
}

void gTranslate::SetClock(const char* name, std::size_t length) {
  // A bare `clock` gives the clock back, which for this object means that
  // conversions across the two families go quiet. The ones within a family are
  // untouched: they never needed a tempo.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    return;
  }

  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Wait-free: a bounded walk over the patcher's binding table and a memcpy of
  // the name into a slot that already exists. A refusal (the table is full, or
  // the name is longer than a slot holds) leaves the object on whatever clock
  // it was on rather than silently converting nothing.
  const clockBridge::Handle bound = clocks->Bind(name, length);
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
}

// ─── which route this handler may take ──────────────────────────────────────

bool gTranslate::OnAudioThread(YSE::THREAD thread) const {
  // The `THREAD` tag is dispatch semantics, not thread identity: in-patcher
  // delivery dispatches T_DSP, and the drains at the top of Calculate dispatch
  // T_GUI *from the audio callback*. Only the patcher knows, and only since
  // #690 — so ask it, exactly as `.when`, `.setclock` and `.tempo` do. A
  // standalone object has no patcher and is never rendered, so it answers false.
  if (parent == nullptr) return false;
  return static_cast<patcherImplementation*>(parent)->CallingThread(thread) == YSE::T_DSP;
}

bool gTranslate::ReadTempo(YSE::THREAD thread, float& bpm) const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  // Nothing bound is nothing to read. Unlike `.when`, this object has no
  // by-name fallback for an unbound name: it takes a `clock <name>`, so the
  // name it would read is exactly the name the bridge holds.
  if (bound == 0) return false;
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return false;

  if (OnAudioThread(thread)) {
    // Two acquire loads, and false while the binding is unresolved. A clock the
    // host has destroyed under a resolved binding reports the tempo it froze at
    // rather than going quiet — #707's rule, inherited from the bridge.
    return clocks->Tempo(bound, bpm);
  }

  // Off the callback the manager answers by name, inline. It takes the
  // manager's mutex, which this thread is allowed to do, and it needs no
  // resolved binding — so a value sent in the same breath as CreateObject
  // converts immediately rather than waiting for the background pool.
  const char* name = clocks->NameOf(bound);
  if (name == nullptr || name[0] == '\0') return false;
  // Almost always the creation argument, which is already the std::string the
  // manager wants. Only a `clock <name>` re-point needs one built, and this
  // branch is never the audio callback, so that allocation is this thread's to
  // make — `.tempo`'s reasoning, and its code.
  if (clockname == name) {
    // `currentTempo` answers 0 for a name it does not have, and 0 is a
    // perfectly ordinary tempo, so the existence question is asked separately.
    if (!CLOCK::Manager().clockExists(clockname)) return false;
    bpm = CLOCK::Manager().currentTempo(clockname);
    return true;
  }
  const std::string live(name);
  if (!CLOCK::Manager().clockExists(live)) return false;
  bpm = CLOCK::Manager().currentTempo(live);
  return true;
}

// ─── the arithmetic ─────────────────────────────────────────────────────────

bool gTranslate::ReadFormat(const char* text, std::size_t length, Format& out) {
  if (text == nullptr) return false;
  for (const formatWord& entry : kFormats) {
    if (WordIs(text, length, entry.word, entry.length)) {
      out = entry.format;
      return true;
    }
  }
  return false;
}

bool gTranslate::ToCanonical(Format format, double value, double& canonical) {
  switch (format) {
  case Format::MS:
  case Format::BEATS:
    canonical = value;
    return true;
  case Format::TICKS:
    canonical = value / TICKS_PER_BEAT;
    return true;
  case Format::HZ:
    // 0 Hz is no repetition at all, which is not a duration. Refused rather
    // than folded to an infinity that would come out of the outlet as one.
    if (!(value != 0.0)) return false;
    canonical = 1000.0 / value;
    return true;
  case Format::SAMPLES: {
    // Live SAMPLERATE, read here rather than cached when the object was built:
    // the device may have been opened at another rate since (#637's rule).
    const double rate = (double)SAMPLERATE;
    if (!(rate > 0.0)) return false;
    canonical = value * 1000.0 / rate;
    return true;
  }
  }
  return false;
}

bool gTranslate::FromCanonical(Format format, double canonical, double& value) {
  switch (format) {
  case Format::MS:
  case Format::BEATS:
    value = canonical;
    return true;
  case Format::TICKS:
    value = canonical * TICKS_PER_BEAT;
    return true;
  case Format::HZ:
    // The mirror of the read: a duration of 0 ms repeats at no rate.
    if (!(canonical != 0.0)) return false;
    value = 1000.0 / canonical;
    return true;
  case Format::SAMPLES: {
    const double rate = (double)SAMPLERATE;
    if (!(rate > 0.0)) return false;
    value = canonical * rate / 1000.0;
    return true;
  }
  }
  return false;
}

void gTranslate::Take(double canonical, bool relative, YSE::THREAD thread) {
  stored.store(canonical, std::memory_order_relaxed);
  storedRelative.store(relative, std::memory_order_relaxed);
  hasStored.store(true, std::memory_order_relaxed);
  Emit(thread);
}

void gTranslate::Emit(YSE::THREAD thread) {
  // Max's bang "converts the last input value"; with nothing received there is
  // no last value, and nothing is emitted.
  if (!hasStored.load(std::memory_order_relaxed)) return;

  double canonical = stored.load(std::memory_order_relaxed);
  const bool relative = storedRelative.load(std::memory_order_relaxed);
  const Format out = OutFormat();

  if (IsRelative(out) != relative) {
    // The one conversion that needs a clock, and it reads the tempo *now* —
    // #516's requirement, and what keeps a tempo ramp from silently
    // invalidating the answer.
    float bpm = 0.f;
    if (!ReadTempo(thread, bpm)) return;
    // Tempo 0 is a stopped clock, and a stopped clock has no exchange rate in
    // either direction: no number of milliseconds is any beats and no number of
    // beats ever arrives. Nothing is emitted, rather than a 0 that would look
    // like an answer. A negative tempo is allowed through — a domain clock's
    // tempo is not clamped, and a domain running backwards converts at a
    // negative rate.
    if (!std::isfinite(bpm) || bpm == 0.f) return;
    canonical = relative ? canonical * (MS_PER_MINUTE / (double)bpm)
                         : canonical * ((double)bpm / MS_PER_MINUTE);
  }

  double value = 0.0;
  if (!FromCanonical(out, canonical, value)) return;
  // An overflow on the way through is not an answer. Every refusal in this
  // object is silence for the same reason: a substituted number would be
  // indistinguishable from a real one.
  if (!std::isfinite(value)) return;
  outputs[0].SendFloat((float)value, thread);
}

void gTranslate::TakeNumber(double value, YSE::THREAD thread) {
  const Format in = InFormat();
  double canonical = 0.0;
  if (!ToCanonical(in, value, canonical)) return;
  Take(canonical, IsRelative(in), thread);
}

// ─── the methods ────────────────────────────────────────────────────────────

BANG_IN(Again) {
  (void)inlet;
  // The same value at the tempo of the moment, which is a different answer
  // whenever the clock has moved. A `.metro` into this inlet is how a patch
  // follows a ramp.
  Emit(thread);
}

FLOAT_IN(TranslateFloat) {
  (void)inlet;
  TakeNumber((double)value, thread);
}

INT_IN(TranslateInt) {
  TranslateFloat((float)value, inlet, thread);
}

LIST_IN(Command) {
  (void)inlet;

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!LeadingToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  // Max's two attributes, under their own names. The word is matched in place —
  // a substr would allocate on whichever thread the message arrived on — and a
  // word that does not name a format leaves the setting exactly where it was,
  // rather than reconfiguring the object into something it was not asked for.
  const bool setsInput = WordIs(value.c_str() + begin, length, "in", 2);
  const bool setsOutput = WordIs(value.c_str() + begin, length, "out", 3);
  if (setsInput || setsOutput) {
    std::size_t wordBegin = 0;
    std::size_t wordEnd = 0;
    if (!LeadingToken(value, end, wordBegin, wordEnd)) return;
    Format parsed = Format::MS;
    if (!ReadFormat(value.c_str() + wordBegin, wordEnd - wordBegin, parsed)) return;
    if (setsInput) {
      informat.store((int)parsed, std::memory_order_relaxed);
    } else {
      outformat.store((int)parsed, std::memory_order_relaxed);
    }
    return;
  }

  // `.metro`'s, `.timepoint`'s and `.tempo`'s `clock <name>`, read the same way:
  // the whole remainder is the name, so a clock named with spaces works, and a
  // bare `clock` unbinds.
  if (WordIs(value.c_str() + begin, length, "clock", 5)) {
    std::size_t nameBegin = end;
    while (nameBegin < value.size() && IsSelectorSeparator(value[nameBegin]))
      nameBegin++;
    std::size_t nameEnd = value.size();
    while (nameEnd > nameBegin && IsSelectorSeparator(value[nameEnd - 1]))
      nameEnd--;
    SetClock(value.c_str() + nameBegin, nameEnd - nameBegin);
    return;
  }

  // Anything else is a time value. A note value (`4nd`) or Max's `<n> ticks`
  // says what unit it is in, so it is read as that whatever the input format is
  // — `timeValue.h`'s shared reader, so this object spells them exactly as
  // `.delay` and `.metro` do.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str(), value.size(), beats)) {
    Take(beats, true, thread);
    return;
  }

  // Otherwise a bare number in the input format, and the whole message has to
  // be that number: a trailing token means this was some other message, and
  // reading its first word would act on half of it.
  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  std::size_t after = end;
  while (after < value.size() && IsSelectorSeparator(value[after]))
    after++;
  if (after != value.size()) return;
  TakeNumber((double)number, thread);
}
