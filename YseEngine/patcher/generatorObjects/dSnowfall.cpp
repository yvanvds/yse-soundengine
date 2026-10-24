#include "dSnowfall.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The next token of `message` from `cursor` on, walked in place so nothing
  // allocates on whichever thread delivers the message. False at the end.
  bool NextToken(const std::string& message, std::size_t& cursor, std::size_t& begin,
                 std::size_t& length) {
    const char* text = message.c_str();
    const std::size_t size = message.size();
    while (cursor < size && IsSelectorSeparator(text[cursor]))
      cursor++;
    begin = cursor;
    while (cursor < size && !IsSelectorSeparator(text[cursor]))
      cursor++;
    length = cursor - begin;
    return length > 0;
  }

  // The attribute keywords of the arguments and messages: first the twelve
  // with a value per dimension, in the order of `SETTERS` below.
  enum keyword : int {
    DIRECTION,
    DIRECTIONDEV,
    ENERGYRAMP,
    INITIAL,
    INITIALDEV,
    MINIMUM,
    MAXIMUM,
    SCALEMIN,
    SCALEMAX,
    SQUISH,
    WANDERPROB,
    ENDVALUE,
    INTERVAL,
    INTERVALDEV,
    BOUNDARYMODE,
    ENDMODE,
    STARTMODE,
    DIMENSIONS,
    NONE
  };

  using setter = YSE::DSP::snowfall& (YSE::DSP::snowfall::*)(UInt, Flt);
  constexpr setter SETTERS[ENDVALUE + 1] = {
      &YSE::DSP::snowfall::direction,         &YSE::DSP::snowfall::directionDeviation,
      &YSE::DSP::snowfall::energyRamp,        &YSE::DSP::snowfall::initial,
      &YSE::DSP::snowfall::initialDeviation,  &YSE::DSP::snowfall::minimum,
      &YSE::DSP::snowfall::maximum,           &YSE::DSP::snowfall::scaleMinimum,
      &YSE::DSP::snowfall::scaleMaximum,      &YSE::DSP::snowfall::squish,
      &YSE::DSP::snowfall::wanderProbability, &YSE::DSP::snowfall::endValue,
  };

  // The DSP class's defaults, per attribute.
  constexpr float DEFAULTS[ENDVALUE + 1] = {1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                            1.f, 0.f, 1.f, 1.f, 0.f, 0.f};

  int ToKeyword(const char* text, std::size_t length) {
    const std::string_view word(text, length);
    if (word == "direction") return DIRECTION;
    if (word == "directiondev") return DIRECTIONDEV;
    if (word == "energyramp") return ENERGYRAMP;
    if (word == "initial") return INITIAL;
    if (word == "initialdev") return INITIALDEV;
    if (word == "min") return MINIMUM;
    if (word == "max") return MAXIMUM;
    if (word == "scalemin") return SCALEMIN;
    if (word == "scalemax") return SCALEMAX;
    if (word == "squish") return SQUISH;
    if (word == "wanderprob") return WANDERPROB;
    if (word == "endvalue") return ENDVALUE;
    if (word == "interval") return INTERVAL;
    if (word == "intervaldev") return INTERVALDEV;
    if (word == "boundarymode") return BOUNDARYMODE;
    if (word == "endmode") return ENDMODE;
    if (word == "startmode") return STARTMODE;
    if (word == "dimensions") return DIMENSIONS;
    return NONE;
  }

  // The number of dimensions `number` names, or 0 when it names none.
  unsigned int ToDimensions(float number) {
    if (!(number >= 1.f && number < 5.f)) return 0;
    return static_cast<unsigned int>(number);
  }
} // namespace

#define className dSnowfall

CONSTRUCT_DSP() {
  Defaults();

  // in 0: the ramp (a number while unconnected); messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRamp);
  REG_INT_IN(SetRampInt);
  REG_LIST_IN(SetList);

  ShapePorts(1);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("A particle of one to four values that moves while a 0-1 ramp runs: it starts "
                  "when the ramp starts and ends when it resets, bouncing, wrapping or stopping at "
                  "its boundaries. One voice of a cloud of overlapping events.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "ramp",
            "The ramp that starts and ends each particle, usually a ~phasor (unconnected, the "
            "last number sent here, 0 at first). A particle starts when the ramp rises above 0 "
            "or wraps, and ends when it wraps or goes to 0. With a value per dimension: "
            "'direction' the speed in units per second, 'directiondev' a random deviation "
            "around it, 'energyramp' how much the ramp speeds the motion up (1: twice as fast "
            "at its end), 'initial' the starting value, 'initialdev' a random deviation around "
            "it, 'min' and 'max' the boundaries, 'scalemin' and 'scalemax' the output at them, "
            "'squish' the speed factor of a bounce, 'wanderprob' the 0-1 chance of a reversal "
            "at each update, 'endvalue' the output once a particle ends. With one value: "
            "'interval <ms>' the time between updates (0: none), 'intervaldev <ms>' a random "
            "deviation around it, 'boundarymode <0-3>' at a boundary (0 bounce, 1 end, 2 wrap "
            "to the other side, 3 stay), 'endmode <0-1>' the output once a particle ends (0 "
            "the end value, 1 the last value), 'startmode <0-1>' where a particle starts (0 "
            "the initial value, 1 where the last one left).",
            "0-1");
  PARAM_DOC("dimensions", "1",
            "The number of dimensions and outlets, 1 to 4 (also 'dimensions <n>'; set only "
            "here). The keywords of the inlet's messages follow it, with their values (defaults: "
            "direction 1, deviations 0, energyramp 0, initial 0, min 0, max 1, scalemin 0, "
            "scalemax 1, squish 1, wanderprob 0, endvalue 0, interval 100, boundarymode 0, "
            "endmode 0, startmode 0). Messages are not saved.",
            "1-4, then keywords");
}

