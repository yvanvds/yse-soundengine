#include "gFunction.h"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cmath>

using namespace YSE::PATCHER;

#define className gFunction

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp logs).
  std::string IntText(int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    return std::string(digits, written);
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

  // The next token that reads as a number, advancing `from` past it. Tokens
  // that are not numbers are skipped rather than ending the walk —
  // ExprParseFloatList's policy, and the family's.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // How many numbers the whole message holds — what tells a two-number add
  // from a whole state. Counted by walking the text a second time rather than
  // into a buffer: a whole state of 1024 points is 3072 numbers and may arrive
  // down a cord on the audio thread.
  std::size_t CountNumbers(const char* text, std::size_t length) {
    std::size_t cursor = 0;
    std::size_t numbers = 0;
    float number = 0.f;
    while (NextNumber(text, length, cursor, number))
      numbers++;
    return numbers;
  }

  // Whether the `length` characters at `text` are exactly `word`. Compared in
  // place for the reason the tokens are walked in place.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "The function, and the only inlet. An int or a float is taken as an x and answers with the "
      "interpolated y out outlet 0 - Max's 'outputs a corresponding Y value ... produced by linear "
      "floating-point interpolation', shaped by the arriving point's curve here; an x outside the "
      "stored span answers with the nearest end's y, and an empty function answers nothing. A bang "
      "sends the whole function out outlet 1 as one ramp list of <target> <time> pairs, the "
      "breakpoint grammar .line and .bline read. A list of two numbers '<x> <y>' adds a breakpoint "
      "with a linear (0) curve, replacing the point already at that exact x - a function holds one "
      "y per x; a list of 3N numbers is the whole state, '<x> <y> <curve>' per point, the exact "
      "string GetGuiValue() produces (issue #551's round trip): the store is cleared and refilled "
      "through the same sorted, clamped path. 'set <index> <x> <y> [<curve>]' rewrites the point "
      "at that ascending-x position and re-sorts, keeping the point's curve when none is given - "
      "Max's split between his three- and four-element point messages; 'setcurve <index> <curve>' "
      "re-curves one point; 'nth <index>' answers that point's y out outlet 0; 'clear' empties the "
      "store and 'clear <index>' removes one point; 'dump' sends every point out outlet 2. Writes "
      "are silent - outlet 1 is a trigger, not a mirror, and a control that replayed its envelope "
      "on every edit would restart the .line it feeds exactly while a patch is being drawn into; "
      "Max's function is silent on edit too. Every x is folded to at least 0 (Max's domain starts "
      "at 0) and a NaN is dropped; every y is clamped into the minimum-maximum bounds; every "
      "curve is clamped into -1 to 1. A point past the capacity is refused whole and silently, "
      "since this inlet may be the audio thread. Not here: sustain / autosustain / next (the "
      "note-driven envelope player is the engine's envelope generators' job), fix (mouse "
      "protection without a mouse), setdomain and the domain / range attributes (bounds are "
      "creation parameters in this family), outputmode, mode (the curve is simply always "
      "available), and Max's bare index-edit lists, whose three-number form would collide with a "
      "one-point whole state - the 'set' keyword spells the same edit unambiguously.";

  constexpr char kYOutDoc[] =
      "The interpolated y - the answer to an int or float x on the inlet, and to 'nth <index>'. "
      "Always a float: the y between two breakpoints is fractional by definition, and truncating "
      "it would quantise every curve the object exists to draw. A query between two points is "
      "shaped by the right-hand point's curve: t^((1+c)/(1-c)) over the segment's normalised "
      "position, so a positive curve leaves the starting value slowly and moves late, a negative "
      "one moves early and lands slowly, and +/-1 are honest steps. An x at or outside either end "
      "answers that end's stored y, which is what every envelope does at its ends. Nothing is "
      "sent when the store is empty, when an 'nth' index names no point, or when the store was "
      "busy on another thread - the family's rule that an object with no answer stays quiet.";

  constexpr char kRampOutDoc[] =
      "The whole function as one ramp list, on a bang: 'y0 0 y1 (x1-x0) y2 (x2-x1) ...' - one "
      "<target> <time> pair per breakpoint, which is exactly the breakpoint grammar .line and "
      ".bline read. The first pair's time is 0, so the consumer jumps to the first y and ramps "
      "through the rest; the absolute x of the first point is dropped, Max's default (his "
      "outputmode attribute is not ported). Max emits an odd-length list with a bare leading y0 "
      "because line~ reads a leading singleton as a jump; .line reads pairs, so the jump is "
      "spelled as the zero-time pair and the observable ramp is the same. Curves are flattened "
      "to their linear segments here - the patcher has no curve~ to speak triples to; the curved "
      "shape lives in the query path. An empty function sends nothing.";

  constexpr char kDumpOutDoc[] =
      "Every breakpoint as its own list, '<index> <x> <y> <curve>', ascending by x, in answer to "
      "'dump' - Max's dump outlet made headless. The burst is captured whole under one "
      "acquisition of the store's guard and emitted after releasing it, so a re-entrant write "
      "cannot shift the sorted table under the walk and the dump is a consistent snapshot. "
      "Four numbers per line on purpose: a triple wired back into inlet 0 would read as a "
      "one-point whole state, while four numbers address nothing and pass through harmlessly. "
      "Silent otherwise, and silent for an empty store.";

} // namespace

