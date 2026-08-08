#include "gTextfile.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gTextfile

namespace {

  // A line that is a whole number only leaves as an int when the int can hold
  // it: casting a float outside the int range is undefined behaviour, and a
  // patch that stored a huge integer is better served by the float that still
  // carries its value. `.route` and `.coll` decide the same question the same
  // way.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
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

  // Trim the separators off both ends of [begin, end).
  void Trim(const char* text, std::size_t& begin, std::size_t& end) {
    while (begin < end && IsSelectorSeparator(text[begin]))
      begin++;
    while (end > begin && IsSelectorSeparator(text[end - 1]))
      end--;
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

  constexpr char kInletDoc[] =
      "Everything arrives here. A message that is not one of the reserved words is appended to the "
      "current line — Max's 'the message is stored in the text object, placed after any previously "
      "stored messages, and is followed by a space' — with one space separating it from what the "
      "line already carried. An int or a float is spelled the patcher's one way, so the text "
      "agrees "
      "with what every other object writes for that value; a list is appended as it arrived, since "
      "it is already the text the patch composed. The reserved words are 'clear', which erases the "
      "contents; 'cr', which ends the current line, and which leaves a blank line behind when no "
      "line is open, so two in a row separate two full lines; 'tab', which writes a tab and no "
      "space after it (Max's 'the tab stop replaces that space'); 'dump', which sends every line "
      "out "
      "outlet 0 in order; 'line <n>', which sends line n preceded by the word 'set', numbering "
      "from "
      "1, converting anything below 1 to 1 and sending nothing for a line that does not exist; "
      "'query', which sends the line count out outlet 1; 'symbol <word>' / 't_symbol <word>', "
      "which store a word that would otherwise be read as one of these — Max's own escape hatch, "
      "'useful if you want to store a word that would otherwise be understood as a specific "
      "message "
      "by text'; and 'read [file]' / 'write [file]', which move the contents through a plain text "
      "file, one line per line. Neither opens anything here: the request is a wait-free claim on a "
      "patcher-owned slot, the disk work runs on the background pool, and a read replaces the "
      "contents in the completion the patcher delivers at the top of a later block — which is also "
      "when outlet 2 bangs. Both bare forms reuse the last name given, which starts as the "
      "filename creation argument, since Max's bare forms open a file dialog and a headless "
      "patcher has none. 'open', 'wclose', 'settitle', 'filetype', 'precision' and "
      "'stringout' are consumed and do nothing: there is no window here and no file dialog for "
      "filetype to narrow, and the two attribute names set an attribute in Max rather than being "
      "stored. They are consumed rather than stored because Max dispatches "
      "on the "
      "selector and so cannot store them either, and contents that differed from Max's for the "
      "same "
      "patch would be the one thing this object must not produce. A line longer than 256 "
      "characters, "
      "or a new line past the 256th, is refused whole and silently, since the inlet may be the "
      "audio "
      "thread.";

  constexpr char kTextDoc[] =
      "The lines. 'dump' sends every one of them in order, one send per line, and 'line <n>' sends "
      "the one asked for preceded by the word 'set' — Max's 'the text of the specified line number "
      "is sent out preceded by the word set ... can be sent to any other object for which that "
      "particular set message is appropriate', which is not a foreign word here: .table, .funbuff, "
      ".bucket, .cycle, .accum and .match all take one. A 'line' is therefore always a list, being "
      "'set' plus the contents; a dumped line leaves in the kind it is, .route's rule — a line "
      "holding '60' as the int 60, one holding '60.5' as that float, and anything else as a list. "
      "An "
      "empty line is still a line and leaves as an empty list, so a dump sends exactly as many "
      "messages as 'query' reports. Appending sends nothing.";

  constexpr char kCountDoc[] =
      "How many lines the contents hold, in response to 'query' — Max's 'a number that specifies "
      "the "
      "number of lines stored in the text object'. Counted the way 'dump' walks them, so the two "
      "always agree. Max's middle outlet, which bangs when a file has finished loading, is the "
      "third one here rather than the second: it was appended after this one rather than inserted "
      "in Max's position, so the cords of every patch saved while .textfile had two outlets still "
      "land where they did (issue #687).";

  constexpr char kFileDoc[] =
      "Bangs when a 'read' has finished loading a file into the contents — Max's middle outlet, "
      "appended here as the third so no saved patch's cords shift (the promise #499 made and the "
      "rule the rest of the file-reading family follows). It fires only on success: a file that "
      "does not exist, does not fit, or cannot be opened leaves it silent, and it does not fire "
      "for a write, for which Max has no outlet either. It fires a block or more after the read "
      "message rather than inside it, because the disk work happens on the background pool — a "
      "read arrives on whichever thread dispatched it, which may be the audio callback. The "
      "filename creation argument's read fires it too, since it is the same request.";

} // namespace

CONSTRUCT() {
  // The filename is built by ParseParams, so a saved `.textfile notes.txt` comes
  // back holding it. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous name in
  // place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has: every message and every value arrives here. No bang —
  // Max documents no bang method for `text`, and one that dumped would give the
  // object two spellings of `dump` on the same inlet.
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY on outlet 0: a dumped line leaves as a list, an int or a float depending
  // on what it holds, and a `line` is always a list. Outlet 1 is the line count,
  // always an int.
  ADD_OUT_ANY;
  ADD_OUT_INT;
  // Appended, not inserted: Max puts the file outlet second, but .textfile
  // shipped with two outlets in #499 and moving the line-count outlet would
  // shift the cords of every patch saved since (issue #687).
  ADD_OUT_BANG;

  // The whole table and the send buffer, taken once here on the control thread.
  // Nothing on a message path ever resizes either, which is what makes an append
  // from a rendering graph allocation-free. One past capacity so a line filled to
  // LINE_CAPACITY still has room for the terminator c_str() needs.
  lines.resize(MAX_LINES);
  for (std::string& line : lines)
    line.reserve(LINE_CAPACITY + 1);
  // The longest send is "set " plus a full line.
  sendText.reserve(LINE_CAPACITY + 5);

  // Same treatment for the file buffers (issue #687): a `read` or `write` may
  // arrive on the audio thread, so remembering a name and formatting the whole
  // contents both have to reuse storage that already exists.
  readPath.reserve(fileScheduler::PATH_CAPACITY);
  writePath.reserve(fileScheduler::PATH_CAPACITY);
  fileScratch.reserve(FILE_TEXT_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Collects the messages it is sent as lines of text — Max's text, which 'collects and formats "
      "incoming messages as text to be output as lines of text'. The line is the unit, and that is "
      "the whole difference from the four stores before it: .coll is keyed by an address, .bag has "
      "no keys, .funbuff is a sparse function of x,y pairs and .table a dense array of numbers, "
      "while .capture — the closest, since it too swallows whatever arrives — stores one atom per "
      "item, so a three-element list becomes three entries and comes back as three sends. Here a "
      "message is appended to the current line, several accumulate on it separated by spaces, and "
      "'cr' ends it. That is the shape a note list, a cue sheet or a configuration block has, and "
      "it "
      "is the shape a text file has. Max keeps the separating space after each stored message, "
      "which "
      "is why both its 'cr' and its 'tab' carry the rule 'if the last character in text is a "
      "space, "
      "the carriage return replaces that space'; the space is kept between messages here instead, "
      "which gives the same visible text with no trailing space to hand downstream, and 'tab' "
      "clears "
      "the pending separator so that 5, tab, 6 gives 5<tab>6 rather than 5<tab> 6 — Max's rule "
      "spelled as state rather than as a fix-up. An int or a float is spelled the patcher's one "
      "way, "
      "WriteInt and ExprFormatValue, so the text agrees with what every other object writes for "
      "the "
      "same value; a list is appended as it arrived, being already the text the patch composed. "
      "'clear' erases the contents, 'dump' sends every line out outlet 0, 'line <n>' sends one "
      "line "
      "preceded by the word 'set' (Max's, and not a foreign word here — .table, .funbuff, .bucket, "
      ".cycle, .accum and .match all take a set), numbering from 1 and sending nothing for a line "
      "that does not exist, and 'query' sends the line count out outlet 1. A dumped line leaves in "
      "the kind it is, .route's rule. Max's own escape hatch is ported: 'symbol clear' stores the "
      "word clear, 'useful if you want to store a word that would otherwise be understood as a "
      "specific message by text'. 'open', 'wclose', 'settitle', 'filetype', "
      "'precision' and 'stringout' are consumed and do nothing — Max dispatches on the selector, "
      "so "
      "a text in Max cannot store those symbols either, and contents differing from Max's for the "
      "same patch is the one thing this object must not produce. 'read [file]' and 'write [file]' "
      "move the contents through a plain text file, one stored line per line of the file, and the "
      "filename creation argument is read when the object is built — Max's 'names a text file to "
      "be read in when the object is loaded' (issue #687). None of that happens on the message "
      "path: a message handler runs on whichever "
      "thread the message arrived on, in-patcher delivery dispatches on the audio thread, and "
      "THREAD "
      "is a dispatch-semantics tag rather than a thread identity, so there is no predicate an "
      "object "
      "can ask to find out it is not on the audio callback — opening a file there would block it. "
      "The request is instead a wait-free claim on a patcher-owned slot, the disk work runs on the "
      "background pool honouring the host's IO() layer, and the bytes are parsed in the completion "
      "the patcher delivers at the top of a later block, which is also when outlet 2 bangs — the "
      "shared fileScheduler plumbing of issue #683. The last line carries a newline only when it "
      "is closed, so a write then read round trip is exact down to whether the last line is still "
      "taking appends; a trailing carriage return is dropped on reading, so a CRLF file written by "
      "another editor loads the same lines everywhere. A read replaces what is held, an over-long "
      "line is skipped with the rest of the file still loading, lines past the 256th are dropped, "
      "and a file too large or unopenable is refused whole with outlet 2 silent. The bare forms of "
      "read and write reuse the last name given, which starts as the filename argument, since "
      "Max's open a file dialog and a headless patcher has none, and filetype is consumed and "
      "inert for the same reason; Max has no readagain / writeagain for text, so neither is "
      "invented here. Max's middle outlet, which bangs when "
      "a "
      "file has finished loading, is outlet 2 rather than outlet 1: it was appended rather than "
      "inserted in Max's position, because #499 shipped this object with two outlets and moving "
      "the line-count outlet would shift every saved patch's cords. The store is .coll's model "
      "and for "
      ".coll's "
      "reason — a fixed table of lines allocated whole at construction, every line reserved to its "
      "capacity, plus .value's non-blocking guard — because a copy-on-write GraphState publish "
      "assumes the writer is the control thread while this object is written by whichever thread "
      "its "
      "message arrived on. The guard is never held across a send, a loser of it drops rather than "
      "waiting, and dump takes it once per line and re-reads the bounds each step. At most 256 "
      "lines "
      "of at most 256 characters, Max's own limit for the line message; anything that does not fit "
      "is refused whole rather than truncated, since half a line is a different line. Calculate() "
      "does nothing. The filename survives a save because it is a creation argument; the contents "
      "deliberately do not, which is the family's rule of saving exactly where Max has a save flag "
      "— "
      "coll has 'save data with patcher', table and funbuff have embed, and text has none of them, "
      "because Max keeps a text's contents in a file — which since #687 is where they live here "
      "too, a '.textfile notes.txt' reloading its lines when the patch it was saved in opens. Not "
      "ported: the editing window and "
      "everything "
      "addressing it (open, wclose, settitle, the double-click), filetype, the precision attribute "
      "— "
      "the patcher has one way of spelling a float, the fewest digits that read back as the same "
      "value, which loses nothing and pads nothing, and a second spelling on one object would make "
      "its text disagree with every other's — and stringout, there being no string atom to "
      "output.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", kInletDoc, "at most 256 lines of 256 characters");
  OUTLET_DOC(0, "text", kTextDoc, "");
  OUTLET_DOC(1, "lines", kCountDoc, "0-256");
  OUTLET_DOC(2, "file", kFileDoc, "");
  PARAM_DOC(
      "filename", "",
      "Max's filename argument, which 'names a text file to be read in when the object is "
      "loaded'. The first argument token is taken as the name. It is held and it survives a "
      "save, so a '.textfile mydata.txt' brought across from Max builds the object it names "
      "and the argument the author typed comes back unchanged. It is also read when the object "
      "joins a patcher, which is where 'when the object is loaded' happens here, and it seeds "
      "the name a bare 'read' or 'write' falls back on — so a bare 'write' saves back over the "
      "file the object is named after (issue #687). A standalone object with no patcher has no "
      "file plumbing and reads nothing. Setting the "
      "parameters again also empties the contents, since the object that comes back is the one "
      "the arguments describe.",
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
  lineCount = 0;
  lineOpen = false;
  needsSeparator = false;
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
  // `write` falls back on, there being no dialog to ask with (issue #687). A
  // name the scheduler could not carry anyway is not remembered.
  if (!fileName.empty() && fileName.size() < fileScheduler::PATH_CAPACITY) {
    readPath = fileName;
    writePath = fileName;
  }

  // A re-parse also drops the contents: the object that comes back is the one
  // the arguments describe, and text left over from before would belong to a
  // file the object is no longer named after.
  lineCount = 0;
  lineOpen = false;
  needsSeparator = false;
}

// ─── the contents ─────────────────────────────────────────────────────────────

bool gTextfile::OpenLine() {
  if (lineOpen) return true;

  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (lineCount >= MAX_LINES) return false;

  // The string keeps its storage — only `lineCount` says which lines are live —
  // so starting a line is a clear() into capacity that already exists.
  lines[lineCount].clear();
  lineCount++;
  lineOpen = true;
  needsSeparator = false;
  return true;
}

bool gTextfile::AppendText(const char* text, std::size_t length) {
  std::string& line = lines[lineCount - 1];

  // Max: each stored message "is followed by a space". Kept between messages
  // rather than after each one — see the class documentation.
  const std::size_t extra = needsSeparator ? length + 1 : length;

  // Refused rather than truncated: half a line is a different line, and a patch
  // building a cue sheet could not tell a clipped entry from a stored one.
  if (line.size() + extra > LINE_CAPACITY) return false;

  if (needsSeparator) line.push_back(' ');
  // append() into a string reserved to LINE_CAPACITY + 1 at construction, so
  // this allocates nothing.
  line.append(text, length);
  needsSeparator = true;
  return true;
}

void gTextfile::Append(const char* text, std::size_t length) {
  // An empty message is not a message: appending it would start a line that
  // nothing was ever stored on.
  if (length == 0) return;

  // Nothing longer than a whole line can ever land, so it is refused *before* a
  // line is opened — a message that could not be stored must not leave an empty
  // line behind for `query` and `dump` to report.
  if (length > LINE_CAPACITY) return;

  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (!OpenLine()) return;
  AppendText(text, length);
}

void gTextfile::AppendInt(int value) {
  // WriteInt rather than std::to_string: no allocation and no locale, and it is
  // the patcher's one way of turning an int into text.
  char digits[FORMAT_INT_WIDTH];
  Append(digits, WriteInt(value, digits));
}

void gTextfile::AppendFloat(float value) {
  // ExprFormatValue writes the fewest digits that read back as the same float,
  // allocation-free and locale-free, and always leaves a float visibly a float.
  char digits[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), digits, kExprValueTextMax);
  if (written <= 0) return;
  Append(digits, (std::size_t)written);
}

void gTextfile::Cr() {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  if (lineOpen) {
    // Max: "puts a carriage return at the end of the contents of text, to start
    // a new line". The new line is not materialised until something is stored on
    // it, so `query` counts the lines that exist rather than one more.
    lineOpen = false;
    needsSeparator = false;
    return;
  }

  // Nothing open: the contents already end in a carriage return, so this one
  // leaves a blank line between the last one and whatever comes next.
  if (lineCount >= MAX_LINES) return;
  lines[lineCount].clear();
  lineCount++;
  needsSeparator = false;
}

void gTextfile::Tab() {
  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (!OpenLine()) return;

  std::string& line = lines[lineCount - 1];
  if (line.size() + 1 > LINE_CAPACITY) return;
  line.push_back('\t');
  // Max: "if the last character in text is a space, the tab stop replaces that
  // space" — the space is never written here, so clearing the pending separator
  // is the same rule with nothing to undo.
  needsSeparator = false;
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gTextfile::SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread) {
  // `.route`'s rule for a remainder, minus its bang case: an empty line is still
  // a line, and a bang would read downstream as "no data" rather than as "empty
  // data" — and a dump has to send exactly as many messages as `query` reports.
  const std::size_t length = text.size();
  float number = 0.f;
  if (length > 0 && ReadNumericToken(text.c_str(), length, number)) {
    // Int or float is decided by the spelling, the test .trigger, .match, .route
    // and .coll already share, so a line holding `60` does not come back as
    // `60.`.
    if (!TokenLooksLikeFloat(text.c_str(), length) && FitsInt(number)) {
      outputs[pin].SendInt((int)number, thread);
    } else {
      outputs[pin].SendFloat(number, thread);
    }
    return;
  }
  outputs[pin].SendList(text, thread);
}

bool gTextfile::Output(std::size_t index, bool withSet, YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    if (!guard.Held()) return false;
    if (index >= lineCount) return false;

    // Copied out under the guard and sent after it: holding it across a
    // synchronous fan-out would make a patch that wires an outlet back into this
    // object's inlet lose its own message to the guard it is still holding, and
    // handing the outlet the stored string would let that patch mutate the very
    // message still being fanned out. The buffer was reserved at construction, so
    // this allocates nothing.
    sendText.clear();
    if (withSet) {
      // Max: "sent out preceded by the word set".
      sendText.append("set", 3);
      if (!lines[index].empty()) sendText.push_back(' ');
    }
    sendText.append(lines[index]);
  }

