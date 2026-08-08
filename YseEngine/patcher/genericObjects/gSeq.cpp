#include "gSeq.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace YSE::PATCHER;

#define className gSeq

namespace {

  // A running sum of deltas can reach the top of the int range on a long tape.
  // Saturate rather than wrap: a clock that went negative would be worse than
  // one that stopped. `.mtr`'s helper, for `.mtr`'s reason.
  int SatAdd(int a, int b) {
    const std::int64_t sum = (std::int64_t)a + (std::int64_t)b;
    if (sum > 2147483647LL) return 2147483647;
    if (sum < -2147483648LL) return -2147483647 - 1;
    return (int)sum;
  }

  // `hook`'s edit: a delta times a positive multiplier, rounded and saturated.
  // Clamped before the round rather than after, since casting an out-of-range
  // double to int is undefined behaviour rather than a large int.
  int SatScale(int value, float factor) {
    const double scaled = (double)value * (double)factor;
    if (scaled >= 2147483647.0) return 2147483647;
    if (scaled <= 0.0) return 0;
    return (int)std::llround(scaled);
  }

  // `addeventdelay`'s argument, which Max types as a float, as the whole number
  // of milliseconds a delta is stored as. Clamped for the reason above.
  int RoundToInt(float value) {
    const double wide = (double)value;
    if (wide >= 2147483647.0) return 2147483647;
    if (wide <= -2147483648.0) return -2147483647 - 1;
    return (int)std::llround(wide);
  }

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // Whether the `length` characters at `text` are exactly `word`. Compared in
  // place rather than through a std::string, since this runs on whichever thread
  // the message arrived on.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  // The float argument after a message word — `hook` and `addeventdelay` both
  // take one. Strict, as the rest of the family is: a token that is only partly
  // a number is not a number.
  bool ReadFloatArgAt(const std::string& text, std::size_t offset, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(text.c_str(), text.size(), offset, begin, end)) return false;
    return ReadNumericToken(text.c_str() + begin, end - begin, out);
  }

  // Trim the separators off both ends of [begin, end).
  void Trim(const char* text, std::size_t& begin, std::size_t& end) {
    while (begin < end && IsSelectorSeparator(text[begin]))
      begin++;
    while (end > begin && IsSelectorSeparator(text[end - 1]))
      end--;
  }

  // The integer starting at `cursor` in [cursor, end), or false when there is
  // none there. `cursor` is left after it. In place rather than through
  // ReadIntArgAt, which wants a std::string a file's bytes would have to be
  // copied into: this runs in a file completion, which is the audio thread.
  // Saturating rather than wrapping, and a token that is not one whole integer —
  // `12x` — is refused rather than half read, the way the rest of the family
  // reads a numeric token. `.mtr`'s reader, for `.mtr`'s reason.
  bool ReadIntInPlace(const char* text, std::size_t& cursor, std::size_t end, int& out) {
    std::size_t at = cursor;
    while (at < end && IsSelectorSeparator(text[at]))
      at++;

    bool negative = false;
    if (at < end && (text[at] == '-' || text[at] == '+')) {
      negative = text[at] == '-';
      at++;
    }

    std::int64_t value = 0;
    std::size_t digits = 0;
    while (at < end && text[at] >= '0' && text[at] <= '9') {
      if (value <= 2147483647LL) value = (value * 10) + (text[at] - '0');
      at++;
      digits++;
    }
    if (digits == 0) return false;
    // The token has to end here: a trailing letter means this was never a
    // number, and half reading it would invent a time the file did not have.
    if (at < end && !IsSelectorSeparator(text[at])) return false;

    if (value > 2147483647LL) value = 2147483647LL;
    out = (int)(negative ? -value : value);
    cursor = at;
    return true;
  }

  // ── standard MIDI file bytes (issue #692) ───────────────────────────────────
  //
  // Written here rather than reused from `YseEngine/midi/`. `MIDI::fileImpl` has
  // the same four primitives and cannot lend them: they are anonymous-namespace
  // statics in its own translation unit, its parser takes a filesystem *path*
  // rather than the bytes this object is handed, and it builds and sorts
  // `std::vector`s — on a completion the audio thread runs, where nothing may
  // allocate. See the class documentation.

  constexpr unsigned char META_PREFIX = 0xFF;
  constexpr unsigned char META_END_OF_TRACK = 0x2F;
  constexpr unsigned char META_TEMPO = 0x51;
  constexpr unsigned char SYSEX_BEGIN = 0xF0;
  constexpr unsigned char SYSEX_ESCAPE = 0xF7;

  std::uint16_t ReadU16(const unsigned char* at) {
    return (std::uint16_t)(((std::uint16_t)at[0] << 8) | (std::uint16_t)at[1]);
  }

  std::uint32_t ReadU32(const unsigned char* at) {
    return ((std::uint32_t)at[0] << 24) | ((std::uint32_t)at[1] << 16) |
           ((std::uint32_t)at[2] << 8) | (std::uint32_t)at[3];
  }

  // A variable-length quantity: seven bits per byte, the high bit set on all but
  // the last. Four bytes is the format's own limit, so a corrupt file cannot
  // send this walking off the end looking for a terminator.
  bool ReadVarLen(const unsigned char* data, std::size_t& at, std::size_t end, std::uint32_t& out) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
      if (at >= end) return false;
      const unsigned char byte = data[at];
      at++;
      value = (value << 7) | (std::uint32_t)(byte & 0x7F);
      if ((byte & 0x80) == 0) {
        out = value;
        return true;
      }
    }
    return false;
  }

  // How many data bytes follow a channel status byte: two for everything except
  // program change and channel pressure, which take one.
  std::size_t ChannelDataBytes(unsigned char status) {
    const unsigned char kind = status & 0xF0;
    return (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
  }

  void AppendByte(std::string& out, unsigned char value) {
    out.push_back((char)value);
  }

  void AppendU16(std::string& out, std::uint16_t value) {
    AppendByte(out, (unsigned char)((value >> 8) & 0xFF));
    AppendByte(out, (unsigned char)(value & 0xFF));
  }

  void AppendU32(std::string& out, std::uint32_t value) {
    AppendByte(out, (unsigned char)((value >> 24) & 0xFF));
    AppendByte(out, (unsigned char)((value >> 16) & 0xFF));
    AppendByte(out, (unsigned char)((value >> 8) & 0xFF));
    AppendByte(out, (unsigned char)(value & 0xFF));
  }

  // The other direction of ReadVarLen. Clamped to the four-byte maximum the
  // format allows: a delta longer than that cannot be spelled at all, and one
  // shortened is better than a file no reader will take.
  void AppendVarLen(std::string& out, std::uint32_t value) {
    if (value > 0x0FFFFFFF) value = 0x0FFFFFFF;
    unsigned char buffer[4];
    std::size_t written = 0;
    buffer[written] = (unsigned char)(value & 0x7F);
    written++;
    value >>= 7;
    while (value != 0) {
      buffer[written] = (unsigned char)((value & 0x7F) | 0x80);
      written++;
      value >>= 7;
    }
    while (written > 0) {
      written--;
      AppendByte(out, buffer[written]);
    }
  }

  // Max's names for the meta messages it recognises — "the name of the meta
  // message and the data". Null for anything else, which leaves the type number
  // to stand in for a name rather than the message being swallowed.
  const char* MetaName(unsigned char type) {
    switch (type) {
    case 0x00:
      return "sequencenumber";
    case 0x01:
      return "text";
    case 0x02:
      return "copyright";
    case 0x03:
      return "sequenceortrackname";
    case 0x04:
      return "instrumentname";
    case 0x05:
      return "lyric";
    case 0x06:
      return "marker";
    case 0x07:
      return "cuepoint";
    case 0x20:
      return "midichannelprefix";
    case META_END_OF_TRACK:
      return "endoftrack";
    case META_TEMPO:
      return "tempo";
    case 0x54:
      return "smpteoffset";
    case 0x58:
      return "timesignature";
    case 0x59:
      return "keysignature";
    case 0x7F:
      return "sequencerspecific";
    default:
      return nullptr;
    }
  }

  // Whether a meta's payload is text rather than bytes, which decides how it is
  // spelled out the outlet. Types 1-7 are the specification's text block.
  bool MetaIsText(unsigned char type) {
    return type >= 0x01 && type <= 0x07;
  }

  // A tempo meta's three bytes as microseconds per quarter note.
  std::uint32_t MetaMicros(const std::string& raw) {
    return ((std::uint32_t)(unsigned char)raw[0] << 16) |
           ((std::uint32_t)(unsigned char)raw[1] << 8) | (std::uint32_t)(unsigned char)raw[2];
  }

  // Microseconds per quarter note as Max's beats per minute, and back. A file
  // stores the first; every attribute on this object is the second.
  float TempoFromMicros(std::uint32_t usPerQuarter) {
    if (usPerQuarter == 0) return gSeq::DEFAULT_TEMPO;
    return (float)(60000000.0 / (double)usPerQuarter);
  }

  std::uint32_t MicrosFromTempo(double bpm) {
    if (!(bpm > 0.0)) bpm = (double)gSeq::DEFAULT_TEMPO;
    double micros = 60000000.0 / bpm;
    // Three bytes is all the format has for it.
    if (micros < 1.0) micros = 1.0;
    if (micros > 16777215.0) micros = 16777215.0;
    return (std::uint32_t)std::llround(micros);
  }

  constexpr char kInletDoc[] =
      "The only inlet, and it carries both the bytes and the transport. While the object is "
      "recording, a number here is one raw MIDI byte — Max: 'numbers received in its inlet are "
      "interpreted as bytes of MIDI messages (usually from midiformat or midiin)' — stored with "
      "the gap since the previous byte, measured on the patcher's block clock. A float is "
      "converted to an int, as Max's is, and a number outside 0-255 is not a MIDI byte and is "
      "refused rather than being folded into range. A list of numbers records each of them in "
      "turn: Max has no list method here, but every message in this patcher arrives as a list, so "
      "refusing one would make the object unreachable from midiformat's own output shape. Then the "
      "transport. 'record' starts a fresh take and 'append' carries on at the end of what is "
      "already there — Max: 'starts recording at the end of the stored sequence, without erasing "
      "the existing sequence'. 'start' and a bang both play from the beginning; 'start <n>' sets "
      "the speed, Max's multiplier where 1024 is the recorded tempo, 512 half of it and 2048 twice "
      "it, and the speed can only be set here, at the moment playback starts. 'start -1' plays on "
      "'tick' messages instead of on the clock — Max: 'seq must receive 48 tick messages per "
      "second' to play at the recorded tempo — which is how a patch drives the sequence from its "
      "own timing source. 'stop' ends recording or playing, and neither 'record' nor 'start' needs "
      "one first. 'clear' erases the tape. 'delay <ms>' sets the onset of the first event and "
      "shifts everything after it, 'addeventdelay <ms>' adds to that onset, and 'hook <f>' "
      "multiplies every event time, which Max allows even mid-playback. 'read [file]' loads a "
      "sequence and 'write [file] [format]' saves one, and neither opens anything here: the "
      "request "
      "is a wait-free claim on a patcher-owned slot, the disk work runs on the background pool, "
      "and "
      "a read replaces the sequence in the completion the patcher delivers at the top of a later "
      "block, which is also when the last outlet bangs. A read takes both of Max's formats — a "
      "standard MIDI file, format 0 or 1, and Max's text form of a start time in milliseconds "
      "followed by the bytes of a MIDI message recorded at that time — and stops the transport, "
      "since a sequence still playing would have a step armed at a delta belonging to a tape that "
      "no longer exists. A write always produces a standard MIDI file, as Max's does, format 0 "
      "unless a non-zero format argument asks for Max's 'multi-track (format 1)' one; a trailing "
      "bare integer is that argument rather than part of the name, so a name with spaces still "
      "works as long as it does not end in one. Both bare forms reuse the last name given, Max's "
      "opening a file dialog a headless patcher has none of, and Max documents no readagain / "
      "writeagain for seq, so neither is invented. 'tempo <f>' and 'overridetempo <n>' are Max's "
      "attributes and describe a sequence read from a file: the first is the tempo in force, the "
      "second whether it wins over the tempo the file asks for, and it applies to the next read "
      "rather than to the sequence already loaded. 'sequencetempo' is Max's read-only value and is "
      "accepted without doing anything. 'dump' and 'print' are accepted and do nothing: dump opens "
      "a file in an editing window this patcher is headless for, and printing would allocate and "
      "lock on a path that may be the audio callback. Anything else does nothing, which is Max.";

  constexpr char kByteOutletDoc[] =
      "The sequence, one raw MIDI byte at a time — Max: 'the sequence stored in seq is sent out "
      "the outlet in the form of individual MIDI bytes, usually to be sent to midiparse or "
      "midiout'. Bytes recorded at the same moment leave in the same audio block rather than one "
      "per block: the three bytes of a note-on delivered a block apart would not be that note-on, "
      "so a step keeps going for as long as the next event's gap scales to nothing, and only waits "
      "for a real one.";

  constexpr char kEndOutletDoc[] =
      "Bangs when the sequence finishes — Max: 'indicates that seq has finished playing the "
      "current sequence'. Max's parenthesis is reproduced exactly, because it is surprising and "
      "because it is useful: '(the bang is sent out immediately before the final event of the "
      "sequence is played)'. So this fires first and the last byte follows it, which lets a patch "
      "know that the byte about to arrive is the last one rather than finding out afterwards. A "
      "'start' on an empty sequence bangs nothing: there is no final event for the bang to "
      "precede.";

  constexpr char kMetaOutletDoc[] =
      "The MIDI-file meta messages of the sequence, at their place in it — Max's rightmost outlet, "
      "'if the current sequence loaded by seq contains MIDI meta messages, these are sent from the "
      "right outlet, prepended with the word meta, followed by the name of the meta message and "
      "the "
      "data'. Max's fifteen names are used where it has one — sequencenumber, text, copyright, "
      "sequenceortrackname, instrumentname, lyric, marker, cuepoint, midichannelprefix, "
      "endoftrack, "
      "tempo, smpteoffset, timesignature, keysignature and sequencerspecific — and any other type "
      "sends its type number in the name's place rather than being swallowed. A text meta sends "
      "its "
      "text, with control characters replaced by spaces so the message stays one message; a tempo "
      "meta sends beats per minute, which is what the tempo attribute is in, rather than the "
      "file's "
      "microseconds per quarter note; everything else sends its bytes as numbers. Meta events live "
      "in the tape beside the bytes rather than on a timeline of their own, so 'hook', 'delay' and "
      "'addeventdelay' move them with the music, tick mode plays them, and a 'write' puts them "
      "back "
      "where they were. The one exception is endoftrack, which is delivered like any other because "
      "Max names it but is not written back: every chunk a write produces ends with its own, and "
      "one carried in the middle of a chunk is not something a reader will take. Silent for a "
      "recorded sequence, which is Max: a meta event is a file "
      "construct and cannot arrive from a MIDI port. Appended rather than inserted, which the "
      "family's rule demands and which here is also Max's own position for it, so no saved patch's "
      "cords shift either way.";

  constexpr char kFileOutletDoc[] =
      "Bangs when a 'read' has finished loading a file into the sequence. Appended after Max's "
      "three outlets, which is the rule the whole file-reading family follows, so no saved patch's "
      "cords shift. Max's seq has no such outlet where coll, text and qlist all do, and this is a "
      "deliberate departure rather than an oversight: Max's read is synchronous, so 'read x' "
      "followed by 'start' plays the file in Max, while here the read is a background job whose "
      "result lands a block or more later — without a signal, the arrival of a sequence would be "
      "entirely unobservable and that idiom would have no correct spelling. It fires only on "
      "success: a file that does not exist, is not a sequence, or cannot be opened leaves it "
      "silent. It does not fire for a write, for which Max has no outlet anywhere in this family.";

} // namespace

