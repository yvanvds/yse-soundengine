#include "gFunbuff.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gFunbuff

namespace {

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

  // The next whitespace-separated token read as a number, advancing `from` past
  // it. False when there is no token left or the one there is not a number.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(text, length, from, begin, end)) return false;
    // Strict, as the rest of the family is: ExprParseFloatList would read `5abc`
    // as 5 and fold `1e999` to 0, and neither answers "is this token a number at
    // all" — which is the question, since a message that is not numbers is not
    // one of Max's methods and has to be ignored rather than half-read.
    if (!ReadNumericToken(text + begin, end - begin, out)) return false;
    from = end;
    return true;
  }

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kXDoc[] =
      "The x of an x,y pair, and the hot inlet — it both writes and reads, and which one happens "
      "is "
      "decided by whether a y is waiting on inlet 1. Max: 'if a y value was previously received in "
      "the right inlet, the pair is stored; otherwise, outputs the corresponding y value'. The "
      "waiting y is consumed by exactly one x, so the next number here is a lookup again; a y that "
      "stayed behind would turn every later lookup into a store and the object could never be "
      "read. "
      "A float is converted to an int, so the stored function is an integer function. A lookup "
      "that "
      "misses an exact x falls back to Max's 'the closest x value which is less than the number "
      "received' — a step function, not an interpolation, which is what makes a plain lookup right "
      "for presets and zone maps; 'interp' is the message that interpolates. An x below every "
      "stored x has no lesser neighbour and sends nothing. A two-number list stores a pair: Max's "
      "ordinary right-to-left list distribution puts the second item on inlet 1 and the first "
      "here, "
      "so '60 100' stores (60, 100). The messages are 'set <x> <y> ...', 'delete <x> [<y>]', "
      "'clear', 'dump', 'goto <x>', 'next', 'min', 'max', 'find <y>', 'interp <x>' and 'embed "
      "<flag>'. 'print' and a bang are Max's Console diagnostics and are consumed silently, since "
      "there is no console here and neither sends anything out any outlet in Max either. A store "
      "past 256 pairs is refused whole and silently, since this inlet may be the audio thread.";

  constexpr char kYDoc[] =
      "The y that will be paired with the next x arriving on inlet 0 — Max's 'y value which will "
      "be "
      "paired with the next x value received in the left inlet, and stored'. Cold: setting it "
      "sends "
      "nothing and changes nothing already stored, it only arms the next x to be a store rather "
      "than a lookup. It is consumed by that one x and not by the one after it. A float is "
      "converted to an int. Nothing is armed until a y arrives, so a fresh object reads rather "
      "than "
      "writes; the armed y is run-time state and does not survive a save.";

  constexpr char kYOutDoc[] =
      "The y values, and everything else that answers a question. A plain x on inlet 0 sends the y "
      "stored at it, or the y of the closest lesser x when nothing is stored there exactly; 'next' "
      "sends the y at the pointer; 'min' and 'max' send the smallest and largest y stored, Max's "
      "'sends the minimum / maximum y value currently stored in the funbuff out the left outlet'; "
      "'find' sends every x whose y equals the number asked for, which is the one message that "
      "sends x values here rather than y values, exactly as in Max; 'dump' sends each pair's y "
      "after its x has gone out outlet 1. All of those are ints, because a stored y is an int. "
      "'interp' is the exception and leaves as a float: it is the one message reporting a value "
      "that was never stored, 'the y value that holds a corresponding position between the two "
      "neighboring y values' is fractional by definition, and truncating it would quantise every "
      "curve this object exists to draw. Max's reference does not state the type. Nothing is sent "
      "when there is no answer — an empty store, or an x below every stored x.";

  constexpr char kXOutDoc[] =
      "The x column. Under 'next' this is the *difference* between the x just reported and the one "
      "reported before it — Max's 'calculates the difference between that x value and the value "
      "previously pointed to' — which makes it the inter-onset interval of a time-stamped "
      "sequence, the gap a .metro or .line needs before the next event. Both the pointer and that "
      "reference x start at 0, so the first 'next' on a fresh object reports the first x itself, "
      "its distance from the origin. A 'goto' moves both, so a jump measures from where the patch "
      "jumped to. Under 'dump' this carries the x of each pair rather than a difference, and it "
      "fires before that pair's y leaves outlet 0 — Max's right-to-left outlet order, the order "
      ".coll and .trigger already fire in. Always an int.";

  constexpr char kEndDoc[] =
      "Bangs when 'next' has run off the end — when no stored x is at or past the pointer. Nothing "
      "leaves the other two outlets in that case, so this is how a patch learns a traversal is "
      "finished. The pointer is left past the end rather than wrapped, so a repeated 'next' keeps "
      "banging and a patch restarts with 'goto'. That is deliberately unlike .coll's 'next', which "
      "wraps: a collection is a ring of entries, while this is a traversal of a function, and a "
      "sequence that silently looped would be a different musical object.";

} // namespace

