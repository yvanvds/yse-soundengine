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
      "A bang sends the stored message, as a load does. 'set <message>' replaces it without "
      "sending; a bare 'set' empties it. Other lists are ignored.";

  constexpr char kOutletDoc[] =
      "The stored message when the patch has finished loading, and for each bang in: a bang, an "
      "int, a float or a list, by its form. Nothing when empty.";

  constexpr char kMessageDoc[] =
      "The message to send, such as '60' or 'mode 2'. At most 256 characters; with none, the "
      "object sends nothing.";

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
      "Sends its arguments as a message when the patch it is in has finished loading, such as "
      "'.loadmess 0.5' to set a gain. A bang at the inlet sends it by hand.");
  ADD_CATEGORY(pCategory::CONTROL);

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