CONSTRUCT() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Max's left outlet: individual MIDI bytes. Max's middle outlet: the end
  // bang. Max's right outlet, appended at Max's own position with the file half
  // (issue #692): the MIDI-file meta messages of a sequence read from a file.
  ADD_OUT_INT;
  ADD_OUT_BANG;
  ADD_OUT_ANY;
  // And the file outlet after all three, which Max has not got — see the
  // outlet's own documentation for why this one exists.
  ADD_OUT_BANG;

  // The whole of this object's allocation, taken here on the control thread.
  // Nothing on a message path ever resizes it, which is what makes recording
  // and playback from a rendering graph allocation-free.
  events.resize(MAX_EVENTS);
  metas.resize(META_CAPACITY);
  for (Meta& meta : metas)
    meta.raw.reserve(META_BYTES_CAPACITY + 1);
  sendText.reserve(META_TEXT_CAPACITY + 1);

  // Same treatment for the file buffers (issue #692): a `read` or `write` may
  // arrive on the audio thread, so remembering a name and building a MIDI file
  // both have to reuse storage that already exists.
  readPath.reserve(fileScheduler::PATH_CAPACITY);
  writePath.reserve(fileScheduler::PATH_CAPACITY);
  fileScratch.reserve(FILE_TEXT_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Records and plays back raw MIDI bytes — Max's seq, 'a sequencer of raw MIDI bytes'. It is "
      "the third object in the store family with a clock and the one whose contents are not "
      "messages: .qlist (issue #500) plays a written score, .mtr (issue #501) plays a recorded "
      "tape of patcher messages, and this plays a recorded tape of the MIDI wire format, one byte "
      "per event, which is Max's own arrangement and the reason Max's See Also puts mtr beside it. "
      "Bytes rather than parsed messages is deliberate: a raw stream is what a MIDI port hands "
      "over, and a sequencer that had to understand running status, system exclusive or a "
      "fourteen-bit bend before it could store them would be unable to record what it does not "
      "understand. Max's answer is midiparse and midiformat on either side, and this patcher's "
      ".noteon, .noteoff, .controlchange and .midiout already speak the same bytes. Timing comes "
      "from the patcher's deferred-message scheduler (issue #628) through the pair .mtr uses: "
      "messageScheduler::Now measures the gap between two arriving bytes and BlocksForMillis waits "
      "one out again, one clock for both halves because a gap measured on one clock and waited out "
      "on another does not come back the length it went in. That clock stops when the engine does, "
      "so a paused patch holds a recording where it stands; a standalone object has no patcher and "
      "so no clock, records everything at delta 0 and plays straight through. One rule departs "
      "from .qlist deliberately: where a cue list treats the scheduler's one-block deadline floor "
      "as a feature, advancing one entry per block through a run of zero delays, this object runs "
      "them out inside a single dispatch, because the three bytes of a note-on delivered one block "
      "apart are not that note-on. The walk is still bounded by the tape, so a patch cannot lock "
      "the audio thread up with it. 'record' starts a fresh take and 'append' carries on at the "
      "end of the existing one; 'start' and a bang play from the beginning; 'start <n>' is Max's "
      "tempo multiplier, 1024 being the recorded speed, and Max's parenthesis that the speed is "
      "settable 'only at the time you start it' is reproduced. 'start -1' is Max's tick-driven "
      "mode: playback then advances on 'tick' messages, 48 of them per second at the recorded "
      "tempo, which is 24 per quarter note at 120 BPM and therefore a MIDI clock. That is also "
      "this object's answer to issue #502's ask for a domain clock — there is no "
      "patcher-to-domain-clock bridge today (issue #688), but tick is Max's own external timing "
      "source and when the bridge lands, driving tick from it is the whole of the work. The end of "
      "the sequence bangs the second outlet, and Max's odd but useful ordering is kept literally: "
      "'the bang is sent out immediately before the final event of the sequence is played'. "
      "'delay' sets the first event's onset and shifts the rest with it, 'addeventdelay' adds to "
      "that onset, and 'hook' multiplies every event time, which Max allows even while the "
      "sequence is playing. Storage is the family's model for the family's reason: a fixed table "
      "of 4096 events allocated whole at construction behind .value's non-blocking guard whose "
      "loser drops rather than waiting, because a copy-on-write GraphState publish assumes the "
      "writer is the control thread while this object is written by whichever thread its message "
      "arrived on. 4096 rather than the 256 the text stores share, because an event here is a "
      "delta and one byte rather than a string: three of them make one note, so 256 would be 85 "
      "notes and not a sequence. The guard is never held across a send, and .mtr's arrangement "
      "inside it is copied — read, advance, arm, release, then send — so a step needs the guard "
      "once rather than twice. Calculate() does nothing. Nothing is saved with the patcher: the "
      "family rule is to save exactly where Max has a save flag, qlist saves its cue list and mtr "
      "has Max 8's embed, and seq has neither because its contents live in a file and read/write "
      "are its persistence, which is text's answer too. The filename creation argument is a "
      "parameter rather than state and does survive, so a patch brought across from Max still "
      "names the file it meant — and since issue #692 that argument is read when the object joins "
      "a patcher, which is Max's 'read into seq automatically when the patch is loaded'. Reading "
      "and writing files happens nowhere on the message path: a handler runs on whichever thread "
      "the message arrived on, in-patcher delivery dispatches on the audio thread, and THREAD is a "
      "dispatch-semantics tag rather than a thread identity, so no object can find out that it is "
      "off the audio callback, where opening a file would block it. The request is instead a "
      "wait-free claim on a patcher-owned slot, the disk work runs on the background pool "
      "honouring "
      "the host's IO() layer, and the bytes are parsed in the completion the patcher delivers at "
      "the top of a later block — the shared fileScheduler plumbing of issue #683, whose consumer "
      "half here is #692. A read takes both of Max's formats, a standard MIDI file and Max's text "
      "form of absolute millisecond times followed by MIDI bytes, and a write always produces a "
      "standard MIDI file as Max's does: format 0 by default and Max's 'multi-track (format 1)' "
      "one "
      "on a non-zero format argument, which is written as a conductor chunk carrying the meta "
      "events and anything with no channel, then one chunk per MIDI channel the sequence actually "
      "uses. The parser is this object's own rather than YseEngine/midi/'s, and the reason is hard "
      "rather than lazy: MIDI::fileImpl reads a filesystem path into std::vectors it then sorts "
      "twice, where this completion is handed the bytes already and runs on the audio thread, and "
      "its byte primitives are statics in its own translation unit. It is bounded rather than "
      "merely finite — the header is validated before anything is cleared, at most 32 MTrk chunks "
      "are merged and the merge stops as soon as the tape is full. Max's meta outlet lands with "
      "the "
      "file half and is appended at Max's own position, so no saved patch's cords shift; meta "
      "events are stored in the tape beside the bytes, which is what makes hook, delay and "
      "addeventdelay move them with the music and a write put them back where they were. So do "
      "Max's three tempo attributes, which all describe a sequence read from a file: a read "
      "applies "
      "the file's tempo map as it converts ticks to milliseconds, sequencetempo is then Max's "
      "'unmodified tempo of the sequence', tempo 'reflects the current tempo at the current "
      "playback time' literally — a tempo meta passing the playhead sets it — and overridetempo "
      "makes the attribute win. One departure follows from the tape being milliseconds rather than "
      "ticks: overridetempo applies to the next read rather than re-timing the sequence already "
      "loaded, keeping ticks alongside milliseconds being an object whose recorded half and file "
      "half measure time differently and whose hook could only edit one of them. Still not ported: "
      "dump and the editing window it opens a file into, the patcher being headless; and print, "
      "for a neighbouring reason, the patcher's log building a string and taking a lock on a path "
      "that must do neither.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "in", kInletDoc, "0-255");
  OUTLET_DOC(0, "midi", kByteOutletDoc, "0-255");
  OUTLET_DOC(1, "end", kEndOutletDoc, "");
  OUTLET_DOC(2, "meta", kMetaOutletDoc, "");
  OUTLET_DOC(3, "file", kFileOutletDoc, "");
  PARAM_DOC("filename", "",
            "Max's 'name of a file to be read into seq automatically when the patch is loaded', "
            "which since issue #692 is exactly what it does: the object asks for the file when it "
            "joins a patcher and the sequence arrives with the next block. It is also the name a "
            "bare 'read' or 'write' falls back on, there being no file dialog here. Unlike .qlist "
            "and .mtr, which both refused to invent one, this argument is Max's own and has no "
            "second source to contradict — .seq saves nothing with the patcher, so the file is the "
            "only answer to what the object holds.",
            "any filename");
}