CONSTRUCT() {
  // Max's filename argument. Held so a `.funbuff mydata` brought across from Max
  // builds and so the argument survives a save, but read for nothing — there is
  // no file I/O here. No clear/parse callbacks: nothing is derived from it, so
  // there is no derived state for SetParams("") to have to undo.
  ADD_PARAM(filename);

  // Inlet 0 is the x and is hot; inlet 1 is the y and is cold. Max's split, and
  // the object's whole interface. No bang handler: Max's bang prints to the Max
  // Console and sends nothing out any outlet, so the honest headless behaviour
  // is an inlet that does not claim to take one.
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);

  // ANY on outlet 0: everything it carries is an int except `interp`, which is a
  // float. Outlet 1 is always an int — a difference or an x — and outlet 2 is
  // the end-of-traversal bang.
  ADD_OUT_ANY;
  ADD_OUT_INT;
  ADD_OUT_BANG;

  // The whole table, taken once here on the control thread. Nothing on a message
  // path ever resizes it, which is what makes a store from a rendering graph
  // allocation-free.
  pairs.resize(MAX_PAIRS);

  ADD_DESCRIPTION(
      "Stores pairs of numbers sorted by x and recalls the y at any x, with linear interpolation "
      "between the stored points — Max's funbuff, which 'stores, manages, and recalls pairs of "
      "numbers'. A sparse function: a handful of (x, y) points and a way to ask what y is at an x "
      "that was never stored, which is the data structure behind every breakpoint curve, tuning "
      "table, velocity map and step sequence. It is the first store in the family ordered by its "
      "own key — .coll is keyed but keeps storage order, .bag has no keys and .capture is a tape — "
      "and the sort is not a convenience but the object, since every question it answers is a "
      "question about where an x sits among the others. Inlet 0 is the x and is hot, inlet 1 is "
      "the "
      "y and is cold, and the same inlet writes and reads: Max's 'if a y value was previously "
      "received in the right inlet, the pair is stored; otherwise, outputs the corresponding y "
      "value'. The waiting y is consumed by exactly one x, since a y that stayed behind would turn "
      "every later lookup into a store, and a float is converted to an int on either inlet, so the "
      "stored function is an integer function. A two-number list stores a pair, Max's ordinary "
      "right-to-left list distribution. The plain lookup is a floor lookup and not an "
      "interpolation: Max's 'if there is no stored x value which matches the number received, "
      "funbuff uses the closest x value which is less than the number received'. That is where "
      "issue #497's shorthand and Max part company — the issue calls the lookup interpolated — and "
      "Max is followed, because a patch that wanted a stepped zone map and got a ramp would be "
      "silently wrong in a way nothing downstream could detect. Interpolation is the separate "
      "'interp' message, Max's 'measures its position between its two neighboring x values ... and "
      "sends the y value that holds a corresponding position between the two neighboring y "
      "values', clamped at both ends here since Max describes only the bracketed case. Its result "
      "leaves as a float, the one type Max's reference does not state: every other message on that "
      "outlet reports a stored y, which is an int by construction, while interp alone reports a "
      "value that was never stored and a position between two values is fractional by definition — "
      "truncating it would quantise every curve the object exists to draw, and the loss is not "
      "recoverable downstream. 'next' walks the pointer forward, sending the y out outlet 0 and "
      "the "
      "difference from the previously reported x out outlet 1 — Max's 'calculates the difference "
      "between that x value and the value previously pointed to' — which makes outlet 1 the "
      "inter-onset interval of a time-stamped sequence; outlet 2 bangs when the walk runs off the "
      "end, and the pointer is left there rather than wrapped, unlike .coll's next, because this "
      "is "
      "a traversal of a function rather than a ring of entries. 'min' and 'max' send the smallest "
      "and largest stored y, 'find' sends every x holding a given y, 'dump' sends every pair in "
      "ascending x order with the x out outlet 1 before its y out outlet 0, 'set' stores pairs, "
      "'delete' removes one by x alone or by an exact x,y match, 'goto' moves the pointer and "
      "'clear' empties the store. 'print' and bang are Max's Console diagnostics and are consumed "
      "silently, since neither sends anything out any outlet in Max either. dump and find capture "
      "their whole burst into a stack array under one acquisition of the guard and emit after "
      "releasing it — .bag's capture rather than .coll's per-item walk, and the sort is what "
      "forces "
      "it: .coll only ever appends, so re-reading its bounds each step is enough, while a "
      "re-entrant set here can insert in front of the cursor and shift every later pair, which "
      "would emit one pair twice and skip another. The store is bounded at 256 pairs allocated "
      "whole at construction and never resized, with .value's non-blocking guard around it — "
      ".coll's model, and the reason issue #497's 'mutation on the control thread only, "
      "Calculate() "
      "reads a published snapshot' is not what is built: a copy-on-write GraphState publish "
      "assumes "
      "the writer is the control thread, while a .funbuff is written by whichever thread its "
      "message arrived on and in-patcher delivery dispatches on the audio thread, so a set from a "
      "rendering graph would have to allocate a replacement table there. A store past the capacity "
      "is refused whole and silently and a loser of the guard drops rather than waiting. "
      "Calculate() does nothing. Persistence is Max's third answer, neither .coll's always nor "
      ".bag's never: the contents ride the DumpState / RestoreState hook only when 'embed' is on, "
      "Max's 'the word embed, followed by a non-zero number, causes the funbuff data to be stored "
      "inside the patcher', and the flag is written alongside them so a reloaded object still "
      "knows "
      "to embed itself. With embed off nothing is written at all. The pointer is not saved, being "
      "run-time position as .coll's pointer and .bucket's freeze are. Not ported: read and write "
      "(file I/O on a path that may be the audio thread) and with them the filename argument, "
      "which "
      "is accepted and ignored so a patch brought across from Max still builds; copy, cut, paste, "
      "select and undo, which are an editor selection plus a global clipboard shared between every "
      "funbuff in the application, the same shared-name context .coll and .bag deferred; and "
      "interptab, which interpolates through a Max table object by name, the patcher having "
      "neither.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "x", kXDoc, "at most 256 pairs");
  INLET_DOC(1, "y", kYDoc, "any int");
  OUTLET_DOC(0, "y", kYOutDoc, "any");
  OUTLET_DOC(1, "x", kXOutDoc, "any int");
  OUTLET_DOC(2, "end", kEndDoc, "");
  PARAM_DOC("filename", "",
            "Max's filename argument, 'specifies a file to read into funbuff when the patch "
            "loads'. Accepted and ignored: reading and writing funbuff files is file I/O on a path "
            "that may be the audio thread and is deliberately out of scope, so the argument is "
            "held only so that a '.funbuff mydata' brought across from Max still builds the object "
            "it names and so the argument a patch author typed survives a save unchanged. Nothing "
            "is derived from it, and the contents of the object come from its inlets or, when "
            "'embed' is on, from the saved patch itself.",
            "any symbol");
}