CONSTRUCT() {
  // One inlet, hot, Max's left inlet: queries, the bang, points and commands.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_FLOAT; // 0: the interpolated y
  ADD_OUT_LIST; // 1: the ramp list, on a bang
  ADD_OUT_LIST; // 2: the dump

  // The store is sized by the creation arguments, so a saved `.function 256 0
  // 127` comes back 256 points wide bounded to MIDI velocities. The clear
  // callback is what makes SetParams("") return the object to the no-argument
  // shape; registering both makes ParamsNeedRebuild() true, so a live re-parse
  // replaces the object rather than reallocating under the audio thread.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  ShapeStore();

  ADD_DESCRIPTION(
      "A breakpoint function editor as one control - Max's function, 'draw or store a set of x, y "
      "points as floating-point numbers'. A bounded, sorted store of (x, y, curve) breakpoints "
      "that answers any x with the interpolation between its neighbours and bangs out the whole "
      "envelope as a ramp list .line and .bline consume - every hand-drawn envelope, velocity "
      "curve, filter sweep and automation shape is authored in one of these, and the engine's "
      "fixed-shape ADSR envelopes are the special case it generalises past. Issue #561 asks for "
      "the storage decision against .funbuff up front: the two keep separate stores on purpose. "
      ".funbuff is an integer function whose floor lookup is a step and whose exact-int search is "
      "load-bearing; this is a float function with per-point curvature whose lookup is the "
      "interpolation .funbuff keeps behind a separate message - one store would either quantise "
      "these curves or break that exactness, so what they share is the invariant (a bounded array "
      "sorted by its own key, one y per x) and the guard model, not code. An int or float on the "
      "inlet answers with the interpolated y out outlet 0, shaped by the arriving point's curve: "
      "t^((1+c)/(1-c)), positive leaving the start slowly, negative moving early, +/-1 honest "
      "steps, 0 linear - Max gates curves behind his mode attribute and speaks them to curve~, "
      "and with no attribute surface and no curve~ here the curve is simply always available. A "
      "bang sends the whole function out outlet 1 as 'y0 0 y1 (x1-x0) ...', one <target> <time> "
      "pair per point - exactly the breakpoint grammar .line and .bline read, with the jump to "
      "the first y spelled as a zero-time pair where Max's odd-length list carries a bare leading "
      "y0 for line~. A two-number list adds a point, a 3N-number list is the whole state (issue "
      "#551's round trip, the exact string GetGuiValue() produces), 'set <index> <x> <y> "
      "[<curve>]' rewrites one point keeping its curve when none is given, 'setcurve' re-curves "
      "one, 'nth' answers one point's y, 'clear [<index>]' empties or removes, and 'dump' sends "
      "every point as '<index> <x> <y> <curve>' out outlet 2. Writes are silent, deliberately "
      "unlike .multislider and .matrixctrl: their outlet mirrors their state, while outlet 1 here "
      "is a trigger, and a control that replayed its envelope on every edit would restart the "
      ".line it feeds exactly while a patch is being drawn into - Max's function is silent on "
      "edit too, and the GUI protocol leaves emission per object. A GUI cell is one breakpoint "
      "'<x> <y> <curve>'; an empty function's bulk read is the word 'clear', the one string that "
      "both round-trips and keeps a zero-token list from being a destructive write. The "
      "breakpoints persist through DumpState / RestoreState unconditionally - Max's function is "
      "a UI object whose points save with the patch, .coll's always-rule rather than .funbuff's "
      "opt-in embed - and restore runs through the same sorted, clamped store the inlet uses. "
      "The store is allocated exactly once at SetParams time from the points capacity argument "
      "(.matrixctrl's arrangement; a live re-parse replaces the object through the graph swap), "
      "no message path allocates, locks or blocks, sends happen with the non-blocking guard "
      "released, and a point past the capacity is refused whole and silently. Not ported: "
      "sustain / autosustain / next (the note-driven envelope player), fix, setdomain and the "
      "domain / range attributes (bounds are creation parameters in this family), outputmode, "
      "mode, Max's bare index-edit lists (the 'set' keyword spells the edit unambiguously), and "
      "everything about drawing and the mouse.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "function", kInDoc, "x >= 0, y in minimum-maximum, curve -1 to 1");
  OUTLET_DOC(0, "y", kYOutDoc, "minimum-maximum");
  OUTLET_DOC(1, "ramp", kRampOutDoc, "target time pairs");
  OUTLET_DOC(2, "dump", kDumpOutDoc, "index x y curve");
  PARAM_DOC(
      "points", "128",
      "The store, in one breath: how many breakpoints the function can hold, then the lowest and "
      "highest y a point may carry. The capacity is clamped to 2-1024 and defaults to 128; it is "
      "a creation parameter because the storage is allocated exactly once, at SetParams time, on "
      "the control thread - a live SetParams therefore replaces the object rather than resizing "
      "it under the audio thread. The bounds default to 0-1, Max's range attribute's default, "
      "and are taken as an ordered pair so a reversed argument still bounds against the right "
      "two numbers; every stored y is clamped into them on the way in. A '.function 256 0 127' "
      "is a velocity curve at full width. A capacity that is not a whole finite number leaves "
      "the default in place. The breakpoints themselves are not parameters - they are contents, "
      "and they ride the saved patch through the object's state instead, unconditionally, "
      "because Max's function is a UI object whose points save with the patch.",
      "2-1024, optional minimum, optional maximum");
}

// ─── the store ────────────────────────────────────────────────────────────────

void gFunction::ShapeStore() {
  // Rebuilt rather than resized: the capacity comes from the arguments and the
  // buffers have to agree with it. Safe because every caller runs before the
  // object is wired or published — the constructor and the two parameter
  // callbacks; a *live* SetParams never reaches here on a published object,
  // since registering the callbacks makes ParamsNeedRebuild() true and #234
  // replaces the object instead.
  int requested = DEFAULT_POINTS;
  int wanted = DEFAULT_POINTS;
  minimum = DEFAULT_MINIMUM;
  maximum = DEFAULT_MAXIMUM;
  int read = 0;

  for (const std::string& token : creationArgs) {
    if (read >= 3) break;

    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList
    // would read `5abc` as 5 and fold `1e999` to 0, and neither answers "is
    // this creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    if (read == 0) {
      wanted = ExprToInt(number);
      requested = wanted;
      if (requested > MAX_POINTS) requested = MAX_POINTS;
      if (requested < MIN_POINTS) requested = MIN_POINTS;
    } else if (read == 1) {
      // The bounds are taken as written; Bound() orders them, so a reversed
      // pair still bounds against the right two numbers.
      minimum = number;
    } else {
      maximum = number;
    }
    read++;
  }

  // The control thread, before the object is wired or published — the one
  // place a clamp can be *said* rather than merely observed through Capacity().
  if (read > 0 && wanted != requested) {
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: .function point capacity ") +
                                            IntText(wanted) + " is outside " + IntText(MIN_POINTS) +
                                            "-" + IntText(MAX_POINTS) + "; clamped to " +
                                            IntText(requested));
  }

  capacity = requested;
  count = 0;

  // The whole table and the dump's capture buffer, taken once here on the
  // control thread. Nothing on a message path ever resizes either.
  points = std::make_unique<Point[]>((std::size_t)capacity);
  scratch = std::make_unique<Point[]>((std::size_t)capacity);

  // The two allocations a send would otherwise need: the ramp list at its
  // worst case — every point a formatted y and a formatted time — and the dump
  // line at its fixed four numbers.
  rampText.reserve((std::size_t)capacity * 2u * ((std::size_t)kExprValueTextMax + 1u));
  lineText.reserve((std::size_t)FORMAT_INT_WIDTH + 3u * ((std::size_t)kExprValueTextMax + 1u));
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of SetParams(""): Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still holding the previous capacity.
  creationArgs.clear();
  ShapeStore();
}

