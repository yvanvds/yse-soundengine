#include "dAdsr.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }
} // namespace

#define className dAdsr

CONSTRUCT_DSP() {
  attack = 0.f;
  decay = 0.f;
  sustain = 1.f;
  release = 0.f;
  legato = 0;
  retrigger = 5.f;
  maxsustain = -1.f;
  triggermode = 0;
  amplitude = 0.f;

  // in 0: the trigger (a gate signal, or a float or int event); a list sets
  // attack, decay, sustain and release; the setting messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(Trigger);
  REG_INT_IN(TriggerInt);
  REG_LIST_IN(SetList);

  // in 1-4: attack, decay, sustain and release (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetAttack);
  REG_INT_IN(SetAttackInt);

  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDecay);
  REG_INT_IN(SetDecayInt);

  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetSustain);
  REG_INT_IN(SetSustainInt);

  ADD_IN_4;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRelease);
  REG_INT_IN(SetReleaseInt);

  // out 0: the envelope; out 1: the gate; out 2: the mute state, sent after
  // the block (issue #1037)
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_INT;

  ADD_PARAM(attack);
  ADD_PARAM(decay);
  ADD_PARAM(sustain);
  ADD_PARAM(release);
  ADD_PARAM(legato);
  ADD_PARAM(retrigger);
  ADD_PARAM(maxsustain);
  ADD_PARAM(triggermode);

  ADD_DESCRIPTION("ADSR envelope with its times in milliseconds, triggered by a gate signal or "
                  "by numbers: the standard synth envelope, with legato, a ramp down before a "
                  "retrigger, a sustain limit and a play-to-end mode.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "trigger",
            "Trigger. A signal is a gate: a rise from 0 starts the envelope at the signal's "
            "value, a return to 0 releases it. A number starts it at that amplitude at the next "
            "block, 0 releases it. A list of up to four numbers sets attack, decay, sustain and "
            "release. Also takes 'attack', 'decay', 'sustain', 'release', 'legato', "
            "'retrigger', 'maxsustain' and 'triggermode' with one number.",
            "amplitude, 0 releases");
  INLET_DOC(1, "attack",
            "Attack time in milliseconds: a signal, or a number used while no signal is "
            "connected.",
            "0 or more ms");
  INLET_DOC(2, "decay",
            "Decay time in milliseconds: a signal, or a number used while no signal is "
            "connected.",
            "0 or more ms");
  INLET_DOC(3, "sustain",
            "Sustain level as a factor of the amplitude: a signal, or a number used while no "
            "signal is connected.",
            "factor, usually 0-1");
  INLET_DOC(4, "release",
            "Release time in milliseconds: a signal, or a number used while no signal is "
            "connected.",
            "0 or more ms");
  OUTLET_DOC(0, "out", "The envelope.", "0 to the amplitude");
  OUTLET_DOC(1, "gate",
             "1 during attack, decay and sustain; 0 during release, a retrigger ramp and at rest.",
             "0 or 1");
  OUTLET_DOC(2, "mute",
             "0 after a block in which the envelope started from rest, 1 after one in which it "
             "came to rest.",
             "0 or 1");
  PARAM_DOC("attack", "0", "Attack time used while no signal is connected.", "0 or more ms");
  PARAM_DOC("decay", "0", "Decay time used while no signal is connected.", "0 or more ms");
  PARAM_DOC("sustain", "1", "Sustain factor used while no signal is connected.",
            "factor, usually 0-1");
  PARAM_DOC("release", "0", "Release time used while no signal is connected.", "0 or more ms");
  PARAM_DOC("legato", "0",
            "1 restarts from the current level; 0 ramps down to 0 over the retrigger time first.",
            "0 or 1");
  PARAM_DOC("retrigger", "5", "Ramp-down time before a restart that is not legato.",
            "0 or more ms");
  PARAM_DOC("maxsustain", "-1",
            "Longest sustain in milliseconds; negative holds until released, 0 skips the "
            "sustain.",
            "ms, negative for none");
  PARAM_DOC("triggermode", "0",
            "1 runs every start to the end, ignoring triggers until at rest; 0 releases on a "
            "trigger of 0.",
            "0 or 1");
}