  if (withSet) {
    // Always a list: `set` plus the contents is more than one atom whatever the
    // line holds.
    outputs[0].SendList(sendText, thread);
    return true;
  }
  SendTyped(0, sendText, thread);
  return true;
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gTextfile::HandleCommand(const char* word, std::size_t length, const std::string& message,
                              std::size_t argOffset, YSE::THREAD thread) {
  if (TokenIs(word, length, "clear", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Max: "erases the contents of text". The strings keep their storage: only
    // `lineCount` says which lines are live, so clearing is O(1) and the capacity
    // the next append needs is still there.
    lineCount = 0;
    lineOpen = false;
    needsSeparator = false;
    return true;
  }

  if (TokenIs(word, length, "cr", 2)) {
    Cr();
    return true;
  }

  if (TokenIs(word, length, "tab", 3)) {
    Tab();
    return true;
  }

  if (TokenIs(word, length, "dump", 4)) {
    // The guard is taken once per line rather than once for the whole walk, so a
    // patch whose dump target appends back into this object is not locked out of
    // its own store. Bounds are therefore re-read every step: Output() stops as
    // soon as the index is past the end.
    for (std::size_t i = 0; Output(i, false, thread); i++) {}
    return true;
  }

  if (TokenIs(word, length, "line", 4)) {
    int number = 0;
    // A bare `line` is still the `line` selector in Max, so it is consumed —
    // there is simply no line to send.
    if (!ReadIntArg(message, argOffset, number)) return true;
    // Max: "lines are numbered beginning with 1; any line number message less
    // than 1 is converted to line 1."
    if (number < 1) number = 1;
    // "If a nonexistent line number is requested, nothing is sent out" — which
    // is what Output() answering false already does.
    Output((std::size_t)(number - 1), true, thread);
    return true;
  }

  if (TokenIs(word, length, "query", 5)) {
    std::size_t live = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      live = lineCount;
    }
    // Read under the guard and sent after releasing it, so a line appended by a
    // re-entrant send is not lost to a guard this object is still holding.
    outputs[1].SendInt((int)live, thread);
    return true;
  }

  if (TokenIs(word, length, "symbol", 6) || TokenIs(word, length, "t_symbol", 8)) {
    // Max's escape hatch: "symbol clear stores the word clear ... rather than
    // erasing the contents". Max documents one word; the whole remainder is
    // stored, which agrees with Max on every input Max documents and keeps a
    // multi-word argument from being silently halved.
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    Append(message.c_str() + begin, end - begin);
    return true;
  }

  // The file half (issue #687). Both are a claim on a patcher-owned slot and
  // nothing more: whichever thread is dispatching, no file is opened here. The
  // whole remainder is the path, so a name with spaces in it still works, and a
  // bare form reuses the last name given — Max's opens a file dialog, which a
  // headless patcher has no equivalent of.
  const bool isRead = TokenIs(word, length, "read", 4);
  if (isRead || TokenIs(word, length, "write", 5)) {
    const FILE_OP op = isRead ? FILE_OP::READ : FILE_OP::WRITE;
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    RequestFile(op, message.c_str() + begin, end - begin);
    return true;
  }

  // Consumed and inert. Max dispatches on the selector, so a `text` in Max
  // cannot store these symbols either, and contents that differed from Max's for
  // the same patch is the one thing this object must not produce. There is no
  // window to open, close, title or type — `filetype` narrows the file dialogs
  // this patcher does not have — and `precision` and `stringout` set an
  // attribute in Max rather than being stored.
  if (TokenIs(word, length, "open", 4)) return true;
  if (TokenIs(word, length, "wclose", 6)) return true;
  if (TokenIs(word, length, "settitle", 8)) return true;
  if (TokenIs(word, length, "filetype", 8)) return true;
  if (TokenIs(word, length, "precision", 9)) return true;
  if (TokenIs(word, length, "stringout", 9)) return true;

  return false;
}

// ─── files ────────────────────────────────────────────────────────────────────

bool gTextfile::RequestFile(FILE_OP op, const char* name, std::size_t length) {
  fileScheduler* io = FileIO();
  // A standalone .textfile has no patcher and so no plumbing. Silent: this may
  // be the audio thread, where a log line would allocate.
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
  if (!Serialize()) return false;
  return io->RequestWrite(this, FILE_TAG_WRITE, remembered.c_str(), remembered.size(),
                          fileScratch.c_str(), fileScratch.size());
}

bool gTextfile::Serialize() {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists. FILE_TEXT_CAPACITY is every line at
  // its maximum plus its newline, so the buffer cannot run out.
  fileScratch.clear();
  for (std::size_t i = 0; i < lineCount; i++) {
    fileScratch.append(lines[i]);
    // A newline after every line except an open last one. Max's buffer is flat
    // text where a `cr` is a character, so contents still taking appends end
    // without one — which is what makes the round trip exact rather than merely
    // equal: reading this back leaves that line open again.
    if (i + 1 < lineCount || !lineOpen) fileScratch.push_back('\n');
  }
  return true;
}

bool gTextfile::LoadFrom(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // Max's read replaces the contents. The strings keep their storage — only
  // `lineCount` says which lines are live — so this costs nothing.
  lineCount = 0;
  lineOpen = false;
  needsSeparator = false;

  std::size_t at = 0;
  while (at < length && lineCount < MAX_LINES) {
    std::size_t end = at;
    while (end < length && text[end] != '\n')
      end++;

    // Whether the line was terminated, which is also whether it is closed: a
    // file that does not end in a newline leaves its last line taking appends.
    const bool terminated = end < length;

    std::size_t stop = end;
    // A CRLF file written by another editor reads as the same lines everywhere.
    // Only the terminator's own carriage return is dropped; one in the middle of
    // a line is text.
    if (stop > at && text[stop - 1] == '\r') stop--;

    const std::size_t lineLength = stop - at;
    // Refused rather than truncated, the rule an over-long append already
    // follows — and only this line: the rest of the file still loads.
    if (lineLength <= LINE_CAPACITY) {
      // assign() into a string reserved to LINE_CAPACITY + 1 at construction, so
      // this allocates nothing.
      lines[lineCount].assign(text + at, lineLength);
      lineCount++;
      lineOpen = !terminated;
      needsSeparator = lineOpen && lineLength > 0;
    }

    at = terminated ? end + 1 : length;
  }
  return true;
}

void gTextfile::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread, and the one place the patcher's file table can be built: a
  // `read` arriving later on the audio thread has to find it already there
  // (issue #683).
  EnableFileIO();
  // Max's filename argument "names a text file to be read in when the object is
  // loaded", and this is where an object is loaded — CreateObjectUnlocked parses
  // the parameters and then calls this, so the name is already here. It is the
  // same deferred request a `read` message makes, so the contents arrive with
  // the patcher's next block and outlet 2 bangs then.
  if (!readPath.empty()) RequestFile(FILE_OP::READ, nullptr, 0);
}