// ─── parameters ───────────────────────────────────────────────────────────────

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous name.
  creationArgs.clear();
  fileName.clear();
  // clear() keeps the capacity reserved at construction, so re-seeding these
  // below never allocates.
  readPath.clear();
  writePath.clear();
  count = 0;
  metaCount = 0;
  position = 0;
  recording = false;
  playing = false;
  speed = NORMAL_SPEED;
  tempo = DEFAULT_TEMPO;
  sequenceTempo = DEFAULT_TEMPO;
  overrideTempo = false;
}

PARM_PARSE() {
  fileName.clear();
  readPath.clear();
  writePath.clear();

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty token is not a filename.
    if (token.empty()) continue;
    fileName = token;
    break;
  }

  // The argument is both the file SetParent reads and the name a bare `read` or
  // `write` falls back on, there being no dialog to ask with (issue #692). A
  // name the scheduler could not carry anyway is not remembered.
  if (!fileName.empty() && fileName.size() < fileScheduler::PATH_CAPACITY) {
    readPath = fileName;
    writePath = fileName;
  }

  // A re-parse also drops the tape: the object that comes back is the one the
  // arguments describe, and a recording left over from before would belong to a
  // file the object is no longer named after. `.textfile`'s rule.
  count = 0;
  metaCount = 0;
  position = 0;
  recording = false;
  playing = false;
  speed = NORMAL_SPEED;
  tempo = DEFAULT_TEMPO;
  sequenceTempo = DEFAULT_TEMPO;
  overrideTempo = false;
}

// ─── the clock ────────────────────────────────────────────────────────────────

std::uint64_t gSeq::NowBlock() const {
  const messageScheduler* scheduler = Scheduler();
  // A standalone object has no patcher and so no clock. Everything then records
  // at delta 0 and plays straight through, which is the only honest answer:
  // there is no time for it to be measured against.
  return scheduler == nullptr ? 0 : scheduler->Now();
}

int gSeq::ScaledMillis(int deltaMs) const {
  if (deltaMs <= 0) return 0;
  // Max's multiplier is a speed — "start 2048 plays it back at twice the
  // original speed" — so it divides. `speed` is never zero here: a `start`
  // refuses a non-positive multiplier, and tick mode never reaches this.
  std::int64_t scaled = ((std::int64_t)deltaMs * (std::int64_t)NORMAL_SPEED) / (std::int64_t)speed;
  if (scaled > 2147483647LL) scaled = 2147483647LL;
  return (int)scaled;
}

bool gSeq::ArmStep(int waitMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  CancelStep();
  // One clock per object, Max's shape — `.bondo`'s rule, arrived at for the
  // same reason. The tag is unused: this object has only one kind of pending
  // step.
  pending = scheduler->ScheduleBang(this, 0, waitMs);
  return pending != 0;
}

