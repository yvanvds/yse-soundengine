#include "dConvert.h"
#include "gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

// ─── ~mtof ~ftom ──────────────────────────────────────────────────────────────

#define className dPitchConvertBase

dPitchConvertBase::dPitchConvertBase() : pObject(true) {
  floatIn = 0.f;
  base = 440.f;
  ref = 69.f;

  // in 0: the input (signal, float or int), `base <Hz>` and `ref <note>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the converted signal
  ADD_OUT_BUFFER;

  ADD_PARAM(base);
  ADD_PARAM(ref);

  ADD_CATEGORY(pCategory::MATH);
}

void dPitchConvertBase::Document(const char* summary, const char* inletDoc, const char* inletRange,
                                 const char* outletDoc, const char* outletRange) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, inletRange);
  OUTLET_DOC(0, "out", outletDoc, outletRange);
  PARAM_DOC("base", "440",
            "Frequency of the reference note, in Hz. 0 or below counts as 440. Also set with "
            "'base <Hz>'.",
            "above 0 Hz");
  PARAM_DOC("ref", "69",
            "The MIDI note that sounds at the base frequency. Also set with 'ref <note>'.",
            "any float");
}

FLOAT_IN(SetFloat) {
  floatIn = value;
}

INT_IN(SetInt) {
  floatIn = static_cast<float>(value);
}

// `base <Hz>` and `ref <note>`, walked in place so nothing allocates.
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

  std::size_t arg = end;
  while (arg < length && IsSelectorSeparator(text[arg]))
    arg++;
  std::size_t argEnd = arg;
  while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
    argEnd++;
  float number = 0.f;
  if (!ReadNumericToken(text + arg, argEnd - arg, number)) return;

  if (word == 4 && message.compare(begin, 4, "base") == 0) {
    base = number;
  } else if (word == 3 && message.compare(begin, 3, "ref") == 0) {
    ref = number;
  }
}

CALC() {
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&Process(inputs[0].Signal(floatIn), base, ref), thread);
}

#undef className

dMtof::dMtof() {
  Document("Converts a MIDI note number to a frequency in Hz at signal rate, for pitch-modulated "
           "oscillators. Equal temperament, tuned with base (440 Hz) and ref (note 69).",
           "A MIDI note number, fractions allowed: a signal, or a number used while no signal "
           "is connected. 'base <Hz>' and 'ref <note>' retune.",
           "any float", "The frequency of the note. Never NaN or infinity.", "0 Hz or higher");
}

dFtom::dFtom() {
  Document("Converts a frequency in Hz to a MIDI note number at signal rate. The inverse of ~mtof, "
           "with the same base and ref tuning. A frequency of 0 or below gives -1500.",
           "A frequency in Hz: a signal, or a number used while no signal is connected. "
           "'base <Hz>' and 'ref <note>' retune.",
           "any float", "The MIDI note number, with fractions. Never NaN or infinity.",
           "-1500 or higher");
}

// ─── ~atodb ~dbtoa ────────────────────────────────────────────────────────────

namespace {
  constexpr const char* kConvertSignalIn =
      "The input: a signal, or a number used while no signal is connected.";
} // namespace

dAtoDb::dAtoDb() {
  Document("Converts a linear amplitude to decibels full scale at signal rate: 1 is 0 dB, 0.5 is "
           "about -6 dB. The sign is ignored, and 0 gives -999 dB.",
           kConvertSignalIn, "any float", "The level in dB.", "-999 dB or higher");
}

dDbtoA::dDbtoA() {
  Document("Converts decibels full scale to a linear amplitude at signal rate: 0 dB is 1, -6 dB "
           "about 0.5. -999 dB or less gives 0.",
           kConvertSignalIn, "any float in dB", "The linear amplitude.", "0 or higher");
}

// ─── ~mstosamps ~sampstoms ────────────────────────────────────────────────────

#define className dTimeConvertBase

dTimeConvertBase::dTimeConvertBase() : pObject(true) {
  floatIn = 0.f;
  // Room for LIST_MAX numbers and their separators: the list reply never
  // grows past this, so building it allocates nothing.
  reply.reserve(static_cast<std::size_t>(LIST_MAX) * (kExprValueTextMax + 1));

  // in 0: the input (signal, float, int or list)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);

  // out 0: the converted signal
  ADD_OUT_BUFFER;
  // out 1: converted numbers and lists
  ADD_OUT_ANY;

  ADD_CATEGORY(pCategory::MATH);
}

void dTimeConvertBase::Document(const char* summary, const char* inletDoc, const char* outletDoc,
                                const char* numberDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "any float");
  OUTLET_DOC(0, "out", outletDoc, "any float");
  OUTLET_DOC(1, "number", numberDoc, "any float");
}

FLOAT_IN(SetFloat) {
  floatIn = value;
  // A number is a message, not CALC(), so it may send at once.
  outputs[1].SendFloat(Convert(value), thread);
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

LIST_IN(SetList) {
  (void)inlet;
  float numbers[LIST_MAX];
  const int count = ExprParseFloatList(value.c_str(), numbers, LIST_MAX);
  if (count <= 0) return;

  // Filled immediately before the send, into the memory reserved in the
  // constructor: a cord looping back into this inlet re-enters here inside
  // SendList, and must not find a half-built reply of its own.
  reply.clear();
  for (int i = 0; i < count; ++i) {
    char text[kExprValueTextMax];
    const int written =
        ExprFormatValue(ExprValue::Float(Convert(numbers[i])), text, kExprValueTextMax);
    if (written <= 0) continue;
    if (!reply.empty()) reply.push_back(' ');
    reply.append(text, static_cast<std::size_t>(written));
  }
  outputs[1].SendList(reply, thread);
}

CALC() {
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&Process(inputs[0].Signal(floatIn)), thread);
}

#undef className

dMsToSamps::dMsToSamps() {
  Document("Converts milliseconds to samples at the current sample rate. A number or list in is "
           "also converted at once and sent from the right outlet.",
           "Milliseconds: a signal, or a number used while no signal is connected. A number or "
           "list is also converted to the right outlet.",
           "The time in samples.", "Numbers and lists in, converted to samples.");
}

dSampsToMs::dSampsToMs() {
  Document("Converts samples to milliseconds at the current sample rate. A number or list in is "
           "also converted at once and sent from the right outlet.",
           "Samples: a signal, or a number used while no signal is connected. A number or list "
           "is also converted to the right outlet.",
           "The time in milliseconds.", "Numbers and lists in, converted to milliseconds.");
}