PARM_PARSE() {
  ShapeStore();
}

float gFunction::Bound(float value) const {
  // The bounds are taken as an ordered pair, the family's rule, so a reversed
  // argument pair still bounds against the right two numbers. A NaN fails both
  // compares and is answered with the lower bound rather than stored.
  const float lowBound = minimum < maximum ? minimum : maximum;
  const float highBound = minimum < maximum ? maximum : minimum;
  if (!(value >= lowBound)) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

float gFunction::BoundCurve(float value) {
  // A NaN fails both compares below in a way no clamp can place, and is read
  // as 0 — linear — rather than folded onto an end it never asked for.
  if (!(value >= -1.f) && !(value <= 1.f)) return 0.f;
  if (value < -1.f) return -1.f;
  if (value > 1.f) return 1.f;
  return value;
}

bool gFunction::SanitizeX(float& x) {
  // Max's domain starts at 0, so a negative x folds onto the origin the way a
  // dragged point stops at the editor's left edge. A NaN has no place on the
  // axis at all and refuses the point.
  if (x >= 0.f) return true;
  if (x < 0.f) {
    x = 0.f;
    return true;
  }
  return false;
}

float gFunction::Shape(float t, float c) {
  if (c == 0.f) return t;
  // The limits are honest steps: at +1 the segment holds its start until the
  // end, at -1 it jumps at the start. Both keep the endpoints exact.
  if (c >= 1.f) return t >= 1.f ? 1.f : 0.f;
  if (c <= -1.f) return t > 0.f ? 1.f : 0.f;
  // t^e with e = (1+c)/(1-c): e grows without bound toward +1 and shrinks to 0
  // toward -1, and +c / -c are reciprocal exponents — reflections of one
  // another across the segment's diagonal. t is in [0, 1] and e is positive,
  // so the result is in [0, 1] and the interpolation below stays bounded.
  const float exponent = (1.f + c) / (1.f - c);
  return std::pow(t, exponent);
}

std::size_t gFunction::LowerBound(float x) const {
  // Binary search: a full store costs ten comparisons rather than 1024 on a
  // path that may be the audio thread, and every question goes through here.
  std::size_t low = 0;
  std::size_t high = count;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    if (points[mid].x < x) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

bool gFunction::StorePoint(float x, float y, float curve) {
  const std::size_t at = LowerBound(x);

  // An x already stored is replaced whole rather than duplicated: a function
  // holds one y per x, and the caller passed the whole point.
  if (at < count && points[at].x == x) {
    points[at].y = y;
    points[at].curve = curve;
    return true;
  }

  // Full: refused rather than grown, since growing would allocate on
  // whichever thread the message arrived on.
  if (count >= (std::size_t)capacity) return false;

  for (std::size_t i = count; i > at; i--) {
    points[i] = points[i - 1];
  }
  points[at].x = x;
  points[at].y = y;
  points[at].curve = curve;
  count++;
  return true;
}

void gFunction::EraseAt(std::size_t position) {
  if (position >= count) return;
  for (std::size_t i = position; i + 1 < count; i++) {
    points[i] = points[i + 1];
  }
  count--;
}

// ─── the query ────────────────────────────────────────────────────────────────

void gFunction::Query(float x, YSE::THREAD thread) {
  // A NaN query has no position on the axis and no honest answer.
  if (std::isnan(x)) return;

  bool send = false;
  float y = 0.f;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    if (count == 0) return;

    if (x <= points[0].x) {
      // Clamped at both ends: what every envelope does there, and the reading
      // that keeps the query total over its input.
      y = points[0].y;
    } else if (x >= points[count - 1].x) {
      y = points[count - 1].y;
    } else {
      // Strictly inside both ends, so there is a point on each side:
      // 1 <= hi <= count - 1.
      const std::size_t hi = LowerBound(x);
      const Point& right = points[hi];
      const Point& left = points[hi - 1];
      // Never 0: one y per x, so two live points never share an x.
      const float span = right.x - left.x;
      const float position = (x - left.x) / span;
      // The arriving point's curve shapes the segment — the first point's
      // curve is unused, which is Max's own bookkeeping.
      const float shaped = Shape(position, right.curve);
      y = left.y + shaped * (right.y - left.y);
    }
    send = true;
  }

  // Sent with the guard released: holding it across a synchronous fan-out
  // would make a patch that wires this outlet back into the inlet lose its
  // own message to the guard it is still holding.
  if (send) outputs[0].SendFloat(y, thread);
}

// ─── the ramp list and the dump ───────────────────────────────────────────────

void gFunction::EmitRamp(YSE::THREAD thread) {
  // An outlet wired back into inlet 0 is one cord away, and a re-entrant bang
  // rebuilding rampText mid-fan-out would hand the outer send another
  // message's text — `.matrixctrl`'s guard, for its reason.
  if (emitting) return;

  bool send = false;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    if (count == 0) return;

    // Built under the guard into memory reserved at construction — appends
    // only, so nothing here allocates — and sent after release.
    rampText.clear();
    char text[kExprValueTextMax];
    float previous = 0.f;
    for (std::size_t i = 0; i < count; i++) {
      if (i > 0) rampText.push_back(' ');
      int written = ExprFormatValue(ExprValue::Float(points[i].y), text, kExprValueTextMax);
      rampText.append(text, written > 0 ? (std::size_t)written : 0);
      rampText.push_back(' ');
      // The first pair's time is 0 — the jump to the first y, Max's default
      // reading in which the absolute x of the first point is dropped. Every
      // later time is the gap to the previous point, which the sort keeps
      // non-negative.
      const float delta = (i == 0) ? 0.f : points[i].x - previous;
      previous = points[i].x;
      written = ExprFormatValue(ExprValue::Float(delta), text, kExprValueTextMax);
      rampText.append(text, written > 0 ? (std::size_t)written : 0);
    }
    send = true;
  }

  if (!send) return;
  emitting = true;
  outputs[1].SendList(rampText, thread);
  emitting = false;
}

