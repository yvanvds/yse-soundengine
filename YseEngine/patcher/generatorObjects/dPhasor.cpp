#include "dPhasor.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../time/timeValue.h"

using namespace YSE::PATCHER;

#define className dPhasor

CONSTRUCT_DSP() {
  frequency = 0.f;
  phaseoffset = 0.f;
  periodbeats = 0.f;
  lock = 0;
  syncupdate = 0;
  pendingPhase = 0.f;

  // in 0: frequency (signal, float or int), bang (take a held frequency), and
  // the reset / phaseoffset / interval / lock / syncupdate messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);
  REG_BANG_IN(Update);

  // in 1: phase jump (float or int)
  ADD_IN_1;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: the ramp
  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
  ADD_PARAM(phaseoffset);
  ADD_PARAM(periodbeats);
  ADD_PARAM(lock);
  ADD_PARAM(syncupdate);
  ADD_PARAM(transport);

  ADD_DESCRIPTION("A ramp from 0 to 1 that wraps around at a given frequency: the phase source "
                  "for wavetable reads, sequencers and sawtooth shapes. A negative frequency runs "
                  "it backwards.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal, or a number used while no signal is connected. A note "
            "value such as '4n' or '8nt', or '<n> ticks', sets an interval on the transport "
            "clock instead; a number sets it back to 0. Also takes 'reset' (jump to "
            "phaseoffset), 'phaseoffset <0-1>', 'lock <0/1>', 'syncupdate <0/1>' and bang (take "
            "a held frequency now).",
            "any float, note value");
  INLET_DOC(1, "phase", "Jumps to this phase now. Does not change phaseoffset.", "0-1");
  OUTLET_DOC(0, "out", "The phase ramp.", "0-1");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.", "any float");
  PARAM_DOC("phaseoffset", "0",
            "The phase the ramp starts at, and returns to on 'reset'. With lock on it shifts the "
            "ramp against the clock.",
            "0-1");
  PARAM_DOC("periodbeats", "0",
            "One cycle every this many beats of the transport clock (1 is a quarter note). 0 uses "
            "the frequency in Hz instead.",
            "0+ beats");
  PARAM_DOC("lock", "0",
            "1 locks the ramp to the transport clock's position, so it starts each cycle on the "
            "clock's grid; 0 only follows its tempo. Needs periodbeats above 0.",
            "0 or 1");
  PARAM_DOC("syncupdate", "0",
            "1 holds a new frequency or interval until the ramp wraps to 0, so a change never "
            "lands mid-cycle. Not for a frequency signal, nor while locked.",
            "0 or 1");
  PARAM_DOC("transport", "",
            "The domain clock an interval in beats follows, by name (a .transport, a .setclock "
            "or the host's). Empty follows none, and the ramp then holds.",
            "");
}

void dPhasor::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread. The name is a string parameter, so a live re-parse
  // rebuilds the object and binds again here; nothing binds on the audio
  // thread.
  clock.Bind(Clocks(), transport.c_str(), transport.size());
}

BANG_IN(Update) {
  (void)inlet;
  (void)thread;
  updatePending = true;
}

// A number is a frequency in Hz, and leaves the clock: as on .metro's right
// inlet, it sets the interval in beats back to 0.
FLOAT_IN(SetFrequency) {
  frequency = value;
  periodbeats = 0.f;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
  periodbeats = 0.f;
}

FLOAT_IN(SetPhase) {
  pendingPhase = value;
  phasePending = true;
}

INT_IN(SetPhaseInt) {
  pendingPhase = static_cast<float>(value);
  phasePending = true;
}

// `reset` and `phaseoffset <x>`. The text is walked in place, as inlet.cpp
// does, so this allocates nothing on whichever thread delivers it.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  const std::size_t word = end - begin;

  if (word == 5 && message.compare(begin, 5, "reset") == 0) {
    resetPending = true;
    return;
  }
  std::size_t arg = end;
  while (arg < length && IsSelectorSeparator(text[arg]))
    arg++;
  std::size_t argEnd = arg;
  while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
    argEnd++;
  float number = 0.f;
  const bool hasNumber = ReadNumericToken(text + arg, argEnd - arg, number);

  if (word == 11 && message.compare(begin, 11, "phaseoffset") == 0) {
    if (hasNumber) phaseoffset = number;
    return;
  }
  if (word == 4 && message.compare(begin, 4, "lock") == 0) {
    if (hasNumber) lock = number != 0.f ? 1 : 0;
    return;
  }
  if (word == 10 && message.compare(begin, 10, "syncupdate") == 0) {
    if (hasNumber) syncupdate = number != 0.f ? 1 : 0;
    return;
  }

  // A tempo-relative interval (issue #1278): '4n', '8nt', '960 ticks'.
  // timeValue.h reads it in place, as .metro's right inlet does.
  double beats = 0.0;
  if (ReadBeatTime(text + begin, length - begin, beats) && beats > 0.0) {
    periodbeats = static_cast<float>(beats);
  }
}

CALC() {
  if (!started) {
    phasor.phase(phaseoffset);
    started = true;
  }
  // A reset and a phase jump in the same block: the jump was sent last only
  // if it arrived after the reset, which the flags cannot tell, so the
  // explicit phase wins.
  if (resetPending.exchange(false)) phasor.phase(phaseoffset);
  if (phasePending.exchange(false)) phasor.phase(pendingPhase);
  phasor.syncUpdate(syncupdate.load() != 0);
  if (updatePending.exchange(false)) phasor.update();

  // The clock is read every block it is bound, so the span stays continuous
  // across a switch between Hz and beats (issue #1278).
  const DSP::beatSpan& span = clock.Read(Clocks(), STANDARD_BUFFERSIZE);
  const float beats = periodbeats;
  if (beats > 0.f) {
    DSP::buffer& out = phasor(span, static_cast<Dbl>(beats), lock.load() != 0, phaseoffset);
    outputs[0].SendBuffer(&out, thread);
    return;
  }

  // A connected frequency signal wins over the float (issue #1036).
  DSP::buffer& out =
      inputs[0].IsConstant() ? phasor(frequency) : phasor(inputs[0].Signal(frequency));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
