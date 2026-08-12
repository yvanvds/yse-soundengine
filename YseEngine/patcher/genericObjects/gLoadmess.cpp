// `.loadmess` (issue #547). See gLoadmess.h for the design; this file is the
// stored message, Max's `set`, and the one send both the load hook and the
// inlet end at. When the load hook is called is gLoadbang.h's subject and
// patcherImplementation::LoadbangObjects's code.
#include "gLoadmess.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gLoadmess

namespace {

  // The one message that is a word rather than a number — `.if`'s phrase, and
  // `.if`'s rule, applied to the same problem.
  constexpr char kBangWord[] = "bang";

  // Max's setter, and the length MatchWord needs told separately.
  constexpr char kSetWord[] = "set";
  constexpr std::size_t kSetLength = sizeof(kSetWord) - 1;

  constexpr char kInletDoc[] =
      "A bang here sends the stored message, exactly as a load does — Max's 'sending a bang "
      "message "
      "to a loadmess object causes it to output its typed message'. It is the manual trigger, and "
      "it is the answer to the one case a load cannot cover: an object added to a patch that is "
      "already running never receives a loadbang, because at creation time it has no cords to send "
      "down and re-firing on a later edit would overwrite whatever the patch has done since. "
      "'set <message>' replaces the held message without sending anything — Max's 'the word set "
      "followed by any message will set the message held by loadmess without any output (can be "
      "used for output in conjunction with bang)' — and a bare 'set' empties it, leaving the "
      "object "
      "a .loadmess with no arguments. It works whether the text arrives as a list or as a message "
      "box's text, since those are two different routes into an object and set has to work on "
      "both. A list that does not begin with 'set' is ignored rather than stored: Max documents "
      "exactly bang, set and a double-click, and treating a bare list as an implicit set would let "
      "every value that happened to pass through this object overwrite the initialisation it "
      "exists to hold. A set longer than 256 characters is refused whole rather than truncated — "
      "half a message is a different message — and counted.";

  constexpr char kOutletDoc[] =
      "The stored message, sent once when the patcher finishes loading — Max's 'the loadmess "
      "object's typed message is sent automatically when the patch is loaded' — and again for "
      "every bang that arrives at the inlet. It leaves in the kind it is, the rule .route, .coll, "
      ".textfile and .qlist already share: the single word 'bang' is a bang, one token that is a "
      "whole finite number is an int when spelled as one and a float when it carries a '.' or an "
      "exponent, and anything else is a list carrying the text verbatim. With no message held, "
      "nothing is sent at all — .loadbang is the object for wanting a bang. The load message "
      "leaves here only after the whole parsed graph has been built, wired and published, so "
      "everything the file describes is already in place when it travels. The order in which two "
      "load objects in one patch fire is not defined, as in Max; put a .trigger between them when "
      "one initialisation must precede another.";

  constexpr char kMessageDoc[] =
      "Max's argument list, in Max's order, taken whole: 'any arguments you type into a loadmess "
      "object are treated as a message to be sent when output is triggered'. It is sent in the "
      "kind it is — 'bang' as a bang, a lone number as an int or a float by its spelling, anything "
      "else as a list carrying the text verbatim — so '.loadmess 60' initialises a pitch and "
      "'.loadmess 0.5' a gain, without either needing a message box in between. With no argument "
      "the object holds nothing and sends nothing; .loadbang is the object for wanting a bang. At "
      "most 256 characters are held, which is the patcher's own bound (a .coll entry, a .bag, an "
      ".offer table and the patcher's value queue are all this number, so anything that can reach "
      "this object through a patch also fits in it); a longer argument list stops at the last "
      "token that fits rather than failing to load, since a creation argument arriving from a "
      "hand-edited or newer saved patch must never be able to break loading it. The argument "
      "string is saved verbatim whatever a later 'set' did, because a set is a live override and "
      "the typed message is what the object is.";

} // namespace