void gFunction::EmitDump(YSE::THREAD thread) {
  if (emitting) return;

  // Captured whole under one acquisition of the guard and emitted after
  // releasing it: a re-entrant `set` can insert in front of the cursor and
  // shift every later point, which would emit one point twice and skip
  // another (`.funbuff`'s lesson, forced by the sort).
  std::size_t taken = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    for (std::size_t i = 0; i < count; i++) {
      scratch[i] = points[i];
    }
    taken = count;
  }
  if (taken == 0) return;

  emitting = true;
  for (std::size_t i = 0; i < taken; i++) {
    // Refilled per point into memory reserved at construction, immediately
    // before its send.
    char digits[FORMAT_INT_WIDTH];
    std::size_t written = WriteInt((int)i, digits);
    lineText.assign(digits, written);
    lineText.push_back(' ');

    char text[kExprValueTextMax];
    int formatted = ExprFormatValue(ExprValue::Float(scratch[i].x), text, kExprValueTextMax);
    lineText.append(text, formatted > 0 ? (std::size_t)formatted : 0);
    lineText.push_back(' ');
    formatted = ExprFormatValue(ExprValue::Float(scratch[i].y), text, kExprValueTextMax);
    lineText.append(text, formatted > 0 ? (std::size_t)formatted : 0);
    lineText.push_back(' ');
    formatted = ExprFormatValue(ExprValue::Float(scratch[i].curve), text, kExprValueTextMax);
    lineText.append(text, formatted > 0 ? (std::size_t)formatted : 0);

    outputs[2].SendList(lineText, thread);
  }
  emitting = false;
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gFunction::HandleCommand(const char* word, std::size_t length, const std::string& message,
                              std::size_t argOffset, YSE::THREAD thread) {
  const char* text = message.c_str();
  const std::size_t total = message.size();

  if (TokenIs(word, length, "set", 3)) {
    // The protocol's cell write: `set <index> <x> <y> [<curve>]`. With no
    // curve given the point keeps the curve it had — Max's own split between
    // his three- and four-element point messages.
    std::size_t cursor = argOffset;
    float index = 0.f;
    float x = 0.f;
    float y = 0.f;
    float curve = 0.f;
    if (!NextNumber(text, total, cursor, index)) return true;
    if (!NextNumber(text, total, cursor, x)) return true;
    if (!NextNumber(text, total, cursor, y)) return true;
    const bool haveCurve = NextNumber(text, total, cursor, curve);
    // NaN fails the compare, so it never reaches ExprToInt; out of range is
    // dropped rather than folded onto a real point.
    if (!(index >= 0.f)) return true;
    if (!SanitizeX(x)) return true;

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    const std::size_t at = (std::size_t)ExprToInt(index);
    if (at >= count) return true;
    const float kept = haveCurve ? BoundCurve(curve) : points[at].curve;
    // Remove and re-insert so a moved x lands in sorted position; the insert
    // cannot fail, the erase having just freed a slot. A new x that collides
    // with another point replaces it — one y per x.
    EraseAt(at);
    StorePoint(x, Bound(y), kept);
    return true;
  }

  if (TokenIs(word, length, "setcurve", 8)) {
    std::size_t cursor = argOffset;
    float index = 0.f;
    float curve = 0.f;
    if (!NextNumber(text, total, cursor, index)) return true;
    if (!NextNumber(text, total, cursor, curve)) return true;
    if (!(index >= 0.f)) return true;

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    const std::size_t at = (std::size_t)ExprToInt(index);
    if (at >= count) return true;
    // The x does not move, so the sort is untouched and no re-insert is
    // needed.
    points[at].curve = BoundCurve(curve);
    return true;
  }

  if (TokenIs(word, length, "nth", 3)) {
    // Max: "outputs the Y value of the breakpoint" at the given index.
    std::size_t cursor = argOffset;
    float index = 0.f;
    if (!NextNumber(text, total, cursor, index)) return true;
    if (!(index >= 0.f)) return true;

    bool send = false;
    float y = 0.f;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      const std::size_t at = (std::size_t)ExprToInt(index);
      if (at >= count) return true;
      y = points[at].y;
      send = true;
    }
    if (send) outputs[0].SendFloat(y, thread);
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    // Bare, Max's "erases all existing breakpoints" — and the whole-state
    // spelling of an empty function, which is what makes GetGuiValue() round
    // trip at count 0. With an index, one point is removed.
    std::size_t cursor = argOffset;
    float index = 0.f;
    const bool haveIndex = NextNumber(text, total, cursor, index);

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (!haveIndex) {
      // Only `count` says which points are live, so clearing is O(1) and the
      // storage a later store needs is still there.
      count = 0;
      return true;
    }
    if (!(index >= 0.f)) return true;
    EraseAt((std::size_t)ExprToInt(index));
    return true;
  }

  if (TokenIs(word, length, "dump", 4)) {
    EmitDump(thread);
    return true;
  }

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Max: "triggers a list output of the current breakpoints ... formatted for
  // use by the line~ object" — the whole reason the object exists next to a
  // patcher that already has `.line`.
  if (inlet != 0) return;
  EmitRamp(thread);
}

