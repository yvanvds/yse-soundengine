// Voice instancing (issue #1040): `~poly`, `.in`, `.out`, `~in`, `~out`,
// `~thispoly`. See dPoly.h. Making the voices (copying and wiring the
// template) and compiling their render is the patcher's work
// (patcherImplementation::SyncPolyVoicesUnlocked, BuildRenderOrder); what is
// here is the per-block render of the voices, the routing of messages to
// them, and the boundary objects.
#include "dPoly.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include "../inlet.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../math/gExprEval.h"

using namespace YSE::PATCHER;

namespace {
  int Clamp(int value, int lowest, int highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

  // The token starting at or after `from`, walked in place so nothing
  // allocates on whichever thread delivers the message; `next` is where it
  // ends.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t length = text.size();
    std::size_t begin = from;
    while (begin < length && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  // The token at or after `from` as a number; false when there is none.
  bool Number(const std::string& text, std::size_t from, std::size_t& next, float& value) {
    const std::string_view token = Token(text, from, next);
    return !token.empty() && ReadNumericToken(token.data(), token.size(), value);
  }

  std::size_t SkipSeparators(const std::string& text, std::size_t from) {
    while (from < text.size() && IsSelectorSeparator(text[from]))
      from++;
    return from;
  }
} // namespace

// ─── ~poly ────────────────────────────────────────────────────────────────────

#define className dPoly

CONSTRUCT_DSP() {
  busy = std::make_unique<std::atomic<bool>[]>(MAX_VOICES);
  pending = std::make_unique<std::atomic<bool>[]>(MAX_VOICES);
  for (int v = 0; v < MAX_VOICES; v++) {
    busy[v].store(false);
    pending[v].store(false);
  }
  scratch.reserve(MAX_MESSAGE);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(name);
  ADD_PARAM(voiceCount);
  ADD_PARAM(ins);
  ADD_PARAM(outs);
  ADD_PARAM(messages);
  ADD_PARAM(stealArgument);
  Configure();

  ADD_DESCRIPTION("Plays several copies of the subpatch inside it, voices that notes and messages "
                  "are allocated to; their ~out signals are summed on the outlets.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  PARAM_DOC("name", "",
            "A name for the subpatch, for a host to show. The engine does not read it; the "
            "contents are placed inside with SetContainer.",
            "");
  PARAM_DOC("voices", "1", "How many copies of the subpatch play. Changing it rebuilds them.",
            "1-128");
  PARAM_DOC("inlets", "1",
            "How many inlets: inlet N is the signal of the ~in N objects and the messages of the "
            ".in N objects inside.",
            "0-16");
  PARAM_DOC("outlets", "1",
            "How many signal outlets: outlet N is the sum over the voices of the ~out N objects "
            "inside.",
            "0-16");
  PARAM_DOC("messages", "0",
            "How many message outlets, after the signal outlets: what the .out objects inside "
            "send. The last outlet always sends the number of the voice that sent.",
            "0-16");
  PARAM_DOC("steal", "0",
            "0: a note or midinote with no free voice is dropped. Any other value: it goes to "
            "the voice started longest ago. The steal message changes it.",
            "0 or non-zero");
}

// Control thread, on an object no audio thread has seen yet: every change of
// argument is a rebuild, so the pins come from the arguments here, before
// anything is wired, as ~pfft's do.
void dPoly::Configure() {
  voiceCount = Clamp(voiceCount, 1, MAX_VOICES);
  const int inCount = Clamp(ins, 0, MAX_PINS);
  const int outCount = Clamp(outs, 0, MAX_PINS);
  const int messageCount = Clamp(messages, 0, MAX_PINS);
  pool.voices(voiceCount).steal(stealArgument != 0);

  inputs.clear();
  for (int i = 0; i < inCount; i++) {
    inputs.emplace_back(this, false, i);
    REG_SIGNAL_IN;
    REG_BANG_IN(SetBang);
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(i == 0 ? "in 0" : "in",
                         "A signal for the ~in objects with this index inside, and messages for "
                         "the .in objects with this index: to the target voice, or routed by "
                         "note, midinote, setvalue. Also target, mute, steal, allnotesoff, list.",
                         "");
  }
  results.clear();
  outputs.clear();
  for (int o = 0; o < outCount; o++) {
    results.push_back(std::make_unique<DSP::buffer>(STANDARD_BUFFERSIZE));
    *results.back() = 0.f;
    ADD_OUT_BUFFER;
    outputs.back().SetDoc("out", "The sum over every voice of the ~out objects with this index.",
                          "");
  }
  for (int m = 0; m < messageCount; m++) {
    ADD_OUT_ANY;
    outputs.back().SetDoc("message", "What the .out objects with this index inside send.", "");
  }
  ADD_OUT_INT;
  outputs.back().SetDoc(
      "voice", "The number of the voice whose .out sends, before the message itself.", "1-128");
}

PARM_CLEAR() {
  name.clear();
  voiceCount = 1;
  ins = 1;
  outs = 1;
  messages = 0;
  stealArgument = 0;
  Configure();
}

PARM_PARSE() {
  Configure();
}

bool dPoly::Busy(int voice) const {
  if (voice < 1 || voice > MAX_VOICES) return false;
  return busy[voice - 1].load(std::memory_order_acquire);
}

void dPoly::SetBusy(int voice, bool on) {
  if (voice < 1 || voice > MAX_VOICES) return;
  busy[voice - 1].store(on, std::memory_order_release);
}

bool dPoly::Available(int voice) const {
  return !busy[voice].load(std::memory_order_acquire) &&
         !pending[voice].load(std::memory_order_acquire);
}

int dPoly::PitchOfVoice(int voice) const {
  return pool.pitch(voice - 1);
}

bool dPoly::Enter() {
  if (guard.exchange(true, std::memory_order_acq_rel)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void dPoly::Leave() {
  guard.store(false, std::memory_order_release);
}

void dPoly::DeliverPart(const GraphState::polyDomain& d, int voice, int inlet,
                        const std::string& text, std::size_t from, YSE::THREAD thread) {
  for (const GraphState::polyVoiceDomain& v : d.voices) {
    if (voice != 0 && static_cast<polyVoice*>(v.root)->Number() != voice) continue;
    for (pObject* object : v.messageInputs) {
      auto* in = static_cast<gVoiceIn*>(object);
      if (in->Index() == inlet) in->Send(text, from, thread);
    }
  }
}

void dPoly::Mute(const GraphState::polyDomain& d, int voice, bool on, YSE::THREAD thread) {
  voiceEvent event;
  event.kind = voiceEvent::KIND::MUTE;
  event.count = 1;
  event.data[0] = on ? 1 : 0;
  for (const GraphState::polyVoiceDomain& v : d.voices) {
    auto* root = static_cast<polyVoice*>(v.root);
    if (voice != 0 && root->Number() != voice) continue;
    root->SetMute(on);
    for (pObject* sink : v.sinks) {
      sink->DeliverVoiceEvent(event, thread);
    }
  }
}

// The routing words (issue #1040). `rest` is where the text after `word`
// starts. Under the guard for the words that touch the allocator or the
// scratch string.
bool dPoly::Route(std::string_view word, const std::string& text, std::size_t rest, int inlet,
                  YSE::THREAD thread) {
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;
  std::size_t next = rest;
  float a = 0.f;
  float b = 0.f;

  if (word == "target") {
    if (!Number(text, rest, next, a)) return true;
    const int n = ExprToInt(a);
    // Max: a number above the voice count is the highest voice.
    target.store(n < 0 ? -1 : std::min(n, voiceCount), std::memory_order_relaxed);
    return true;
  }
  if (word == "mute") {
    if (!Number(text, rest, next, a)) return true;
    if (!Number(text, next, next, b)) b = 1.f;
    if (d != nullptr) Mute(*d, ExprToInt(a), b != 0.f, thread);
    return true;
  }
  if (word == "steal") {
    if (!Number(text, rest, next, a)) a = 1.f;
    if (!Enter()) return true;
    pool.steal(a != 0.f);
    Leave();
    return true;
  }
  if (word == "note") {
    if (d == nullptr || !Enter()) return true;
    const int v = pool.choose([this](int voice) { return !Available(voice); });
    if (v != DSP::voiceAllocator::NO_VOICE) {
      pool.touch(v);
      pending[v].store(true, std::memory_order_release);
      DeliverPart(*d, v + 1, inlet, text, SkipSeparators(text, rest), thread);
    }
    Leave();
    return true;
  }
  if (word == "midinote") {
    if (!Number(text, rest, next, a) || !Number(text, next, next, b)) return true;
    if (d == nullptr || !Enter()) return true;
    const int pitch = ExprToInt(a);
    int v = DSP::voiceAllocator::NO_VOICE;
    if (ExprToInt(b) > 0) {
      v = pool.noteOn(pitch, [this](int voice) { return !Available(voice); }).voice;
      if (v != DSP::voiceAllocator::NO_VOICE) pending[v].store(true, std::memory_order_release);
    } else {
      v = pool.noteOff(pitch);
    }
    if (v != DSP::voiceAllocator::NO_VOICE) {
      DeliverPart(*d, v + 1, inlet, text, SkipSeparators(text, rest), thread);
    }
    Leave();
    return true;
  }
  if (word == "allnotesoff") {
    if (d == nullptr || !Enter()) return true;
    // Freed first, then sent, in ascending voice order, from a stack table:
    // a send may come back here, and the guard drops it.
    int voices[MAX_VOICES];
    int pitches[MAX_VOICES];
    int count = 0;
    pool.releaseAll([&](int voice, int pitch) {
      voices[count] = voice;
      pitches[count] = pitch;
      count++;
    });
    for (int n = 0; n < count; n++) {
      char digits[FORMAT_INT_WIDTH];
      const std::size_t written = WriteInt(pitches[n], digits);
      scratch.assign(digits, written);
      scratch.append(" 0");
      DeliverPart(*d, voices[n] + 1, inlet, scratch, 0, thread);
    }
    Leave();
    return true;
  }
  if (word == "setvalue") {
    if (!Number(text, rest, next, a)) return true;
    const int v = ExprToInt(a);
    if (d == nullptr || v < 0 || !Enter()) return true;
    DeliverPart(*d, v, inlet, text, SkipSeparators(text, next), thread);
    Leave();
    return true;
  }
  if (word == "list") {
    const int t = Target();
    if (d == nullptr || t < 0 || !Enter()) return true;
    DeliverPart(*d, t, inlet, text, SkipSeparators(text, rest), thread);
    Leave();
    return true;
  }
  return false;
}

BANG_IN(SetBang) {
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;
  const int t = Target();
  if (d == nullptr || t < 0) return;
  for (const GraphState::polyVoiceDomain& v : d->voices) {
    if (t != 0 && static_cast<polyVoice*>(v.root)->Number() != t) continue;
    for (pObject* object : v.messageInputs) {
      auto* in = static_cast<gVoiceIn*>(object);
      if (in->Index() == inlet) in->SendBang(thread);
    }
  }
}

INT_IN(SetInt) {
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;
  const int t = Target();
  if (d == nullptr || t < 0) return;
  for (const GraphState::polyVoiceDomain& v : d->voices) {
    if (t != 0 && static_cast<polyVoice*>(v.root)->Number() != t) continue;
    for (pObject* object : v.messageInputs) {
      auto* in = static_cast<gVoiceIn*>(object);
      if (in->Index() == inlet) in->SendInt(value, thread);
    }
  }
}

FLOAT_IN(SetFloat) {
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;
  const int t = Target();
  if (d == nullptr || t < 0) return;
  for (const GraphState::polyVoiceDomain& v : d->voices) {
    if (t != 0 && static_cast<polyVoice*>(v.root)->Number() != t) continue;
    for (pObject* object : v.messageInputs) {
      auto* in = static_cast<gVoiceIn*>(object);
      if (in->Index() == inlet) in->SendFloat(value, thread);
    }
  }
}

LIST_IN(SetList) {
  std::size_t next = 0;
  const std::string_view word = Token(value, 0, next);
  if (word.empty()) return;
  float number = 0.f;
  // A number first is data; a word first may be a routing word.
  if (!ReadNumericToken(word.data(), word.size(), number) &&
      Route(word, value, next, inlet, thread)) {
    return;
  }
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;
  const int t = Target();
  if (d == nullptr || t < 0) return;
  for (const GraphState::polyVoiceDomain& v : d->voices) {
    if (t != 0 && static_cast<polyVoice*>(v.root)->Number() != t) continue;
    for (pObject* object : v.messageInputs) {
      auto* in = static_cast<gVoiceIn*>(object);
      if (in->Index() == inlet) in->SendList(value, thread);
    }
  }
}

void dPoly::VoiceBang(int voice, int index, YSE::THREAD thread) {
  if (index < 0 || index >= MessageOutlets()) return;
  outputs.back().SendInt(voice, thread);
  outputs[static_cast<std::size_t>(SignalOutlets()) + static_cast<std::size_t>(index)].SendBang(
      thread);
}

void dPoly::VoiceInt(int voice, int index, int value, YSE::THREAD thread) {
  if (index < 0 || index >= MessageOutlets()) return;
  outputs.back().SendInt(voice, thread);
  outputs[static_cast<std::size_t>(SignalOutlets()) + static_cast<std::size_t>(index)].SendInt(
      value, thread);
}

void dPoly::VoiceFloat(int voice, int index, float value, YSE::THREAD thread) {
  if (index < 0 || index >= MessageOutlets()) return;
  outputs.back().SendInt(voice, thread);
  outputs[static_cast<std::size_t>(SignalOutlets()) + static_cast<std::size_t>(index)].SendFloat(
      value, thread);
}

void dPoly::VoiceList(int voice, int index, const std::string& value, YSE::THREAD thread) {
  if (index < 0 || index >= MessageOutlets()) return;
  outputs.back().SendInt(voice, thread);
  outputs[static_cast<std::size_t>(SignalOutlets()) + static_cast<std::size_t>(index)].SendList(
      value, thread);
}

CALC() {
  const graphReadScope scope(this);
  const GraphState* g = scope.Graph();
  const GraphState::polyDomain* d = g != nullptr ? g->Poly(this) : nullptr;

  // The inputs; an unconnected one is silence (numbers sent to an inlet are
  // messages for the voices, not a constant).
  DSP::buffer* in[MAX_PINS] = {};
  UInt length = STANDARD_BUFFERSIZE;
  for (std::size_t k = 0; k < inputs.size(); k++) {
    in[k] = &inputs[k].Signal(0.f);
    length = std::min(length, in[k]->getLength());
  }
  for (auto& r : results) {
    // Within the capacity set at construction: no allocation.
    if (r->getLength() != length) r->resize(length);
    *r = 0.f;
  }

  if (d != nullptr) {
    // Arrivals store and wait for the walk, as in the patcher's own render
    // (issue #1185); normally already open around this step.
    const renderOrderScope order;
    for (const GraphState::polyVoiceDomain& voice : d->voices) {
      // A muted voice does not run at all (#1137's mute word, both bits).
      if (static_cast<const polyVoice*>(voice.root)->Muted()) continue;
      for (pObject* object : voice.signalInputs) {
        auto* vin = static_cast<dVoiceIn*>(object);
        const int k = vin->Index();
        vin->Load(k >= 0 && static_cast<std::size_t>(k) < inputs.size()
                      ? in[static_cast<std::size_t>(k)]
                      : nullptr);
      }
      for (const GraphState::renderStep& step : voice.renderOrder) {
        if (g->Muted(step.muteBegin, step.muteEnd)) {
          step.object->SendSilence(thread);
        } else if (step.startPoint) {
          step.object->Calculate(thread);
        } else if (thread != YSE::T_GUI || !step.object->IsDSPObject()) {
          step.object->CalculateIfReady(thread);
        }
      }
      for (pObject* object : voice.signalOutputs) {
        const auto* out = static_cast<const dVoiceOut*>(object);
        const int o = out->Index();
        if (o >= 0 && static_cast<std::size_t>(o) < results.size()) {
          out->AddTo(*results[static_cast<std::size_t>(o)]);
        }
      }
    }
  }
  // A voice given a note counts as busy until it has run once, so its
  // ~thispoly has had a block to say so.
  for (int v = 0; v < voiceCount; v++) {
    pending[v].store(false, std::memory_order_release);
  }

  for (std::size_t o = results.size(); o-- > 0;) {
    outputs[o].SendBuffer(results[o].get(), thread);
  }
}

#undef className

// ─── .in ──────────────────────────────────────────────────────────────────────

#define className gVoiceIn

CONSTRUCT() {
  part.reserve(dPoly::MAX_MESSAGE);
  ADD_OUT_ANY;
  ADD_PARAM(index);

  ADD_DESCRIPTION("Inside a ~poly: the messages sent to one of its inlets, as the ~poly routes "
                  "them to this voice.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  OUTLET_DOC(0, "message", "Whatever the ~poly passes this voice on inlet index.", "");
  PARAM_DOC("index", "0", "Which inlet of the ~poly this reads, from 0.", "0-15");
}

void gVoiceIn::SendBang(YSE::THREAD thread) {
  outputs[0].SendBang(thread);
}

void gVoiceIn::SendInt(int value, YSE::THREAD thread) {
  outputs[0].SendInt(value, thread);
}

void gVoiceIn::SendFloat(float value, YSE::THREAD thread) {
  outputs[0].SendFloat(value, thread);
}

void gVoiceIn::SendList(const std::string& value, YSE::THREAD thread) {
  outputs[0].SendList(value, thread);
}

// What `text` from `from` spells, typed as inlet::SetMessage types a message
// box's text: nothing or `bang` a bang, one number an int or a float, anything
// else a list (word-first lists reach a command channel downstream).
void gVoiceIn::Send(const std::string& text, std::size_t from, YSE::THREAD thread) {
  std::size_t next = 0;
  const std::string_view first = Token(text, from, next);
  const bool single = SkipSeparators(text, next) == text.size();
  if (first.empty() || (single && first == "bang")) {
    SendBang(thread);
    return;
  }
  float number = 0.f;
  if (single && ReadNumericToken(first.data(), first.size(), number)) {
    if (TokenLooksLikeFloat(first.data(), first.size())) {
      SendFloat(number, thread);
    } else {
      SendInt(ExprToInt(number), thread);
    }
    return;
  }
  if (from == 0) {
    SendList(text, thread);
    return;
  }
  // A part of the ~poly's text (after `note`, `setvalue` ...): copied into a
  // string reserved at construction, never longer than that (dropped). Only
  // the ~poly's routing words send a part, under its guard, so one thread at
  // a time and never re-entrantly.
  const std::size_t start = static_cast<std::size_t>(first.data() - text.c_str());
  if (text.size() - start > dPoly::MAX_MESSAGE) return;
  part.assign(text, start, std::string::npos);
  SendList(part, thread);
}

#undef className

// ─── .out ─────────────────────────────────────────────────────────────────────

#define className gVoiceOut

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);
  ADD_PARAM(index);

  ADD_DESCRIPTION("Inside a ~poly: sends a message out of one of its message outlets, after the "
                  "voice's number on its last outlet.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  INLET_DOC(0, "message", "Any message, sent out of the ~poly's message outlet index.", "");
  PARAM_DOC("index", "0", "Which message outlet of the ~poly this sends from, from 0.", "0-15");
}

BANG_IN(SetBang) {
  (void)inlet;
  if (voice_ != nullptr) voice_->Host()->VoiceBang(voice_->Number(), index.load(), thread);
}

INT_IN(SetInt) {
  (void)inlet;
  if (voice_ != nullptr) voice_->Host()->VoiceInt(voice_->Number(), index.load(), value, thread);
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  if (voice_ != nullptr) voice_->Host()->VoiceFloat(voice_->Number(), index.load(), value, thread);
}

LIST_IN(SetList) {
  (void)inlet;
  if (voice_ != nullptr) voice_->Host()->VoiceList(voice_->Number(), index.load(), value, thread);
}

#undef className

// ─── ~in ──────────────────────────────────────────────────────────────────────

#define className dVoiceIn

CONSTRUCT_DSP() {
  silence.resize(STANDARD_BUFFERSIZE);
  silence = 0.f;
  ADD_OUT_BUFFER;
  ADD_PARAM(index);

  ADD_DESCRIPTION("Inside a ~poly: the signal on one of its inlets, the same in every voice.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  OUTLET_DOC(0, "signal", "The ~poly's inlet index, passed on without a copy.", "");
  PARAM_DOC("index", "0", "Which inlet of the ~poly this reads, from 0.", "0-15");
}

CALC() {
  outputs[0].SendBuffer(loaded != nullptr ? loaded : &silence, thread);
}

#undef className

// ─── ~out ─────────────────────────────────────────────────────────────────────

#define className dVoiceOut

CONSTRUCT_DSP() {
  inputs.emplace_back(this, false, 0);
  REG_SIGNAL_IN;
  ADD_PARAM(index);

  ADD_DESCRIPTION("Inside a ~poly: a signal the ~poly adds, over every voice, to one of its "
                  "signal outlets.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  INLET_DOC(0, "signal", "This voice's part of the ~poly's signal outlet index.", "");
  PARAM_DOC("index", "0", "Which signal outlet of the ~poly this adds to, from 0.", "0-15");
}

RESET()
in = nullptr;
}

CALC() {
  (void)thread;
  in = &inputs[0].Signal(0.f);
}

void dVoiceOut::AddTo(DSP::buffer& sum) const {
  if (in == nullptr) return;
  const UInt n = std::min(sum.getLength(), in->getLength());
  Flt* to = sum.getPtr();
  const Flt* from = in->getPtr();
  for (UInt i = 0; i < n; i++)
    to[i] += from[i];
}

#undef className

// ─── ~thispoly ────────────────────────────────────────────────────────────────

#define className dThisPoly

CONSTRUCT_DSP() {
  inputs.emplace_back(this, false, 0);
  REG_SIGNAL_IN;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);
  ADD_OUT_INT;
  ADD_OUT_INT;
  ADD_OUT_INT;

  ADD_DESCRIPTION("Inside a ~poly: sets the voice busy or free for note allocation, mutes it, "
                  "and reports its number.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
  INLET_DOC(0, "busy",
            "A number: non-zero busy, 0 free; a busy voice gets no note or midinote. A signal: "
            "busy while any sample is non-zero. 'mute 1' / 'mute 0' mutes and unmutes the "
            "voice. A bang reports the voice count and number.",
            "");
  OUTLET_DOC(0, "voice", "The voice number on a bang, from 1; 0 outside a ~poly.", "0-128");
  OUTLET_DOC(1, "mute", "1 when the voice is muted, 0 when unmuted, as it changes.", "0 or 1");
  OUTLET_DOC(2, "voices", "The ~poly's voice count on a bang; 1 outside a ~poly.", "1-128");
}

void dThisPoly::SetBusyFlag(bool on) {
  if (voice_ != nullptr) voice_->Host()->SetBusy(voice_->Number(), on);
}

BANG_IN(SetBang) {
  (void)inlet;
  outputs[2].SendInt(voice_ != nullptr ? voice_->Host()->Voices() : 1, thread);
  outputs[0].SendInt(voice_ != nullptr ? voice_->Number() : 0, thread);
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  SetBusyFlag(value != 0);
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  SetBusyFlag(value != 0.f);
}

LIST_IN(SetList) {
  (void)inlet;
  std::size_t next = 0;
  const std::string_view word = Token(value, 0, next);
  float number = 0.f;
  if (word == "mute") {
    if (!Number(value, next, next, number)) number = 1.f;
    if (voice_ == nullptr) return;
    voice_->SetMute(number != 0.f);
    outputs[1].SendInt(number != 0.f ? 1 : 0, thread);
    return;
  }
  if (word == "busy") {
    if (!Number(value, next, next, number)) number = 1.f;
    SetBusyFlag(number != 0.f);
    return;
  }
  // A list of numbers: the first is the busy flag.
  if (!word.empty() && ReadNumericToken(word.data(), word.size(), number)) {
    SetBusyFlag(number != 0.f);
  }
}

void dThisPoly::DeliverVoiceEvent(const voiceEvent& event, YSE::THREAD thread) {
  if (event.kind == voiceEvent::KIND::MUTE && event.count > 0) {
    outputs[1].SendInt(event.data[0] != 0 ? 1 : 0, thread);
  }
}

CALC() {
  (void)thread;
  // A signal sets the flag each block; with no signal cord the messages do.
  if (inputs[0].IsConstant()) return;
  DSP::buffer& b = inputs[0].Signal(0.f);
  const Flt* p = b.getPtr();
  bool any = false;
  for (UInt i = 0; i < b.getLength(); i++) {
    if (p[i] != 0.f) {
      any = true;
      break;
    }
  }
  SetBusyFlag(any);
}

#undef className