void gSeq::CancelStep() {
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

// ─── recording ────────────────────────────────────────────────────────────────

void gSeq::Record(int value) {
  // Max stores raw MIDI bytes, and a number outside a byte is not one. Refused
  // rather than masked or clamped: the family's rule everywhere else is that
  // something that does not fit is refused whole, and a byte folded into range
  // would be a different MIDI message rather than a rejected one.
  if (value < 0 || value > 255) return;

  const std::uint64_t now = NowBlock();

  storeGuard guard(busy);
  if (!guard.Held()) return;

  if (!recording) return;
  // Dropped rather than growing the tape, which would allocate on whichever
  // thread this is.
  if (count >= MAX_EVENTS) return;

  // The gap since the previous byte, measured on the block clock playback will
  // wait on. The comparison is defensive: the clock is monotonic, but an object
  // that began recording before the patcher started would otherwise be able to
  // read a gap backwards.
  events[count].deltaMs = messageScheduler::MillisForBlocks(now > lastBlock ? now - lastBlock : 0);
  events[count].byte = (unsigned char)value;
  // Cleared rather than left: this slot may be one a file read used for a meta
  // event, and only `count` says which slots are live (issue #692).
  events[count].meta = false;
  events[count].metaIndex = 0;
  lastBlock = now;
  count++;
}

// ─── playing back ─────────────────────────────────────────────────────────────

gSeq::Step gSeq::TakeStep(bool& last, bool& armed) {
  last = false;
  armed = false;

  storeGuard guard(busy);
  if (!guard.Held()) return Step::DROP;

  // Whatever armed this step has fired; the handle it left behind is stale.
  pending = 0;
  // Stopped, cleared or switched to recording between the arm and the delivery.
  if (!playing) return Step::END;
  if (position >= count) {
    playing = false;
    return Step::END;
  }

  // Copied out rather than referenced: the send happens with the guard
  // released, so a patch that records into this object from downstream must not
  // be able to move the byte still being fanned out.
  PrepareSend(position);
  position++;
  last = position >= count;

  if (last) {
    playing = false;
  } else {
    // Armed here, under the guard and before the send, so a step needs the
    // guard once rather than twice — `.mtr`'s arrangement, for `.mtr`'s reason.
    // A gap that scales to nothing arms nothing at all and the walk carries on
    // in this same dispatch, which is what keeps the bytes of one MIDI message
    // together; the scheduler's one-block floor would otherwise spread them
    // over three blocks.
    const int wait = ScaledMillis(events[position].deltaMs);
    if (wait > 0) armed = ArmStep(wait);
  }
  return Step::OUTPUT;
}

void gSeq::Resume(YSE::THREAD thread) {
  // Bounded by the tape: every step either advances the cursor or ends the
  // walk. It runs more than once for a run of zero-delta events — the bytes of
  // one MIDI message — and when there is no clock to arm on at all, a
  // standalone object or a full pending set, where playing straight through is
  // better than abandoning the sequence half played.
  for (std::size_t steps = 0; steps <= MAX_EVENTS; steps++) {
    bool last = false;
    bool armed = false;
    switch (TakeStep(last, armed)) {
    case Step::OUTPUT:
      SendStep(last, thread);
      if (armed) return;
      break;
    case Step::END:
    case Step::DROP:
      return;
    }
  }
}

gSeq::Step gSeq::TakeTickStep(bool& last) {
  last = false;

  storeGuard guard(busy);
  if (!guard.Held()) return Step::DROP;

  if (!playing || speed != TICK_SPEED) return Step::END;
  if (position >= count) {
    playing = false;
    return Step::END;
  }

  // Max: "in order to play the sequence at its original recorded tempo, seq
  // must receive 48 tick messages per second". Derived from the count rather
  // than accumulated per tick, so 48 ticks is exactly one second however many
  // have gone by.
  const std::int64_t elapsedMs = (ticks * 1000) / TICKS_PER_SECOND;
  // Not due yet. END rather than a state of its own: both answers stop the
  // walk, and the next tick asks again.
  if (dueMs > elapsedMs) return Step::END;

  PrepareSend(position);
  position++;
  last = position >= count;

  if (last) {
    playing = false;
  } else {
    dueMs += events[position].deltaMs;
  }
  return Step::OUTPUT;
}

void gSeq::Tick(YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    // A tick outside Max's `start -1` mode means nothing — the millisecond
    // clock is already running the sequence.
    if (!playing || speed != TICK_SPEED) return;
    ticks++;
  }

  // Every event the tick just made due, which is more than one whenever the
  // sequence is denser than the tick grid. Bounded by the tape.
  for (std::size_t steps = 0; steps <= MAX_EVENTS; steps++) {
    bool last = false;
    switch (TakeTickStep(last)) {
    case Step::OUTPUT:
      SendStep(last, thread);
      break;
    case Step::END:
    case Step::DROP:
      return;
    }
  }
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gSeq::PrepareSend(std::size_t index) {
  const Event& event = events[index];
  sendMeta = event.meta;
  if (!sendMeta) {
    sendByte = event.byte;
    return;
  }

  // Formatted here rather than in SendStep, because the payload it reads is
  // guarded state and the send happens with the guard released — `.mtr`'s
  // arrangement for the same reason.
  FormatMeta(event.metaIndex);

  // Max: "if the seq has read a MIDI file with tempo information, the tempo
  // attribute will reflect the current tempo at the current playback time",
  // which is this: the tempo the playhead has just passed. Unless the patch has
  // taken the tempo over, which is exactly what `overridetempo` is — Max's "the
  // value of the tempo attribute will override any tempo requested by the
  // sequence".
  if (overrideTempo || event.metaIndex >= metaCount) return;
  const Meta& meta = metas[event.metaIndex];
  if (meta.type == META_TEMPO && meta.raw.size() == 3)
    tempo = TempoFromMicros(MetaMicros(meta.raw));
}

void gSeq::SendStep(bool last, YSE::THREAD thread) {
  // Max: "the bang is sent out immediately before the final event of the
  // sequence is played." Odd, documented, and useful — a patch learns that the
  // byte about to arrive is the last one rather than finding out afterwards.
  // A meta event is an event like any other here, so it gets the same warning.
  if (last && outputs.size() > 1) outputs[1].SendBang(thread);

  if (sendMeta) {
    // Max's rightmost outlet, "prepended with the word meta" (issue #692).
    if (outputs.size() > 2) outputs[2].SendList(sendText, thread);
    return;
  }
  if (!outputs.empty()) outputs[0].SendInt((int)sendByte, thread);
}

void gSeq::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  // The wait has elapsed. The scheduler wraps this in a fresh messageEventScope,
  // so everything the resumed step goes on to cause is one logical event (#628).
  //
  // The delivered tag is passed straight through. It is T_GUI — "let the
  // block's own traversal render it" — which is the right reading for an outlet
  // send, and since #690 the only reading there is: `patcherImplementation::
  // PassData` decides lock-free-vs-queued from `CallingThread`, not from the
  // tag, so a T_GUI delivery on the audio callback no longer takes `mtx`.
  Resume(thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

void gSeq::StartRecording(bool keepContents) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "a stop message need not be received when switching directly from
  // playing to recording, or vice-versa."
  CancelStep();
  playing = false;
  recording = true;

  // Max's `record` starts a fresh take; `append` "starts recording at the end
  // of the stored sequence, without erasing the existing sequence", which is
  // the whole difference between the two.
  if (!keepContents) count = 0;
  position = 0;

  // The first gap of a take is measured from this message, which is what gives
  // Max's `delay` something to overwrite. An `append` measures the gap to the
  // byte it stored last from here too: the time the object spent stopped is not
  // part of the recording.
  lastBlock = NowBlock();
}

void gSeq::StartPlaying(int requested, YSE::THREAD thread) {
  bool armed = false;
  bool started = false;

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    CancelStep();
    recording = false;
    playing = false;

    // Max's `bang`: "plays the sequence stored in seq", from the beginning.
    // Nothing recorded is nothing to play, and there is no final event for the
    // end bang to precede either.
    if (count == 0) return;

    position = 0;
    playing = true;
    // Max: the speed is settable "only at the time you start it", so this is
    // the one place it is written.
    speed = requested;

    if (speed == TICK_SPEED) {
      // Max: "starts the sequencer, but rather than follow Max's millisecond
      // clock, seq waits for a tick message to advance its clock." So nothing
      // happens here at all — not even a zero-delta first event, which falls
      // due on the first tick.
      ticks = 0;
      dueMs = events[0].deltaMs;
      return;
    }

    const int wait = ScaledMillis(events[0].deltaMs);
    if (wait > 0) armed = ArmStep(wait);
    started = true;
  }

  // Either the first event is due immediately, or there is no clock to wait on
  // — a standalone object, or a full pending set. Playing on is better than not
  // playing at all, and the walk is bounded by the tape.
  if (started && !armed) Resume(thread);
}

void gSeq::StopAll() {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "stops the sequencer if it is recording or playing." The cursor stays
  // where it is; only a `start` rewinds.
  CancelStep();
  playing = false;
  recording = false;
}

void gSeq::Clear() {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "clears the sequence currently stored in the seq object." A cleared
  // sequence has nothing left to play, so a walk in progress ends with it.
  CancelStep();
  playing = false;
  recording = false;
  count = 0;
  metaCount = 0;
  position = 0;
}