INT_IN(IntIn) {
  if (inlet != 0) return;
  // Max: "the value is taken as an X value and outputs a corresponding Y
  // value". The answer is a float either way — see the outlet doc.
  Query((float)value, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  Query(value, thread);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the store because someone added a handler above.
  if (inlet != 0) return;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing — deliberately not a `clear`, or any
  // stray empty message down a cord would wipe an envelope.
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first. None of the reserved words is a number, so nothing this
  // inlet legitimately carries as data can collide with one.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // A bare list is one of two things, and its length says which: two numbers
  // are Max's list method — one breakpoint — and a multiple of three is the
  // whole state, the exact string GetGuiValue() produces. Two is not a
  // multiple of three, so the forms never collide; any other length
  // addresses nothing and is ignored. (Max's bare index-edit lists are not
  // ported: their three-number form is exactly a one-point whole state, and
  // the `set` keyword spells the same edit unambiguously.)
  const std::size_t numbers = CountNumbers(text, length);

  if (numbers == 2) {
    std::size_t cursor = 0;
    float x = 0.f;
    float y = 0.f;
    if (!NextNumber(text, length, cursor, x)) return;
    if (!NextNumber(text, length, cursor, y)) return;
    if (!SanitizeX(x)) return;

    storeGuard guard(busy);
    if (!guard.Held()) return;
    // Max's "list (2 values): adds new point". Linear until a curve is given;
    // replacing the point at an exact x replaces the whole point, curve
    // included — the caller passed the point it wants.
    StorePoint(x, Bound(y), 0.f);
    return;
  }

  if (numbers >= 3 && numbers % 3 == 0) {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    // A restore replaces: the state described is the state that results.
    // Refilled through the same sorted, clamped store every write uses, so a
    // foreign list still comes back ordered and bounded; a state longer than
    // the capacity keeps what fits, refused point by point past it.
    count = 0;
    std::size_t cursor = 0;
    float x = 0.f;
    float y = 0.f;
    float curve = 0.f;
    while (NextNumber(text, length, cursor, x) && NextNumber(text, length, cursor, y) &&
           NextNumber(text, length, cursor, curve)) {
      if (!SanitizeX(x)) continue;
      StorePoint(x, Bound(y), BoundCurve(curve));
    }
    return;
  }
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // A local rather than a send buffer: those belong to the message path, and
  // this runs on the host thread. One call, one allocation — the bulk read the
  // protocol asks for, and the form `.preset` stores.
  std::string out;
  out.reserve((std::size_t)capacity * 3u * ((std::size_t)kExprValueTextMax + 1u));

  storeGuard guard(busy);
  if (!guard.Held()) return out;

  // The whole-state spelling of an empty function is the message that makes a
  // function empty — the one string that both round-trips through inlet 0 and
  // keeps a zero-token list from being a destructive write.
  if (count == 0) {
    out = "clear";
    return out;
  }

  char text[kExprValueTextMax];
  for (std::size_t i = 0; i < count; i++) {
    if (i > 0) out.push_back(' ');
    int written = ExprFormatValue(ExprValue::Float(points[i].x), text, kExprValueTextMax);
    out.append(text, written > 0 ? (std::size_t)written : 0);
    out.push_back(' ');
    written = ExprFormatValue(ExprValue::Float(points[i].y), text, kExprValueTextMax);
    out.append(text, written > 0 ? (std::size_t)written : 0);
    out.push_back(' ');
    written = ExprFormatValue(ExprValue::Float(points[i].curve), text, kExprValueTextMax);
    out.append(text, written > 0 ? (std::size_t)written : 0);
  }
  return out;
}

GUI_VALUE_COUNT() {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return (unsigned int)count;
}

GUI_VALUE_AT() {
  storeGuard guard(busy);
  // Range-checked against the count read now, as the protocol requires: past
  // the end — or a store busy on another thread — is "", never the whole
  // state again and never an index.
  if (!guard.Held()) return std::string();
  if ((std::size_t)index >= count) return std::string();

  char text[kExprValueTextMax];
  std::string out;
  int written = ExprFormatValue(ExprValue::Float(points[index].x), text, kExprValueTextMax);
  out.append(text, written > 0 ? (std::size_t)written : 0);
  out.push_back(' ');
  written = ExprFormatValue(ExprValue::Float(points[index].y), text, kExprValueTextMax);
  out.append(text, written > 0 ? (std::size_t)written : 0);
  out.push_back(' ');
  written = ExprFormatValue(ExprValue::Float(points[index].curve), text, kExprValueTextMax);
  out.append(text, written > 0 ? (std::size_t)written : 0);
  return out;
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gFunction::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard
  // is still taken, because a message may be arriving from a rendering graph
  // while the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Unconditional, .coll's always-rule: Max's function is a UI object whose
  // points save with the patch. An empty function writes nothing, so its
  // serialised form is byte for byte what it would be without the hook.
  if (count == 0) return;

  for (std::size_t i = 0; i < count; i++) {
    nlohmann::json point;
    point["x"] = points[i].x;
    point["y"] = points[i].y;
    point["curve"] = points[i].curve;
    json["points"].push_back(point);
  }
}

void gFunction::RestoreState(const nlohmann::json::value_type& json) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  count = 0;

  const auto stored = json.find("points");
  if (stored == json.end() || !stored->is_array()) return;
  for (const auto& point : *stored) {
    // Written back through the same store the inlet uses, so a saved patch
    // whose points are out of order — or that someone edited by hand — still
    // comes back sorted, clamped and bounded, which everything else here
    // depends on.
    float x = point.value("x", 0.f);
    if (!SanitizeX(x)) continue;
    StorePoint(x, Bound(point.value("y", 0.f)), BoundCurve(point.value("curve", 0.f)));
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gFunction::PointCount() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

float gFunction::XAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0.f;
  if (position >= count) return 0.f;
  return points[position].x;
}

float gFunction::YAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0.f;
  if (position >= count) return 0.f;
  return points[position].y;
}

float gFunction::CurveAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0.f;
  if (position >= count) return 0.f;
  return points[position].curve;
}

#undef className