CONSTRUCT() {
  // The message is built by ParseParams, so a saved `.loadmess 1 2 3` comes back
  // as that object. The clear callback is what makes `SetParams("")` return it
  // to Max's no-argument shape rather than leaving the previous message
  // standing.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet: Max's bang and Max's `set`.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // One outlet. Max types it `symbol`, but what actually leaves depends on what
  // is held, so it is ANY — see the outlet doc.
  ADD_OUT_ANY;

  // Reserve the store here, on the control thread, so that every later write to
  // it assigns into storage the object already owns. This one call is what makes
  // `set` allocation-free on whatever thread it runs on, which matters because
  // in-patcher delivery dispatches on the audio thread — the same trick, for the
  // same reason, as patcherImplementation's listScratch_.
  message.reserve(MESSAGE_CAPACITY);

  ADD_DESCRIPTION(
      "Sends a stored message once the patcher has finished loading — Max's 'loadmess', which "
      "'outputs a message automatically when the file is opened, or when the patch is part of "
      "another file that is opened', where 'any arguments you type into a loadmess object are "
      "treated as a message to be sent when output is triggered'. It is .loadbang with a payload, "
      "and it is the half that does the actual initialising: '.loadbang -> .m 0.5 -> ~sine' is "
      "three objects and a cord where '.loadmess 0.5 -> ~sine' is one, so a patch that sets its "
      "own gains, cutoffs, tempos and modes at load needs one of these per value and nothing from "
      "the host at all. It fires exactly where .loadbang fires and through the same hook: once, at "
      "the end of ParseJSON, after the parsed graph has been compiled and published with a single "
      "atomic swap — never during the build, where the cords do not exist yet or exist only in "
      "part. The pass runs on the control thread outside the patcher's lock, so a patch whose "
      "initialisation travels through a .forward or a .qlist loads rather than deadlocking, and "
      "the order of two load objects is undefined as in Max. An object created live through "
      "CreateObject never receives one and stays silent until the patch is saved and loaded again; "
      "the consequence is sharper here than for .loadbang, since a .loadmess that fired on every "
      "edit would re-send its value every time anything in the patch was touched, and an "
      "initialisation that will not stay initialised is not one. The inlet is the manual trigger "
      "for that case. What is sent leaves in the kind it is, the rule .route, .coll, .textfile and "
      ".qlist already share: nothing at all when nothing is typed (.loadbang being the object for "
      "wanting a bang), a bang for the single word 'bang', an int or a float for one token that is "
      "a whole finite number depending on whether it is spelled with a '.' or an exponent, and "
      "otherwise a list carrying the text verbatim — text travels as a list message in this "
      "patcher, the same deviation .trigger documents. 'set <message>' replaces the held message "
      "without output and a bare 'set' empties it, on both the list route and the message-box "
      "route; a list that does not begin with 'set' is ignored, since treating one as an implicit "
      "set would let every value passing through overwrite the initialisation. The store is a "
      "string whose capacity is reserved at construction, so set assigns into the buffer it "
      "already owns and allocates nothing on whatever thread the message arrived on; a message "
      "past 256 characters is refused whole and counted rather than truncated, half a message "
      "being a different message. Reads and writes are serialised by .value's non-blocking guard, "
      "and a loser of it drops and is counted rather than waiting. The guard is held across the "
      "send, unusually for this patcher and deliberately: what is being sent is the buffer itself, "
      "so releasing first would mean either copying it — an allocation on the audio thread — or "
      "handing the outlet a string another thread may be overwriting. The cost is that an outlet "
      "wired back into this object's own inlet drops, which is really a second benefit. "
      "Calculate() does nothing. The creation arguments are saved verbatim whatever a later set "
      "did, because a set is a live override and the typed message is what the object is.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "trigger/set", kInletDoc, "bang, or 'set <message>'");
  OUTLET_DOC(0, "out", kOutletDoc, "any");
  PARAM_DOC("message", "", kMessageDoc, "any message, up to 256 characters");
}

// ─── creation arguments ───────────────────────────────────────────────────────

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // message. `clear()` keeps the reserved capacity, which is the point.
  creationArgs.clear();
  message.clear();
}

PARM_PARSE() {
  // Control thread, and always before the object is published: a live SetParams
  // re-parse rebuilds the object instead, because registering these callbacks
  // makes Parameters::NeedsRebuild() true. So this is the one writer of
  // `message` that does not take the guard — there is no other thread that can
  // yet reach the object.
  message.clear();

  // Max: "any arguments you type into a loadmess object are treated as a message
  // to be sent". Joined back with single spaces, which is what Parameters::Set
  // split them on.
  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty token is not part of anything anyone typed.
    if (token.empty()) continue;

    const std::size_t separator = message.empty() ? 0 : 1;
    if (message.size() + separator + token.size() > MESSAGE_CAPACITY) {
      // Stop at the last token that fits rather than throwing or truncating
      // mid-token: a creation argument arriving from a hand-edited or newer
      // saved patch must never be able to break loading it, and the argument
      // string is stored verbatim by Parameters either way, so a save still
      // writes back what was typed.
      break;
    }
    if (separator != 0) message += ' ';
    message += token;
  }
}

