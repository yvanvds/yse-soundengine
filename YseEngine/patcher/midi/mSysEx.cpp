// `.sysexin` and `.sxformat` (issue #531). See mSysEx.h for the design; this
// file is the template compiler, the message builder and the input filter.
#include "mSysEx.h"

#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // System exclusive on the wire, spelled here rather than shared with
  // mMidiCodec.cpp or mMidiIn.cpp: those two are compiled under different
  // conditions from this one and from each other, so a shared header would be
  // the only way to link them — for four constants that have not moved since
  // 1983.
  constexpr unsigned char kSysExStart = 0xF0;
  constexpr unsigned char kSysExEnd = 0xF7;
  constexpr unsigned char kFirstStatus = 0x80;
  constexpr unsigned char kFirstRealTime = 0xF8;

  constexpr int kByteMax = 255;
  constexpr int kDataMax = 127;

  // Appends one number to a list being built, with the separator a list needs
  // between its atoms and none in front of its first. Allocation-free as long
  // as the caller reserved the string, which the constructor does; `WriteInt`
  // is the patcher's own decimal writer, which neither allocates nor reads
  // locale state.
  void AppendNumber(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    if (!out.empty()) out.push_back(' ');
    out.append(digits, written);
  }

  // Reads `$i<n>` and hands back the placeholder's index, 0-based. False for
  // anything else, including `$i0` and `$i10` — Max's placeholders are 1..9 and
  // a tenth one would silently become a first.
  bool ReadPlaceholder(const std::string& text, int& index) {
    if (text.size() != 3) return false;
    if (text[0] != '$' || text[1] != 'i') return false;
    if (text[2] < '1' || text[2] > '9') return false;
    index = (text[2] - '1');
    return true;
  }

  constexpr char kTemplateDoc[] =
      "The message, one token per byte: a number 0-255, a placeholder $i1 to $i9 filled from the "
      "inlets, \"sumstart\" to start the checksum region, or \"sum\" for the checksum byte.";

} // namespace

// ─── .sxformat ──────────────────────────────────────────────────────────────

#define className mSxFormat

CONSTRUCT() {
  // Room for all nine inlets up front, so ParseParams' emplace_back can never
  // reallocate. A standalone object — a unit test, an embedder driving pObject
  // directly — can be wired before it is re-parsed, and a reallocation would
  // leave its peers holding inlet pointers into freed storage. `.expr` reserves
  // for the same reason.
  inputs.reserve(kSxMaxVars);

  // The left inlet always exists: a template of nothing but constants is still
  // a message, and banging it is how a patch sends a fixed request.
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(SetBang);
  REG_LIST_IN(SetList);

  ADD_OUT_LIST;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(sysex);

  // The object's whole allocation, taken on the control thread before it is
  // wired: from here on a send only ever appends into storage that exists.
  scratch.reserve(static_cast<std::size_t>(MAX_BYTES) * (FORMAT_INT_WIDTH + 1));

  ADD_DESCRIPTION("Builds a system-exclusive message from a template of constant bytes, "
                  "placeholders filled from the inlets, and an optional checksum.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "$i1",
            "Value for $i1; sends the message. A list fills $i1, $i2 and so on, then sends; a bang "
            "sends with the stored values. A negative value leaves its byte out; above 127 is "
            "clamped.",
            "-1 to 127");
  OUTLET_DOC(0, "sysex", "The message as a list of byte values, such as 240 67 0 1 247.", "0-255");
  PARAM_DOC("sysex", "", kTemplateDoc, "0-255, $i1-$i9, sumstart, sum");
}

PARM_CLEAR() {
  // Back to the bare object the constructor built: one inlet, no template. The
  // stored values go too — a value received for an inlet the new template
  // re-uses for something else would otherwise leak across, and a placeholder
  // that has never been given a value stands for 0.
  while (inputs.size() > 1) {
    inputs.pop_back();
  }
  for (int i = 0; i < kSxMaxVars; i++) {
    vars[i] = 0;
  }
  sysex.clear();
  programSize = 0;
  compileError.clear();
}

PARM_PARSE() {
  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place, because the clear/parse callbacks
  // above make ParamsNeedRebuild() true (issue #234).
  int highest = 0;

  for (std::size_t i = 0; i < sysex.size(); i++) {
    const std::string& text = sysex[i];
    if (text.empty()) continue;

    if (programSize >= MAX_BYTES) {
      // Reported rather than silently cut at send time: a dump that is wrong by
      // its tail is the kind of thing that corrupts a synthesiser's memory, so
      // it must not be discoverable only by listening.
      compileError = "template is longer than " + std::to_string(MAX_BYTES) + " tokens";
      INTERNAL::LogImpl().emit(E_ERROR, "patcher: .sxformat " + compileError + "; the rest is cut");
      break;
    }

    token& slot = program[programSize];
    int placeholder = 0;
    float number = 0.f;

    if (ReadPlaceholder(text, placeholder)) {
      slot.what = token::kind::Var;
      slot.value = static_cast<unsigned char>(placeholder);
      if (placeholder + 1 > highest) highest = placeholder + 1;
    } else if (text == "sum") {
      slot.what = token::kind::Checksum;
      slot.value = 0;
    } else if (text == "sumstart") {
      slot.what = token::kind::SumStart;
      slot.value = 0;
    } else if (ReadNumericToken(text, number) && number >= 0.f && number <= (float)kByteMax) {
      slot.what = token::kind::Literal;
      slot.value = static_cast<unsigned char>((int)number);
    } else {
      // Skipped rather than guessed at: reading an unknown token as a byte would
      // put a number the patch never wrote into the middle of a dump.
      compileError = "\"" + text + "\" is not a byte, a $i placeholder, sumstart or sum";
      INTERNAL::LogImpl().emit(E_ERROR, "patcher: .sxformat ignores " + compileError);
      continue;
    }
    programSize++;
  }

  // One inlet past the highest placeholder the template mentions, and never
  // fewer than the one the constructor made.
  while ((int)inputs.size() < highest) {
    const std::size_t index = inputs.size();
    inputs.emplace_back(this, false, (int)index);
    REG_INT_IN(SetInt);
    REG_FLOAT_IN(SetFloat);
    inputs.back().SetDoc("$i" + std::to_string(index + 1),
                         "Value for the placeholder with this number, stored for the next message. "
                         "A negative value leaves its byte out; above 127 is clamped.",
                         "-1 to 127");
  }
}