// ─── the store ────────────────────────────────────────────────────────────────

std::size_t gFunbuff::LowerBound(int x) const {
  // Binary search rather than a scan: a full table costs eight comparisons
  // instead of 256 on a path that may be the audio thread, and every question
  // the object answers goes through here.
  std::size_t low = 0;
  std::size_t high = count;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    if (pairs[mid].x < x) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

std::size_t gFunbuff::LowerBoundF(float x) const {
  std::size_t low = 0;
  std::size_t high = count;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    if ((float)pairs[mid].x < x) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

bool gFunbuff::StorePair(int x, int y) {
  const std::size_t at = LowerBound(x);

  // An x already stored is replaced rather than duplicated: this is a function,
  // so one x has exactly one y.
  if (at < count && pairs[at].x == x) {
    pairs[at].y = y;
    return true;
  }

  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (count >= MAX_PAIRS) return false;

  for (std::size_t i = count; i > at; i--) {
    pairs[i] = pairs[i - 1];
  }
  pairs[at].x = x;
  pairs[at].y = y;
  count++;
  return true;
}

void gFunbuff::EraseAt(std::size_t position) {
  if (position >= count) return;
  for (std::size_t i = position; i + 1 < count; i++) {
    pairs[i] = pairs[i + 1];
  }
  count--;
}

// ─── inlet 0's two jobs ───────────────────────────────────────────────────────

void gFunbuff::ApplyX(int x, YSE::THREAD thread) {
  bool send = false;
  int y = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    // Max: "if a y value was previously received in the right inlet, the pair is
    // stored". Consumed by this one x and not by the next.
    if (hasPendingY) {
      hasPendingY = false;
      StorePair(x, pendingY);
      return;
    }

    const std::size_t at = LowerBound(x);
    if (at < count && pairs[at].x == x) {
      y = pairs[at].y;
      send = true;
    } else if (at > 0) {
      // Max: "funbuff uses the closest x value which is less than the number
      // received, and sends out the corresponding y value" — a step function,
      // not an interpolation. `interp` is the message that interpolates.
      y = pairs[at - 1].y;
      send = true;
    }
    // at == 0 with no exact match means every stored x is greater than this one,
    // so there is no lesser neighbour to fall back to and nothing is sent.
  }

  // Sent with the guard released: holding it across a synchronous fan-out would
  // make a patch that wires an outlet back into this object's inlet lose its own
  // message to the guard it is still holding.
  if (send) outputs[0].SendInt(y, thread);
}

void gFunbuff::Next(YSE::THREAD thread) {
  bool found = false;
  int x = 0;
  int y = 0;
  int difference = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    // Max: "finds the x value pointed to by the pointer (or, if the pointer
    // points to a number not yet stored as an x value, to the next greater x
    // value)".
    const std::size_t at = LowerBound(pointer);
    if (at < count) {
      x = pairs[at].x;
      y = pairs[at].y;
      // Max: "calculates the difference between that x value and the value
      // previously pointed to". Both lastX and pointer start at 0, so the first
      // next on a fresh object reports the first x itself — the delta from the
      // origin a sequencer wants before its first event.
      difference = x - lastX;
      lastX = x;
      pointer = x + 1;
      found = true;
    }
    // No stored x at or past the pointer: the traversal has run off the end. The
    // pointer is left where it is rather than wrapped, so a repeated next keeps
    // banging and a patch restarts with goto.
  }

  if (!found) {
    outputs[2].SendBang(thread);
    return;
  }
  // Outlet 1 before outlet 0: Max's right-to-left order, the order .coll and
  // .trigger already fire in, so a patch has the interval before the value.
  outputs[1].SendInt(difference, thread);
  outputs[0].SendInt(y, thread);
}