// ─── the message ──────────────────────────────────────────────────────────────

void gLoadmess::Store(const char* text, std::size_t length) {
  if (length > MESSAGE_CAPACITY) {
    // Refused whole, `.capture`'s rule rather than `.print`'s: this stores a
    // message that will later be *sent*, and half of one is a different message
    // — where a printed line cut short is still almost all of the information.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  storeGuard guard(busy);
  if (!guard.Held()) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Within the capacity reserved in the constructor, so this reuses the buffer
  // the object already owns and allocates nothing.
  message.assign(text, length);
}

bool gLoadmess::TryStoreSet(const std::string& text) {
  // Max: "the word set followed by any message will set the message held by
  // loadmess without any output."
  std::size_t offset = 0;
  if (MatchWord(text, kSetWord, kSetLength, offset)) {
    // MatchWord steps over exactly one separator, so `set  0.5` would otherwise
    // store a leading space — and a message with one is a different message to
    // every reader below, which would send " 0.5" as a list rather than as the
    // float it plainly is. Trim both ends: the surrounding whitespace was never
    // part of what anyone typed.
    std::size_t end = text.size();
    while (offset < end && (text[offset] == ' ' || text[offset] == '\t'))
      offset++;
    while (end > offset && (text[end - 1] == ' ' || text[end - 1] == '\t'))
      end--;
    Store(text.c_str() + offset, end - offset);
    return true;
  }

  // MatchWord requires a separator after the word, so the bare `set` never
  // reaches it. It is still a set — of nothing — and emptying the store leaves
  // exactly the object a `.loadmess` with no arguments is.
  if (text == kSetWord) {
    Store("", 0);
    return true;
  }

  return false;
}

void gLoadmess::Emit(YSE::THREAD thread) {
  storeGuard guard(busy);
  if (!guard.Held()) {
    // Another thread is inside the store right now — a `set`, or this object's
    // own outlet wired back into its own inlet. Dropping is the answer rather
    // than waiting, which a message handler may never do; see the class notes
    // for why the guard is held across the send in the first place.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Nothing typed, nothing to send. `.loadbang` is the object for wanting a
  // bang, so inventing one here would only make the two objects overlap.
  if (message.empty()) return;

  // `.route`'s rule, shared with `.coll`, `.textfile` and `.qlist`: a stored
  // message leaves in the kind it is. Nothing below allocates —
  // `ReadNumericToken` parses into a stack buffer, and the list form hands the
  // outlet the stored string by reference.
  if (message == kBangWord) {
    sent.fetch_add(1, std::memory_order_relaxed);
    outputs[0].SendBang(thread);
    return;
  }

  float number = 0.f;
  if (ReadNumericToken(message, number)) {
    sent.fetch_add(1, std::memory_order_relaxed);
    // Int or float by the spelling — the test `.trigger`, `.match`, `.route`
    // and `.qlist` already share — so a stored `60` does not come back as `60.`.
    if (TokenLooksLikeFloat(message)) {
      outputs[0].SendFloat(number, thread);
    } else {
      // The token may be wider than an int even though ReadNumericToken agreed
      // it is a finite float, so truncate through the range-checked conversion
      // rather than casting.
      outputs[0].SendInt(ExprToInt(number), thread);
    }
    return;
  }

  sent.fetch_add(1, std::memory_order_relaxed);
  outputs[0].SendList(message, thread);
}

// ─── triggers ─────────────────────────────────────────────────────────────────

void gLoadmess::Loadbang(YSE::THREAD thread) {
  // The load has finished and the graph is published. Everything downstream of
  // this outlet exists and is wired, so the message reaches the patch the file
  // describes rather than a partial one. The caller's T_GUI is passed straight
  // through: this is the control thread, and T_GUI is "set the state and let the
  // block's own traversal render what you caused".
  Emit(thread);
}

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "Sending a bang message to a loadmess object causes it to output its
  // typed message." Deliberately the same call the load makes rather than a
  // near-copy of it — a host initialising a live-built graph must get the same
  // thing a load would have sent, or the two ways of starting a patch would not
  // start it the same way.
  Emit(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  (void)thread;
  // `set` and nothing else. A list that is not one is ignored rather than
  // stored — see the class notes.
  (void)TryStoreSet(value);
}

MESSAGES() {
  (void)value;
  // The other route text arrives by: a message box sends through
  // outlet::SendMessage, which lands here rather than at the list handler. `set`
  // has to work either way, which is why both call the same reader.
  (void)TryStoreSet(message);
}

#undef className