INT_IN(SetInt) {
  (void)thread;
  if (inlet >= 0 && inlet < kSxMaxVars) vars[inlet] = value;
}

FLOAT_IN(SetFloat) {
  SetInt((int)value, inlet, thread);
}

BANG_IN(SetBang) {
  // Nothing to store; inlet 0 is the hot one, so the send follows.
  (void)inlet;
  (void)thread;
}

LIST_IN(SetList) {
  (void)inlet;
  // Max: the items of a list in the left inlet are treated as if each had come
  // in a different inlet. Items past the ninth are dropped, and a placeholder
  // the list does not reach keeps the value it had.
  //
  // Read with the patcher's own integer reader rather than by splitting into
  // strings: a list can arrive on the audio thread, where allocating is not
  // allowed.
  std::size_t cursor = 0;
  int number = 0;
  int index = 0;
  while (index < kSxMaxVars && ReadIntArgAt(value, cursor, number)) {
    vars[index] = number;
    index++;
  }
}

void mSxFormat::Emit(unsigned char byte) {
  // The leading status byte is not part of a Roland checksum region, and a
  // template that starts anywhere else has nothing to exclude.
  if (!(emitted == 0 && byte == kSysExStart)) checksum += (int)byte;
  AppendNumber(scratch, (int)byte);
  emitted++;
}

CALC() {
  if (!Enter()) return;

  scratch.clear();
  emitted = 0;
  checksum = 0;

  for (int i = 0; i < programSize; i++) {
    const token& slot = program[i];
    switch (slot.what) {
    case token::kind::Literal:
      Emit(slot.value);
      break;
    case token::kind::Var: {
      const int held = vars[slot.value];
      // Max's rule: a negative value emits no byte at all, so one template
      // covers both a short form of a message and a long one. A value above
      // 127 is clamped instead, since a byte with its top bit set inside a
      // dump would end the message early at the receiving device.
      if (held < 0) break;
      Emit(static_cast<unsigned char>(held > kDataMax ? kDataMax : held));
      break;
    }
    case token::kind::Checksum:
      Emit(static_cast<unsigned char>((128 - (checksum & kDataMax)) & kDataMax));
      break;
    case token::kind::SumStart:
      // Emits nothing; it only says where the region starts.
      checksum = 0;
      break;
    }
  }

  if (emitted > 0) outputs[0].SendList(scratch, thread);
  Leave();
}

bool mSxFormat::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired this object's outlet
    // back into its inlet. Counted rather than spun on: this is a path the
    // audio callback takes, and the builder is not re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mSxFormat::Leave() {
  busy.store(false, std::memory_order_release);
}

#undef className

// ─── .sysexin ───────────────────────────────────────────────────────────────

#if YSE_ENABLE_MIDI_DEVICE

mSysExIn::mSysExIn() : mMidiInBase() {
  ADD_OUT_INT;

  ADD_DESCRIPTION("System-exclusive input. Every byte of every system-exclusive message received "
                  "on a MIDI input port leaves as an int, from 240 to 247; other messages are "
                  "filtered out.");
  OUTLET_DOC(0, "byte",
             "Each byte of a system-exclusive message, in arrival order: 240, the manufacturer ID, "
             "the data, then 247.",
             "0-247");
}

void mSysExIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  // A dump arrives as consecutive chunks of at most inEvent::kMaxBytes (see
  // midiInHub.h), which is why the open-message flag is a member and not a
  // local: the message routinely spans many calls.
  for (std::size_t i = 0; i < event.len; i++) {
    const unsigned char byte = event.bytes[i];

    // System real time may appear between any two bytes of anything, including
    // inside a dump. Not ours, and not an ending.
    if (byte >= kFirstRealTime) continue;

    if (byte == kSysExStart) {
      // A second 240 with no 247 in front of it is a device that restarted its
      // dump. The new message begins; the abandoned one has already been sent
      // out byte by byte, which is all a streaming object can do about it.
      inSysEx = true;
      outputs[0].SendInt(static_cast<int>(byte), thread);
      continue;
    }

    if (!inSysEx) continue;

    if (byte == kSysExEnd) {
      inSysEx = false;
      outputs[0].SendInt(static_cast<int>(byte), thread);
      continue;
    }

    // Any other status byte ends an unterminated dump without being reported:
    // it is the start of some other message, and belongs to '.midiin'.
    if (byte >= kFirstStatus) {
      inSysEx = false;
      continue;
    }

    outputs[0].SendInt(static_cast<int>(byte), thread);
  }
}

#endif // YSE_ENABLE_MIDI_DEVICE