void dSnowfall::ShapePorts(unsigned int dims) {
  // Rebuilt rather than resized: the outlet count comes from the arguments.
  dimensionCount = dims;
  fn.dimensions(dims);
  outputs.clear();
  for (unsigned int d = 0; d < dims; ++d) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(d + 1);
    outputs.back().SetDoc(dims == 1 ? std::string("out") : "out " + n,
                          "The value of dimension " + n +
                              " of the particle, scaled to scalemin - scalemax; once it ends, "
                              "the end value or the last value.",
                          "");
  }
}

void dSnowfall::Defaults() {
  for (unsigned int w = 0; w < VECTORS; ++w)
    for (unsigned int d = 0; d < DIMS; ++d)
      vectors[w][d].store(DEFAULTS[w], std::memory_order_relaxed);
  ramp = 0.f;
  interval = 100.f;
  intervaldev = 0.f;
  boundarymode = DSP::snowfall::BOUNCE;
  endmode = DSP::snowfall::END_VALUE;
  startmode = DSP::snowfall::INITIAL;
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  Defaults();
  ShapePorts(1);
}

PARM_PARSE() {
  // A message's form, rebuilt from the tokens: a keyword and the numbers
  // after it. Allocation is fine here, on the control thread.
  unsigned int dims = 1;
  bool first = true;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    const int word = ToKeyword(token.c_str(), token.size());
    float number = 0.f;
    if (word == NONE) {
      // A bare number first is the dimensions, as Max's argument.
      if (first && ReadNumericToken(token, number) && ToDimensions(number) > 0)
        dims = ToDimensions(number);
      first = false;
      continue;
    }
    first = false;
    const std::size_t at = i;
    std::string message = token;
    while (i + 1 < arguments.size() && ReadNumericToken(arguments[i + 1], number)) {
      message += ' ';
      message += arguments[++i];
    }
    if (word == DIMENSIONS) {
      if (i > at && ReadNumericToken(arguments[at + 1], number) && ToDimensions(number) > 0)
        dims = ToDimensions(number);
      continue;
    }
    Apply(message, token.size(), word);
  }
  ShapePorts(dims);
}

void dSnowfall::Apply(const std::string& message, std::size_t cursor, int word) {
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  if (word >= DIRECTION && word <= ENDVALUE) {
    // A value per dimension, in order; a word ends them.
    for (unsigned int d = 0; d < DIMS && NextToken(message, cursor, begin, length); ++d) {
      if (!ReadNumericToken(message.c_str() + begin, length, number)) break;
      if (std::isfinite(number)) vectors[word][d] = number;
    }
    return;
  }
  if (!NextToken(message, cursor, begin, length) ||
      !ReadNumericToken(message.c_str() + begin, length, number) || !std::isfinite(number))
    return;
  switch (word) {
  case INTERVAL:
    interval = number;
    break;
  case INTERVALDEV:
    if (number >= 0.f) intervaldev = number;
    break;
  case BOUNDARYMODE:
    if (number >= 0.f && number < 4.f) boundarymode = static_cast<int>(number);
    break;
  case ENDMODE:
    if (number >= 0.f && number < 2.f) endmode = static_cast<int>(number);
    break;
  case STARTMODE:
    if (number >= 0.f && number < 2.f) startmode = static_cast<int>(number);
    break;
  default: // DIMENSIONS is a creation argument only, as in Max
    break;
  }
}

FLOAT_IN(SetRamp) {
  (void)inlet;
  (void)thread;
  ramp = value;
}

INT_IN(SetRampInt) {
  (void)inlet;
  (void)thread;
  ramp = static_cast<float>(value);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // A word first is a command that came as a list, as text through a
  // receive does; a number first is the ramp's number.
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  float number = 0.f;
  if (!NextToken(value, cursor, begin, length)) return;
  if (ReadNumericToken(value.c_str() + begin, length, number)) {
    ramp = number;
    return;
  }
  SetMessage(value, 0.f);
}

MESSAGES() {
  (void)value;
  std::size_t cursor = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextToken(message, cursor, begin, length)) return;
  const int word = ToKeyword(message.c_str() + begin, length);
  if (word == NONE) return;
  Apply(message, cursor, word);
}

CALC() {
  fn.interval(interval.load())
      .intervalDeviation(intervaldev.load())
      .boundaryMode(static_cast<DSP::snowfall::BOUNDARY_MODE>(boundarymode.load()))
      .endMode(static_cast<DSP::snowfall::END_MODE>(endmode.load()))
      .startMode(static_cast<DSP::snowfall::START_MODE>(startmode.load()));
  for (unsigned int w = 0; w < VECTORS; ++w)
    for (unsigned int d = 0; d < dimensionCount; ++d)
      (fn.*SETTERS[w])(d, vectors[w][d].load(std::memory_order_relaxed));

  // Unconnected (#1036): the last number.
  fn(inputs[0].Signal(ramp.load()));
  for (unsigned int d = 0; d < dimensionCount; ++d)
    outputs[d].SendBuffer(&fn.output(d), thread);
}

#undef className