bool gSeq::HandleCommand(const char* word, std::size_t length, const std::string& message,
                         std::size_t argOffset, YSE::THREAD thread) {
  if (TokenIs(word, length, "record", 6)) {
    StartRecording(false);
    return true;
  }

  if (TokenIs(word, length, "append", 6)) {
    StartRecording(true);
    return true;
  }

  if (TokenIs(word, length, "start", 5)) {
    // Max: "the word start by itself has the same effect as bang", and with a
    // number, "start 1024 indicates normal tempo".
    int requested = NORMAL_SPEED;
    int argument = 0;
    if (ReadIntArg(message, argOffset, argument)) {
      if (argument == TICK_SPEED) {
        requested = TICK_SPEED;
      } else if (argument > 0) {
        requested = argument;
      }
      // Anything else — 0, or a negative that is not -1 — cannot scale a
      // duration into something a clock can wait for, so it is refused and the
      // recorded tempo used instead.
    }
    StartPlaying(requested, thread);
    return true;
  }

  if (TokenIs(word, length, "stop", 4)) {
    StopAll();
    return true;
  }

  if (TokenIs(word, length, "tick", 4)) {
    Tick(thread);
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    Clear();
    return true;
  }

  if (TokenIs(word, length, "delay", 5)) {
    // Max: "sets the onset time, in milliseconds, of the first event in the
    // recorded sequence. All events in the sequence are shifted so that the
    // first event occurs at the specified onset time." With deltas stored
    // rather than absolute times, that shift *is* writing the first delta.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (count > 0) events[0].deltaMs = value > 0 ? value : 0;
    return true;
  }

  if (TokenIs(word, length, "addeventdelay", 13)) {
    // Max: "adds to the delay onset time, in milliseconds, of the first event
    // in the recorded sequence" — the same edit as `delay`, made relative.
    float value = 0.f;
    if (!ReadFloatArgAt(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (count > 0) {
      const int added = SatAdd(events[0].deltaMs, RoundToInt(value));
      events[0].deltaMs = added > 0 ? added : 0;
    }
    return true;
  }

  if (TokenIs(word, length, "hook", 4)) {
    // Max: "multiplies all the event times in the stored sequence by that
    // number. For example, if the number is 2.0, all event times will be
    // doubled, and the sequence will play back twice as slowly.
    // Multiplications can even be performed while the sequence is playing" —
    // so this is an edit rather than a setting, and a hook mid-playback reaches
    // every event from the next step onward. The step already armed keeps the
    // wait it was armed with, there being no way to shorten a deadline that has
    // already been handed to the scheduler.
    float value = 0.f;
    if (!ReadFloatArgAt(message, argOffset, value)) return true;
    // A multiplier of zero or less cannot scale a duration into anything a
    // clock can wait for, so it is refused and the tape left alone.
    if (!(value > 0.f)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    for (std::size_t i = 0; i < count; i++)
      events[i].deltaMs = SatScale(events[i].deltaMs, value);
    return true;
  }

  // The file half (issue #692). Both are a claim on a patcher-owned slot and
  // nothing more: whichever thread is dispatching, no file is opened here. The
  // remainder is the path, so a name with spaces in it still works, and a bare
  // form reuses the last name given — Max's opens a file dialog, which a
  // headless patcher has no equivalent of.
  const bool isRead = TokenIs(word, length, "read", 4);
  if (isRead || TokenIs(word, length, "write", 5)) {
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);

    // Max's `write <filename> <format>`: "a non-zero int argument creates a
    // multi-track (format 1) MIDI file". Taken off the end rather than as the
    // second token, so that the whole-remainder-is-the-path rule the family
    // shares survives — and only when there is a name in front of it, so a file
    // actually called `1` can still be written. The cost is that a name whose
    // last word is a bare number has to be written without it, which is
    // documented rather than guessed at.
    int format = 0;
    if (!isRead && end > begin) {
      std::size_t tail = end;
      while (tail > begin && !IsSelectorSeparator(message[tail - 1]))
        tail--;
      std::size_t nameEnd = tail;
      while (nameEnd > begin && IsSelectorSeparator(message[nameEnd - 1]))
        nameEnd--;

      int value = 0;
      std::size_t cursor = tail;
      if (nameEnd > begin && ReadIntInPlace(message.c_str(), cursor, end, value)) {
        format = value;
        end = nameEnd;
      }
    }

    RequestFile(isRead ? FILE_OP::READ : FILE_OP::WRITE, message.c_str() + begin, end - begin,
                format);
    return true;
  }

  if (TokenIs(word, length, "tempo", 5)) {
    // Max's tempo attribute, in beats per minute. What a read converts a file's
    // ticks with when `overridetempo` is on, what a write puts in its header,
    // and what a tempo meta passing the playhead overwrites when it is off.
    float value = 0.f;
    if (!ReadFloatArgAt(message, argOffset, value)) return true;
    // A tempo of zero or less cannot convert a duration into anything, so it is
    // refused and the previous one kept.
    if (!(value > 0.f)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    tempo = value;
    return true;
  }

  if (TokenIs(word, length, "overridetempo", 13)) {
    // Max: "if enabled (default = 0 (disabled)), the value of the tempo
    // attribute will override any tempo requested by the sequence." It decides
    // what the *next* read converts its ticks with — see the class
    // documentation on why the read rather than the sequence already loaded.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    overrideTempo = value != 0;
    return true;
  }

  // Max's `sequencetempo` is get-only — "this is a read-only value for
  // convenience purposes" — so it is consumed and changes nothing, which is
  // what Max does with a write to a read-only attribute.
  if (TokenIs(word, length, "sequencetempo", 13)) return true;

  // Consumed and inert. `dump` "is opened as text in a new Untitled text
  // window", which is the editing window this patcher is headless for, and
  // `print` would build a log string and take the log's lock on a path that may
  // be the audio callback. See the class documentation.
  if (TokenIs(word, length, "dump", 4)) return true;
  if (TokenIs(word, length, "print", 5)) return true;

  return false;
}

// ─── files (issue #692) ───────────────────────────────────────────────────────

bool gSeq::RequestFile(FILE_OP op, const char* name, std::size_t length, int format) {
  fileScheduler* io = FileIO();
  // A standalone .seq has no patcher and so no plumbing. Silent: this may be the
  // audio thread, where a log line would allocate.
  if (io == nullptr) return false;

  std::string& remembered = op == FILE_OP::READ ? readPath : writePath;
  if (name != nullptr && length > 0) {
    if (length >= fileScheduler::PATH_CAPACITY) return false;
    // assign() into a string reserved at construction reuses its storage.
    remembered.assign(name, length);
  }
  // Nothing named yet, and no dialog to ask with.
  if (remembered.empty()) return false;

  if (op == FILE_OP::READ) {
    return io->RequestRead(this, FILE_TAG_READ, remembered.c_str(), remembered.size());
  }

  // The bytes are built here rather than on the pool thread, because the pool
  // must never touch this object: by the time the job runs, a live edit may have
  // deleted it.
  if (!Serialize(format)) return false;
  return io->RequestWrite(this, FILE_TAG_WRITE, remembered.c_str(), remembered.size(),
                          fileScratch.data(), fileScratch.size());
}

// ─── writing a standard MIDI file ─────────────────────────────────────────────

bool gSeq::NextMessage(Walk& walk, Message& out) const {
  while (walk.at < count) {
    const std::size_t index = walk.at;
    walk.absMs = SatAdd(walk.absMs, events[index].deltaMs);
    const int absMs = walk.absMs;
    walk.at = index + 1;

    if (events[index].meta) {
      // A meta cancels running status, as everything that is not a channel
      // message does.
      walk.running = 0;
      out.begin = index;
      out.dataBegin = index;
      out.end = index + 1;
      out.absMs = absMs;
      out.channel = -1;
      out.meta = true;
      out.metaIndex = events[index].metaIndex;
      out.status = 0;
      return true;
    }

    const unsigned char byte = events[index].byte;

    // Max: seq "supports channel and system exclusive messages but not
    // real-time messages", and a file has nowhere to put one anyway.
    if (byte >= 0xF8) continue;

    if (byte == SYSEX_BEGIN) {
      // Everything up to and including the terminator, or up to whatever cut it
      // short. The clock still passes over the bytes it swallows.
      std::size_t stop = walk.at;
      while (stop < count && !events[stop].meta) {
        const unsigned char next = events[stop].byte;
        if (next == SYSEX_ESCAPE) {
          stop++;
          break;
        }
        if (next >= 0x80) break;
        stop++;
      }
      for (std::size_t i = walk.at; i < stop; i++)
        walk.absMs = SatAdd(walk.absMs, events[i].deltaMs);
      walk.at = stop;
      walk.running = 0;

      out.begin = index;
      out.dataBegin = index + 1;
      out.end = stop;
      out.absMs = absMs;
      out.channel = -1;
      out.meta = false;
      out.metaIndex = 0;
      out.status = SYSEX_BEGIN;
      return true;
    }

    unsigned char status = 0;
    std::size_t dataBegin = 0;
    if (byte >= 0x80) {
      // System common (F1-F7) has no length a file could carry and is not part
      // of what Max says seq stores, so it is skipped rather than guessed at.
      if (byte >= 0xF1) {
        walk.running = 0;
        continue;
      }
      status = byte;
      walk.running = byte;
      dataBegin = index + 1;
    } else {
      // A data byte under running status, which is what a recorded stream is
      // full of. The status byte is implied and has to be written out rather
      // than copied.
      if (walk.running == 0) continue;
      status = walk.running;
      dataBegin = index;
    }

    const std::size_t wanted = ChannelDataBytes(status);
    std::size_t stop = dataBegin;
    while (stop < dataBegin + wanted && stop < count && !events[stop].meta &&
           events[stop].byte < 0x80)
      stop++;
    for (std::size_t i = walk.at; i < stop; i++)
      walk.absMs = SatAdd(walk.absMs, events[i].deltaMs);
    walk.at = stop > walk.at ? stop : walk.at;

    // Cut short by the end of the tape or by the next status byte: half a
    // message is a different message, so it is dropped rather than written.
    if (stop != dataBegin + wanted) continue;

    out.begin = index;
    out.dataBegin = dataBegin;
    out.end = stop;
    out.absMs = absMs;
    out.channel = (int)(status & 0x0F);
    out.meta = false;
    out.metaIndex = 0;
    out.status = status;
    return true;
  }
  return false;
}

bool gSeq::Serialize(int format) {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists.
  fileScratch.clear();

  // Format 1 is Max's "multi-track": a conductor chunk carrying the meta events
  // and anything with no channel of its own, then one chunk per MIDI channel the
  // sequence actually uses. Counting them is a pass of its own, because the
  // header has to say how many there are before the first one is written.
  bool used[16] = {};
  std::uint16_t chunks = 1;
  if (format != 0) {
    Walk walk;
    Message message;
    while (NextMessage(walk, message)) {
      if (message.channel < 0 || used[message.channel]) continue;
      used[message.channel] = true;
      chunks++;
    }
  }

  // "MThd" <length 6> <format> <chunks> <division>.
  fileScratch.append("MThd", 4);
  AppendU32(fileScratch, 6);
  AppendU16(fileScratch, format == 0 ? 0 : 1);
  AppendU16(fileScratch, chunks);
  AppendU16(fileScratch, (std::uint16_t)WRITE_DIVISION);

  if (format == 0) return WriteChunk(TRACK_EVERYTHING);

  if (!WriteChunk(TRACK_CONDUCTOR)) return false;
  for (int channel = 0; channel < 16; channel++) {
    if (!used[channel]) continue;
    if (!WriteChunk(channel)) return false;
  }
  return true;
}

bool gSeq::WriteChunk(int filter) {
  if (fileScratch.size() + 8 > FILE_TEXT_CAPACITY) return false;
  fileScratch.append("MTrk", 4);
  const std::size_t lengthAt = fileScratch.size();
  AppendU32(fileScratch, 0);
  const std::size_t bodyAt = fileScratch.size();

  // The tick timeline, which every chunk shares: each one follows the whole
  // tape's tempo map whether or not it is the chunk that writes the tempo metas,
  // because a channel chunk of a file with a tempo change would otherwise drift
  // away from the conductor.
  //
  // It starts at the tempo attribute, and a tempo meta saying so is written at
  // tick 0 unless the tape already opens with one — which makes the round trip
  // exact and, after the first one, stable: what comes back already opens with
  // the meta, so writing it again adds nothing.
  double bpm = tempo > 0.f ? (double)tempo : (double)DEFAULT_TEMPO;
  bool declareTempo = true;
  if (count > 0 && events[0].meta && events[0].deltaMs == 0 && events[0].metaIndex < metaCount) {
    const Meta& first = metas[events[0].metaIndex];
    if (first.type == META_TEMPO && first.raw.size() == 3) {
      declareTempo = false;
      bpm = (double)TempoFromMicros(MetaMicros(first.raw));
    }
  }

  if (declareTempo && (filter == TRACK_EVERYTHING || filter == TRACK_CONDUCTOR)) {
    if (fileScratch.size() + 7 > FILE_TEXT_CAPACITY) return false;
    AppendVarLen(fileScratch, 0);
    AppendByte(fileScratch, META_PREFIX);
    AppendByte(fileScratch, META_TEMPO);
    AppendVarLen(fileScratch, 3);
    const std::uint32_t micros = MicrosFromTempo(bpm);
    AppendByte(fileScratch, (unsigned char)((micros >> 16) & 0xFF));
    AppendByte(fileScratch, (unsigned char)((micros >> 8) & 0xFF));
    AppendByte(fileScratch, (unsigned char)(micros & 0xFF));
  }

  Walk walk;
  Message message;
  double absTicks = 0.0;
  std::int64_t lastTicks = 0;
  int lastMs = 0;

  while (NextMessage(walk, message)) {
    // The clock first, at the tempo in force over the gap that led up to this
    // message — a tempo meta takes effect from its own time onward, so it is
    // applied *after* the gap before it.
    if (message.absMs > lastMs) {
      absTicks += ((double)(message.absMs - lastMs) * (double)WRITE_DIVISION * bpm) / 60000.0;
      lastMs = message.absMs;
    }

    bool accept = false;
    if (message.meta) {
      // End of track is structural: every chunk writes its own, and one carried
      // on the tape would land in the middle of a chunk, where the format says
      // nothing may follow it.
      const bool structural =
          message.metaIndex < metaCount && metas[message.metaIndex].type == META_END_OF_TRACK;
      accept = !structural && (filter == TRACK_EVERYTHING || filter == TRACK_CONDUCTOR);
    } else if (message.channel < 0) {
      accept = filter == TRACK_EVERYTHING || filter == TRACK_CONDUCTOR;
    } else {
      accept = filter == TRACK_EVERYTHING || filter == message.channel;
    }

    if (accept) {
      // Rounded against the running total rather than per gap, so a long
      // sequence does not drift by half a tick per event.
      const std::int64_t ticks = (std::int64_t)std::llround(absTicks);
      std::int64_t delta = ticks - lastTicks;
      if (delta < 0) delta = 0;
      lastTicks = ticks;

      if (message.meta) {
        const Meta& meta = metas[message.metaIndex];
        if (fileScratch.size() + 10 + meta.raw.size() > FILE_TEXT_CAPACITY) return false;
        AppendVarLen(fileScratch, (std::uint32_t)delta);
        AppendByte(fileScratch, META_PREFIX);
        AppendByte(fileScratch, meta.type);
        AppendVarLen(fileScratch, (std::uint32_t)meta.raw.size());
        fileScratch.append(meta.raw);
      } else if (message.status == SYSEX_BEGIN) {
        // "F0 <length> <data>", the leading F0 being the status byte itself and
        // the terminator part of the data, which is the format's own shape.
        const std::size_t bytes = message.end - message.dataBegin;
        if (fileScratch.size() + 9 + bytes > FILE_TEXT_CAPACITY) return false;
        AppendVarLen(fileScratch, (std::uint32_t)delta);
        AppendByte(fileScratch, SYSEX_BEGIN);
        AppendVarLen(fileScratch, (std::uint32_t)bytes);
        for (std::size_t i = message.dataBegin; i < message.end; i++)
          AppendByte(fileScratch, events[i].byte);
      } else {
        const std::size_t bytes = message.end - message.dataBegin;
        if (fileScratch.size() + 5 + bytes > FILE_TEXT_CAPACITY) return false;
        AppendVarLen(fileScratch, (std::uint32_t)delta);
        // Written out rather than copied: under running status the tape has no
        // status byte to copy. No running status is written either — it is legal
        // and it costs one byte a message that a sequencer will not notice.
        AppendByte(fileScratch, message.status);
        for (std::size_t i = message.dataBegin; i < message.end; i++)
          AppendByte(fileScratch, events[i].byte);
      }
    }

    // And only now the tempo change, for the reason above.
    if (message.meta && message.metaIndex < metaCount) {
      const Meta& meta = metas[message.metaIndex];
      if (meta.type == META_TEMPO && meta.raw.size() == 3)
        bpm = (double)TempoFromMicros(MetaMicros(meta.raw));
    }
  }

  // "FF 2F 00", which every chunk ends with and nothing may follow.
  if (fileScratch.size() + 4 > FILE_TEXT_CAPACITY) return false;
  AppendVarLen(fileScratch, 0);
  AppendByte(fileScratch, META_PREFIX);
  AppendByte(fileScratch, META_END_OF_TRACK);
  AppendByte(fileScratch, 0);

  // The chunk length, which could only be known once the chunk was written.
  const std::uint32_t body = (std::uint32_t)(fileScratch.size() - bodyAt);
  fileScratch[lengthAt] = (char)(unsigned char)((body >> 24) & 0xFF);
  fileScratch[lengthAt + 1] = (char)(unsigned char)((body >> 16) & 0xFF);
  fileScratch[lengthAt + 2] = (char)(unsigned char)((body >> 8) & 0xFF);
  fileScratch[lengthAt + 3] = (char)(unsigned char)(body & 0xFF);
  return true;
}

// ─── reading a sequence ───────────────────────────────────────────────────────

void gSeq::ResetForLoad() {
  // A read replaces the sequence, so whatever the transport was doing to the old
  // one ends with it: a sequence left playing would already have a step armed at
  // a delta belonging to a tape that no longer exists. `.mtr`'s rule, and where
  // both part company with `.qlist`, whose walk carries on into the list it just
  // loaded.
  CancelStep();
  playing = false;
  recording = false;
  count = 0;
  metaCount = 0;
  position = 0;
  ticks = 0;
  dueMs = 0;
}

bool gSeq::PushByte(int deltaMs, unsigned char byte) {
  if (count >= MAX_EVENTS) return false;
  events[count].deltaMs = deltaMs > 0 ? deltaMs : 0;
  events[count].byte = byte;
  events[count].meta = false;
  events[count].metaIndex = 0;
  count++;
  return true;
}

bool gSeq::PushMeta(int deltaMs, unsigned char type, const unsigned char* data,
                    std::size_t length) {
  if (count >= MAX_EVENTS) return false;
  // Dropped rather than truncated or refused, and the sequence loads around it:
  // a meta is information *about* the music rather than part of it, so losing
  // one is not losing the take. The caller keeps the gap, which then belongs to
  // whatever event comes next.
  if (metaCount >= META_CAPACITY || length > META_BYTES_CAPACITY) return true;

  metas[metaCount].type = type;
  // assign() into a payload reserved at construction, so this allocates nothing.
  metas[metaCount].raw.assign((const char*)data, length);

  events[count].deltaMs = deltaMs > 0 ? deltaMs : 0;
  events[count].byte = 0;
  events[count].meta = true;
  events[count].metaIndex = (std::uint16_t)metaCount;
  metaCount++;
  count++;
  return true;
}

void gSeq::FormatMeta(std::size_t index) {
  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists.
  sendText.clear();
  // Max: "prepended with the word meta, followed by the name of the meta message
  // and the data".
  sendText.append("meta ", 5);
  if (index >= metaCount) return;

  const Meta& meta = metas[index];
  char digits[FORMAT_INT_WIDTH];
  const char* name = MetaName(meta.type);
  if (name != nullptr) {
    sendText.append(name);
  } else {
    // A type Max has no name for. The number stands in for the name rather than
    // the message being swallowed — a patch can still tell two of them apart.
    sendText.append(digits, WriteInt((int)meta.type, digits));
  }

  if (meta.raw.empty()) return;

  if (MetaIsText(meta.type)) {
    sendText.push_back(' ');
    // A text meta's payload is arbitrary bytes, and a control character in one
    // would break the message it is being spelled into. Replaced rather than
    // dropped, so the text stays the length it was.
    for (char c : meta.raw) {
      const unsigned char byte = (unsigned char)c;
      sendText.push_back(byte < 32 || byte == 127 ? ' ' : c);
    }
    return;
  }

  if (meta.type == META_TEMPO && meta.raw.size() == 3) {
    // Spelled as Max's beats per minute, which is what the tempo attribute is
    // in, rather than as the file's microseconds per quarter note.
    char value[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(TempoFromMicros(MetaMicros(meta.raw))),
                                        value, kExprValueTextMax);
    if (written > 0) {
      sendText.push_back(' ');
      sendText.append(value, (std::size_t)written);
    }
    return;
  }

  for (char c : meta.raw) {
    sendText.push_back(' ');
    sendText.append(digits, WriteInt((int)(unsigned char)c, digits));
  }
}

void gSeq::LoadText(const char* text, std::size_t length) {
  ResetForLoad();

  // Max's text form: "each line consists of a start time in milliseconds (the
  // time elapsed since the beginning of the sequence) followed by the
  // (space-separated) bytes of a MIDI message recorded at that start time."
  // Absolute times there, deltas here, which is the whole of the conversion.
  int lastAbsMs = 0;
  std::size_t at = 0;
  while (at < length) {
    std::size_t stop = at;
    while (stop < length && text[stop] != '\n')
      stop++;

    std::size_t begin = at;
    std::size_t end = stop;
    // A `\r` before the break is whitespace the trim removes, so a CRLF file
    // loads as the lines it looks like.
    Trim(text, begin, end);
    at = stop + 1;
    if (end <= begin) continue;

    std::size_t cursor = begin;
    int absMs = 0;
    // A line that does not start with a time is not an event.
    if (!ReadIntInPlace(text, cursor, end, absMs)) continue;
    if (absMs < 0) absMs = 0;
    int delta = absMs - lastAbsMs;
    if (delta < 0) delta = 0;

    int value = 0;
    bool first = true;
    while (ReadIntInPlace(text, cursor, end, value)) {
      // Not a MIDI byte, and folding it into range would be a different message
      // rather than a refused one — `Record`'s rule.
      if (value < 0 || value > 255) continue;
      // The bytes of one line share one start time, so only the first carries
      // the gap. That is also exactly what playback runs out together.
      if (!PushByte(first ? delta : 0, (unsigned char)value)) return;
      first = false;
    }
    // A line whose bytes were all refused never happened, so the next line's gap
    // is still measured from the last one that did.
    if (!first) lastAbsMs = absMs;
  }
}

bool gSeq::LoadMidiFile(const unsigned char* data, std::size_t length) {
  // Validated whole *before* anything is cleared, which is what lets a file that
  // turns out not to be a sequence leave the one already loaded alone.
  if (length < 14) return false;
  const std::uint32_t headerLength = ReadU32(data + 4);
  if (headerLength < 6 || headerLength > length - 8) return false;
  const int division = (int)(std::int16_t)ReadU16(data + 12);
  if (division == 0) return false;

  // A negative division is SMPTE: frames per second in the high byte (negated),
  // ticks per frame in the low one. Absolute time, so a tempo meta in such a
  // file changes nothing about the timing.
  const bool smpte = division < 0;
  double msPerTick = 0.0;
  if (smpte) {
    const int fps = -(division >> 8);
    const int perFrame = division & 0xFF;
    if (fps <= 0 || perFrame <= 0) return false;
    msPerTick = 1000.0 / ((double)fps * (double)perFrame);
  }

  // The chunk table. Anything that is not an MTrk is skipped, which the format
  // asks for: a reader should expect alien chunks and behave as if they were not
  // there.
  struct Chunk {
    std::size_t at;
    std::size_t end;
  };
  Chunk chunks[MAX_FILE_TRACKS];
  std::size_t chunkCount = 0;

  std::size_t cursor = 8 + (std::size_t)headerLength;
  while (cursor + 8 <= length) {
    const std::uint32_t chunkLength = ReadU32(data + cursor + 4);
    const std::size_t body = cursor + 8;
    // A truncated tail is taken as far as it goes rather than read past.
    std::size_t bodyEnd = length;
    if ((std::size_t)chunkLength <= length - body) bodyEnd = body + (std::size_t)chunkLength;

    if (data[cursor] == 'M' && data[cursor + 1] == 'T' && data[cursor + 2] == 'r' &&
        data[cursor + 3] == 'k') {
      // Refused whole rather than half read: a merge missing a track would
      // silently be a different sequence.
      if (chunkCount >= MAX_FILE_TRACKS) return false;
      chunks[chunkCount].at = body;
      chunks[chunkCount].end = bodyEnd;
      chunkCount++;
    }
    if (bodyEnd <= cursor) break;
    cursor = bodyEnd;
  }
  if (chunkCount == 0) return false;

  ResetForLoad();

  // The tempo the ticks are converted with. Max's "the value of the tempo
  // attribute will override any tempo requested by the sequence" is made once,
  // here, because the tape stores milliseconds — see the class documentation.
  double usPerQuarter =
      (double)MicrosFromTempo(overrideTempo ? (double)tempo : (double)DEFAULT_TEMPO);
  if (!smpte) msPerTick = usPerQuarter / (double)division / 1000.0;

  // One cursor per chunk. `nextTick` is the absolute tick of the event the
  // cursor is standing on, which is what the merge compares.
  struct Cursor {
    std::size_t at;
    std::size_t end;
    std::uint64_t nextTick;
    unsigned char running;
    bool done;
  };
  Cursor tracks[MAX_FILE_TRACKS];
  for (std::size_t i = 0; i < chunkCount; i++) {
    tracks[i].at = chunks[i].at;
    tracks[i].end = chunks[i].end;
    tracks[i].nextTick = 0;
    tracks[i].running = 0;
    tracks[i].done = false;
    std::uint32_t delta = 0;
    if (ReadVarLen(data, tracks[i].at, tracks[i].end, delta))
      tracks[i].nextTick = delta;
    else
      tracks[i].done = true;
  }

  std::uint64_t lastTick = 0;
  double absMs = 0.0;
  int emittedMs = 0;
  bool haveSequenceTempo = false;
  float firstTempo = DEFAULT_TEMPO;

  // Bounded rather than merely finite. Every pass either puts an event on the
  // tape, drops one the tape or the meta table has no room for, or finishes a
  // chunk — so the walk cannot outlast the three of them together however large
  // the file is or however much it lies about itself. That is the property that
  // makes this affordable in a completion the audio thread delivers.
  const std::size_t limit = MAX_EVENTS + META_CAPACITY + MAX_FILE_TRACKS;
  for (std::size_t step = 0; step < limit; step++) {
    // The merge: whichever chunk's next event is earliest, ties going to the
    // lower-numbered chunk so the order is the file's own. This is what makes a
    // format 1 file play as one sequence, and it is the identity for format 0.
    std::size_t pick = MAX_FILE_TRACKS;
    for (std::size_t i = 0; i < chunkCount; i++) {
      if (tracks[i].done) continue;
      if (pick == MAX_FILE_TRACKS || tracks[i].nextTick < tracks[pick].nextTick) pick = i;
    }
    if (pick == MAX_FILE_TRACKS) break;

    Cursor& track = tracks[pick];

    // The clock, at the tempo in force over the gap that led up to this event.
    if (track.nextTick > lastTick) {
      absMs += (double)(track.nextTick - lastTick) * msPerTick;
      lastTick = track.nextTick;
    }
    // Rounded against the running total rather than per gap, so a long sequence
    // does not drift by half a millisecond per event.
    double clamped = absMs;
    if (!(clamped > 0.0)) clamped = 0.0;
    if (clamped > 2147483000.0) clamped = 2147483000.0;
    const int nowMs = (int)std::llround(clamped);
    int delta = nowMs - emittedMs;
    if (delta < 0) delta = 0;

    std::size_t at = track.at;
    unsigned char status = 0;
    if (at < track.end && (data[at] & 0x80) != 0) {
      status = data[at];
      at++;
    } else {
      status = track.running;
      // A data byte with no running status behind it: this chunk cannot be
      // walked any further without guessing.
      if (status == 0) {
        track.done = true;
        continue;
      }
    }

    const std::size_t before = count;
    bool full = false;

    if (status == META_PREFIX) {
      // "FF <type> <length> <data>".
      if (at >= track.end) {
        track.done = true;
        continue;
      }
      const unsigned char type = data[at];
      at++;
      std::uint32_t payload = 0;
      if (!ReadVarLen(data, at, track.end, payload) || (std::size_t)payload > track.end - at) {
        track.done = true;
        continue;
      }
      track.running = 0;

      if (!PushMeta(delta, type, data + at, (std::size_t)payload)) full = true;

      // The tempo change takes effect from its own time onward, so it is applied
      // after the gap before it and after the meta is stored — a tempo meta is
      // an event the outlet gets like any other.
      if (!full && type == META_TEMPO && payload == 3 && !smpte) {
        const std::uint32_t micros = ((std::uint32_t)data[at] << 16) |
                                     ((std::uint32_t)data[at + 1] << 8) |
                                     (std::uint32_t)data[at + 2];
        if (!haveSequenceTempo) {
          firstTempo = TempoFromMicros(micros);
          haveSequenceTempo = true;
        }
        if (!overrideTempo && micros != 0) {
          usPerQuarter = (double)micros;
          msPerTick = usPerQuarter / (double)division / 1000.0;
        }
      }
      at += (std::size_t)payload;
    } else if (status == SYSEX_BEGIN || status == SYSEX_ESCAPE) {
      // Max: seq "supports channel and system exclusive messages". A file holds
      // the length rather than relying on the terminator, and an F0 chunk's
      // leading F0 is the status byte itself.
      std::uint32_t payload = 0;
      if (!ReadVarLen(data, at, track.end, payload) || (std::size_t)payload > track.end - at) {
        track.done = true;
        continue;
      }
      track.running = 0;

      const std::size_t needed = (std::size_t)payload + (status == SYSEX_BEGIN ? 1 : 0);
      if (count + needed > MAX_EVENTS) {
        full = true;
      } else {
        bool first = true;
        if (status == SYSEX_BEGIN) {
          PushByte(delta, SYSEX_BEGIN);
          first = false;
        }
        for (std::uint32_t i = 0; i < payload; i++) {
          PushByte(first ? delta : 0, data[at + i]);
          first = false;
        }
      }
      at += (std::size_t)payload;
    } else if (status < 0xF0) {
      const std::size_t wanted = ChannelDataBytes(status);
      if (at + wanted > track.end) {
        track.done = true;
        continue;
      }
      // Checked before anything is stored, so the tape never ends with half a
      // MIDI message on it.
      if (count + 1 + wanted > MAX_EVENTS) {
        full = true;
      } else {
        PushByte(delta, status);
        for (std::size_t i = 0; i < wanted; i++)
          PushByte(0, data[at + i]);
        track.running = status;
      }
      at += wanted;
    } else {
      // F1-F7 and F8-FE carry no length in a file and cannot appear in one;
      // whatever this chunk is, it cannot be walked any further.
      track.done = true;
      continue;
    }

    if (full) break;
    // Only what actually reached the tape moves the gap the next event is
    // measured from — a meta the table had no room for leaves its gap to be
    // carried by whatever comes after it.
    if (count > before) emittedMs = nowMs;

    track.at = at;
    std::uint32_t next = 0;
    if (track.at < track.end && ReadVarLen(data, track.at, track.end, next)) {
      track.nextTick += next;
    } else {
      track.done = true;
    }
  }

  // Max: sequencetempo is "the unmodified tempo of the sequence", so it is what
  // the file declared whether or not the attribute overrode it, and tempo
  // "reflects the current tempo at the current playback time" — which at the
  // start of a freshly read sequence is the tempo it starts at.
  sequenceTempo = firstTempo;
  if (!overrideTempo) tempo = firstTempo;
  return true;
}

bool gSeq::LoadFrom(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // Max reads two formats and this is the only thing that tells them apart:
  // "MThd" is the standard MIDI file's own magic, and anything else is Max's
  // text form.
  if (length >= 4 && text[0] == 'M' && text[1] == 'T' && text[2] == 'h' && text[3] == 'd')
    return LoadMidiFile((const unsigned char*)text, length);

  LoadText(text, length);
  return true;
}

void gSeq::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread, and the one place the patcher's file table can be built: a
  // `read` arriving later on the audio thread has to find it already there
  // (issue #683).
  EnableFileIO();
  // Max's filename argument "specifies the name of a file to be read into seq
  // automatically when the patch is loaded", and this is where an object is
  // loaded — CreateObjectUnlocked parses the parameters and then calls this, so
  // the name is already here. It is the same deferred request a `read` message
  // makes, so the sequence arrives with the patcher's next block and outlet 3
  // bangs then.
  if (!readPath.empty()) RequestFile(FILE_OP::READ, nullptr, 0, 0);
}

void gSeq::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  // Max has no outlet for a finished write anywhere in this family, so a write
  // reports only by having happened. A failed read — missing, unopenable, or not
  // a sequence at all — reports by the outlet staying silent.
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) return;
  if (!LoadFrom(result.bytes, result.byteCount)) return;

  // The file outlet, after the sequence is in place so a patch that answers it
  // with a `start` finds it. `thread` is the tag the scheduler delivered —
  // forwarded unchanged, because Pass* picks its mechanism from the render-frame
  // marker rather than from the tag (issue #690).
  if (outputs.size() > 3) outputs[3].SendBang(thread);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "bang — starts playing the sequence stored in seq", at the recorded
  // tempo.
  StartPlaying(NORMAL_SPEED, thread);
}