void gTextfile::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  // Max has no outlet for a finished write, so a write reports only by having
  // happened. A failed read reports by the outlet staying silent.
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) return;
  if (!LoadFrom(result.bytes, result.byteCount)) return;

  // Max's middle outlet: "bang when a file has finished loading". After the
  // contents are in place, so a patch that reacts to it with a `line` or a
  // `dump` finds them. `thread` is the tag the scheduler delivered — forwarded
  // unchanged, because Pass* picks its mechanism from the render-frame marker
  // rather than from the tag (issue #690).
  outputs[2].SendBang(thread);
}

// ─── inlet ────────────────────────────────────────────────────────────────────

INT_IN(IntIn) {
  (void)inlet;
  (void)thread;
  AppendInt(value);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  (void)thread;
  AppendFloat(value);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first — see the class documentation for why a data inlet gets to
  // reserve words here at all.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // Max's list and anything methods: "the message is stored in the text object,
  // placed after any previously stored messages". Stored as it arrived — it is
  // already the text the patch composed, and re-spelling its atoms would rewrite
  // a message somebody wrote on purpose.
  std::size_t restBegin = begin;
  std::size_t restEnd = length;
  Trim(text, restBegin, restEnd);
  Append(text + restBegin, restEnd - restBegin);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gTextfile::LineCount() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return lineCount;
}

std::string gTextfile::LineAt(std::size_t index) const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  if (index >= lineCount) return std::string();
  return lines[index];
}

bool gTextfile::LineIsOpen() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return lineOpen;
}

std::string gTextfile::Filename() const {
  return fileName;
}

#undef className