void gFunbuff::Interp(float x, YSE::THREAD thread) {
  bool send = false;
  float y = 0.f;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    if (count == 0) return;

    // The query x is used at full precision rather than truncated: `interp 5.5`
    // is a different question from `interp 5`, and Max's "converted to int" is a
    // rule about the inlets rather than about this message's argument.
    if (x <= (float)pairs[0].x) {
      // Clamped: Max describes only the bracketed case, and clamping is what
      // every breakpoint curve does at its ends.
      y = (float)pairs[0].y;
    } else if (x >= (float)pairs[count - 1].x) {
      y = (float)pairs[count - 1].y;
    } else {
      // Strictly inside both ends, so there is a pair on each side. The search
      // has to be the float one: casting the query to an int first would
      // truncate toward zero, which picks the neighbour on the wrong side of a
      // fractional negative x — with pairs at -10 and -2, an `interp -2.5` would
      // bracket [-2, 0] and extrapolate rather than interpolate.
      const std::size_t hi = LowerBoundF(x);
      // The two clamps above leave 1 <= hi <= count - 1: x is strictly greater
      // than the first x, so hi is past 0, and strictly less than the last, so
      // hi is in range.
      const Pair& right = pairs[hi];
      const Pair& left = pairs[hi - 1];
      const float span = (float)right.x - (float)left.x;
      // span is never 0: the table holds one y per x, so two live pairs never
      // share an x. An x landing exactly on `right` needs no special case — the
      // formula gives position 1 and so exactly right.y.
      const float position = (x - (float)left.x) / span;
      y = (float)left.y + position * ((float)right.y - (float)left.y);
    }
    send = true;
  }

  // A float, which is the one type Max's reference does not state — see the
  // class documentation for why truncating would defeat the message.
  if (send) outputs[0].SendFloat(y, thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gFunbuff::HandleCommand(const char* word, std::size_t length, const std::string& message,
                             std::size_t argOffset, YSE::THREAD thread) {
  const char* text = message.c_str();
  const std::size_t total = message.size();

  if (TokenIs(word, length, "set", 3)) {
    // Max: "the word set, followed by one or more space-separated pairs of
    // numbers, stores each pair as x,y pair".
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    std::size_t cursor = argOffset;
    float x = 0.f;
    float y = 0.f;
    while (NextNumber(text, total, cursor, x) && NextNumber(text, total, cursor, y)) {
      // A trailing odd number is not a pair and is dropped rather than paired
      // with a zero it was never given.
      StorePair(ExprToInt(x), ExprToInt(y));
    }
    return true;
  }

  if (TokenIs(word, length, "delete", 6)) {
    // Max: "the word delete, followed by two numbers, looks for such an x,y pair
    // in funbuff, and deletes it if it exists"; and "if delete is followed by
    // only one number, only the x value is sought, and deleted if it is present".
    std::size_t cursor = argOffset;
    float x = 0.f;
    if (!NextNumber(text, total, cursor, x)) return true;
    float y = 0.f;
    const bool haveY = NextNumber(text, total, cursor, y);

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    const std::size_t at = LowerBound(ExprToInt(x));
    if (at >= count || pairs[at].x != ExprToInt(x)) return true;
    // With a y given, both have to match: Max looks for "such an x,y pair".
    if (haveY && pairs[at].y != ExprToInt(y)) return true;
    EraseAt(at);
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Only `count` says which pairs are live, so clearing is O(1) and the
    // storage a later store needs is still there. The traversal goes back to the
    // start with it, since a pointer into a function that no longer exists is
    // not a position.
    count = 0;
    pointer = 0;
    lastX = 0;
    return true;
  }

  if (TokenIs(word, length, "dump", 4)) {
    // Captured whole under one acquisition of the guard and emitted after
    // releasing it. .bag's capture rather than .coll's per-item walk, and the
    // sort is what forces it: .coll only ever appends, so re-reading its bounds
    // each step is enough, while a re-entrant `set` here can insert in front of
    // the cursor and shift every later pair up by one — which would emit one
    // pair twice and skip another.
    Pair burst[MAX_PAIRS];
    std::size_t written = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      // Max: "sends all the stored pairs out the middle and left outlets ... in
      // ascending order based on the x value", which is the order the table is
      // already in.
      for (std::size_t i = 0; i < count; i++) {
        burst[i] = pairs[i];
      }
      written = count;
    }
    for (std::size_t i = 0; i < written; i++) {
      // The x before its y: Max's right-to-left outlet order.
      outputs[1].SendInt(burst[i].x, thread);
      outputs[0].SendInt(burst[i].y, thread);
    }
    return true;
  }

  if (TokenIs(word, length, "goto", 4)) {
    std::size_t cursor = argOffset;
    float x = 0.f;
    if (!NextNumber(text, total, cursor, x)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Max: "sets a pointer to the x value (index) specified by the number". Both
    // the pointer and the reference x move, so the next `next` measures its
    // difference from where the patch jumped to rather than from wherever the
    // playhead had wandered.
    pointer = ExprToInt(x);
    lastX = pointer;
    return true;
  }

  if (TokenIs(word, length, "next", 4)) {
    Next(thread);
    return true;
  }

  if (TokenIs(word, length, "min", 3) || TokenIs(word, length, "max", 3)) {
    // Max: "sends the minimum / maximum y value currently stored in the funbuff
    // out the left outlet" — the y column, not the x, and no argument.
    const bool wantMin = TokenIs(word, length, "min", 3);
    bool have = false;
    int best = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      for (std::size_t i = 0; i < count; i++) {
        if (!have || (wantMin ? pairs[i].y < best : pairs[i].y > best)) {
          best = pairs[i].y;
          have = true;
        }
      }
    }
    // An empty store has no minimum, so nothing is sent rather than a 0 that
    // would read downstream as a stored value.
    if (have) outputs[0].SendInt(best, thread);
    return true;
  }

  if (TokenIs(word, length, "find", 4)) {
    // Max: "the word find, followed by a number, will output (out the left
    // outlet) all x values (indexes) whose y value is equal to the number
    // indicated" — the one message that sends x values out outlet 0.
    std::size_t cursor = argOffset;
    float y = 0.f;
    if (!NextNumber(text, total, cursor, y)) return true;
    const int wanted = ExprToInt(y);

    int burst[MAX_PAIRS];
    std::size_t written = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      for (std::size_t i = 0; i < count; i++) {
        if (pairs[i].y == wanted) {
          burst[written] = pairs[i].x;
          written++;
        }
      }
    }
    // Captured before sending, for `dump`'s reason: a re-entrant store would
    // otherwise shift the pairs this walk has not reached yet.
    for (std::size_t i = 0; i < written; i++) {
      outputs[0].SendInt(burst[i], thread);
    }
    return true;
  }

  if (TokenIs(word, length, "interp", 6)) {
    std::size_t cursor = argOffset;
    float x = 0.f;
    if (!NextNumber(text, total, cursor, x)) return true;
    Interp(x, thread);
    return true;
  }

  if (TokenIs(word, length, "embed", 5)) {
    // Max: "the word embed, followed by a non-zero number, causes the funbuff
    // data to be stored inside the patcher". Off until a patch asks, which is
    // why this object's persistence is neither .coll's always nor .bag's never.
    std::size_t cursor = argOffset;
    float flag = 0.f;
    if (!NextNumber(text, total, cursor, flag)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    embed = ExprToInt(flag) != 0;
    return true;
  }

  // Max's Console diagnostics. Consumed rather than left to fall through because
  // Max dispatches on the selector, so a funbuff in Max cannot read `print` as
  // data either — and neither message sends anything out any outlet in Max, so
  // consuming them silently reproduces Max's observable behaviour exactly.
  if (TokenIs(word, length, "print", 5)) return true;
  if (TokenIs(word, length, "bang", 4)) return true;

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

INT_IN(IntIn) {
  if (inlet == 1) {
    // Cold: it arms the next x and sends nothing.
    storeGuard guard(busy);
    if (!guard.Held()) return;
    pendingY = value;
    hasPendingY = true;
    return;
  }
  ApplyX(value, thread);
}

FLOAT_IN(FloatIn) {
  // Max's float method is "converted to int", on both inlets — which is why the
  // stored function is an integer function.
  const int truncated = ExprToInt(value);
  if (inlet == 1) {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    pendingY = truncated;
    hasPendingY = true;
    return;
  }
  ApplyX(truncated, thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first. None of the reserved words is a number, so nothing this
  // inlet legitimately carries as data can collide with one.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  float x = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, x)) return;

  // Max's ordinary right-to-left list distribution: the second item behaves as
  // though it had been sent to inlet 1 and the first as though it had been sent
  // to inlet 0, so `60 100` stores the pair (60, 100). Anything past the second
  // item is ignored — a funbuff pair is two numbers.
  std::size_t cursor = end;
  float y = 0.f;
  if (NextNumber(text, length, cursor, y)) {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    pendingY = ExprToInt(y);
    hasPendingY = true;
  }

  ApplyX(ExprToInt(x), thread);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gFunbuff::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard
  // is still taken, because a message may be arriving from a rendering graph
  // while the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max's embed is what decides whether there is anything to write at all. With
  // it off nothing is written and the serialised object is byte for byte what it
  // would have been without the state hook, which is the contract that hook was
  // added under.
  if (!embed) return;

  // Written even when the store is empty, so a reloaded object still knows to
  // embed itself the next time the patch is saved.
  json["embed"] = true;
  for (std::size_t i = 0; i < count; i++) {
    nlohmann::json pair;
    pair["x"] = pairs[i].x;
    pair["y"] = pairs[i].y;
    json["pairs"].push_back(pair);
  }
}

void gFunbuff::RestoreState(const nlohmann::json::value_type& json) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  embed = json.value("embed", false);

  count = 0;
  pointer = 0;
  lastX = 0;

  const auto stored = json.find("pairs");
  if (stored == json.end() || !stored->is_array()) return;
  for (const auto& pair : *stored) {
    // Written back through the same store the inlet uses, so a saved patch whose
    // pairs are out of order — or that someone edited by hand — still comes back
    // sorted, which everything else here depends on.
    StorePair(pair.value("x", 0), pair.value("y", 0));
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gFunbuff::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

int gFunbuff::XAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (position >= count) return 0;
  return pairs[position].x;
}

int gFunbuff::YAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (position >= count) return 0;
  return pairs[position].y;
}

int gFunbuff::Lookup(int x, bool& found) const {
  found = false;
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  const std::size_t at = LowerBound(x);
  if (at >= count || pairs[at].x != x) return 0;
  found = true;
  return pairs[at].y;
}

bool gFunbuff::Embeds() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return embed;
}

#undef className