INT_IN(IntIn) {
  (void)inlet;
  (void)thread;
  Record(value);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  (void)thread;
  // Max: "float — converted to int."
  Record((int)value);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // Not a command: a list of numbers is a run of MIDI bytes, recorded in order.
  // Max documents no list method — its own chain feeds seq one int at a time —
  // but every message in this patcher arrives as a list, so a numeric list is
  // the shape midiformat's output actually has here and refusing it would leave
  // the object unreachable. The bytes of one list all record inside one
  // dispatch and so share one block, which gives the second and third of a
  // note-on a delta of zero: exactly what playback runs out together.
  std::size_t cursor = 0;
  int number = 0;
  while (ReadIntArgAt(value, cursor, number))
    Record(number);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gSeq::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

std::string gSeq::EventAt(std::size_t index) const {
  storeGuard guard(busy);
  if (!guard.Held() || index >= count) return std::string();

  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt(events[index].deltaMs, digits);
  std::string text(digits, written);
  text.push_back(' ');

  // A meta event has no byte to show, so it names itself instead (issue #692).
  // The payload is not spelled out here: what a patch actually receives is the
  // meta outlet's message, and building that needs the send buffer a running
  // sequence may be halfway through using.
  if (events[index].meta && events[index].metaIndex < metaCount) {
    const Meta& meta = metas[events[index].metaIndex];
    text.append("meta ");
    const char* name = MetaName(meta.type);
    if (name != nullptr) {
      text.append(name);
    } else {
      text.append(digits, WriteInt((int)meta.type, digits));
    }
    return text;
  }

  written = WriteInt((int)events[index].byte, digits);
  text.append(digits, written);
  return text;
}

std::size_t gSeq::Position() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return position;
}

bool gSeq::IsRecording() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return recording;
}

bool gSeq::IsPlaying() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return playing;
}

int gSeq::Speed() const {
  storeGuard guard(busy);
  if (!guard.Held()) return NORMAL_SPEED;
  return speed;
}

bool gSeq::IsTickDriven() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return speed == TICK_SPEED;
}

std::string gSeq::Filename() const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  return fileName;
}

float gSeq::Tempo() const {
  storeGuard guard(busy);
  if (!guard.Held()) return DEFAULT_TEMPO;
  return tempo;
}

float gSeq::SequenceTempo() const {
  storeGuard guard(busy);
  if (!guard.Held()) return DEFAULT_TEMPO;
  return sequenceTempo;
}

bool gSeq::OverridesTempo() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return overrideTempo;
}

std::size_t gSeq::MetaCount() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return metaCount;
}

#undef className