void dAdsr::Event(float value) {
  amplitude = value;
  pending = true;
}

FLOAT_IN(Trigger) {
  (void)inlet;
  (void)thread;
  Event(value);
}

INT_IN(TriggerInt) {
  (void)inlet;
  (void)thread;
  Event(static_cast<float>(value));
}

// Up to four numbers: attack, decay, sustain and release, from the left.
// A token that is not a number leaves its setting alone.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  std::size_t next = 0;
  aFlt* const targets[] = {&attack, &decay, &sustain, &release};
  for (aFlt* target : targets) {
    const std::string_view token = Token(value, next, next);
    if (token.empty()) return;
    float number = 0.f;
    if (ReadNumericToken(token.data(), token.size(), number)) *target = number;
  }
}

FLOAT_IN(SetAttack) {
  (void)inlet;
  (void)thread;
  attack = value;
}

INT_IN(SetAttackInt) {
  (void)inlet;
  (void)thread;
  attack = static_cast<float>(value);
}

FLOAT_IN(SetDecay) {
  (void)inlet;
  (void)thread;
  decay = value;
}

INT_IN(SetDecayInt) {
  (void)inlet;
  (void)thread;
  decay = static_cast<float>(value);
}

FLOAT_IN(SetSustain) {
  (void)inlet;
  (void)thread;
  sustain = value;
}

INT_IN(SetSustainInt) {
  (void)inlet;
  (void)thread;
  sustain = static_cast<float>(value);
}

FLOAT_IN(SetRelease) {
  (void)inlet;
  (void)thread;
  release = value;
}

INT_IN(SetReleaseInt) {
  (void)inlet;
  (void)thread;
  release = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // '<setting> <number>'; a missing or malformed number is ignored.
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  const std::string_view token = Token(message, next, next);
  float number = 0.f;
  if (!ReadNumericToken(token.data(), token.size(), number)) return;
  if (word == "attack")
    attack = number;
  else if (word == "decay")
    decay = number;
  else if (word == "sustain")
    sustain = number;
  else if (word == "release")
    release = number;
  else if (word == "retrigger")
    retrigger = number;
  else if (word == "maxsustain")
    maxsustain = number;
  else if (word == "legato")
    legato = number != 0.f ? 1 : 0;
  else if (word == "triggermode")
    triggermode = number != 0.f ? 1 : 0;
}

CALC() {
  // The DSP class cleans the values: negative or non-finite times are 0, a
  // negative maxsustain is no limit.
  fn.attack(attack.load());
  fn.decay(decay.load());
  fn.sustain(sustain.load());
  fn.release(release.load());
  fn.retrigger(retrigger.load());
  fn.maxSustain(maxsustain.load());
  fn.legato(legato.load() != 0);
  fn.playToEnd(triggermode.load() != 0);
  if (pending.exchange(false)) fn.trigger(amplitude.load());

  // A connected signal wins over the number (issue #1036). An unconnected
  // trigger inlet is no gate: numbers alone trigger.
  DSP::buffer& a = inputs[1].Signal(fn.attack());
  DSP::buffer& d = inputs[2].Signal(fn.decay());
  DSP::buffer& s = inputs[3].Signal(fn.sustain());
  DSP::buffer& r = inputs[4].Signal(fn.release());
  DSP::buffer& out =
      inputs[0].IsConstant() ? fn(a, d, s, r) : fn(inputs[0].Signal(0.f), a, d, s, r);

  // Never Send* a control message from CALC(): the patcher sends it after
  // the traversal. The state at the end of the block is what is armed.
  if (fn.started() || fn.finished()) outputs[2].ArmInt(fn.active() ? 0 : 1);
  outputs[1].SendBuffer(&fn.gate(), thread);
  outputs[0].SendBuffer(&out, thread);
}

#undef className
