#include "gSpray.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gSpray

namespace {

  // Max's sentinel first element: "If the first number is -1, the remaining
  // elements of the list will be repeated to all outlets."
  constexpr int BROADCAST_INDEX = -1;

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kInletDoc[] =
      "A list whose first number is an outlet index and whose remaining elements are the values to "
      "send: the first goes out that outlet, the next out the outlet to its right, and so on, all "
      "of them right to left. An index of -1 repeats the remaining elements around every outlet. "
      "Elements with no outlet are dropped and symbols are ignored in place, costing their outlet "
      "a "
      "send without moving the elements after them. 'offset <n>' shifts every incoming index by n. "
      "A bare int, float or bang is not accepted — Max's spray requires a list — and a message "
      "whose first token is a symbol is ignored.";

  constexpr char kOutletDoc[] =
      "The element of an incoming list that addressed this outlet, as the kind it was spelled as, "
      "or the whole remainder of the list in list mode. Outlets fire right to left, so a "
      "downstream "
      "collector sees the last element of a burst before the first.";

} // namespace

CONSTRUCT() {
  // Every outlet is built by ShapePorts(), so a saved `.spray 4` comes back with
  // four of them. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous outlet
  // count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has. Only `list` is registered: Max's `int` method exists
  // solely to post "spray requires a list", and there is no `float` or `bang`
  // method at all. A message handler must not log, so rather than a handler that
  // swallows the message the inlet declines the type outright — GetAcceptedTypes
  // then reports the object's real contract.
  ADD_IN_0;
  REG_LIST_IN(SetList);
  inputs.back().SetDoc("in", kInletDoc, "list");

  // Max: "If there is no argument present, the object has two outlets." Also the
  // shape ClearParams() restores.
  ShapePorts();

  ADD_DESCRIPTION(
      "Distributes the values of a list to numbered outlets — Max's spray, which 'accepts lists as "
      "input, where the first number is taken as the outlet number, and one or more values that "
      "follow are sent out that outlet and those to its right, in right-to-left order'. A "
      "demultiplexer whose destination is data rather than structure: the patch sends 2 60 and 60 "
      "leaves by outlet 2, so one stream can address a bank of destinations without a box per "
      "destination. It is the inverse of Max's funnel, which tags a value with the inlet it "
      "arrived "
      "on: a funnel into a spray of the same width is a numbered bus that survives being carried "
      "through one cord. It is not .gate, which routes by an index arriving at a separate inlet "
      "and "
      "therefore in a separate message, so the patch must set the destination before sending the "
      "value and every ordering mistake in between sends a value to last time's outlet; here the "
      "pair arrives together and cannot be desynchronised, which is the whole reason to reach for "
      "it. It is not .cycle, which picks the outlet itself by rotating, nor .route or .sel, which "
      "pick by matching against constants fixed at creation; this is the one that takes the "
      "destination as a number at run time. And unlike .trigger, which fans one value out every "
      "outlet as copies, a list here may well reach several outlets but each receives a different "
      "element — it spreads rather than copies, and unlike .unpack it starts wherever the message "
      "says rather than always at outlet 0. Element k of the remainder goes out outlet index+k; an "
      "element whose outlet does not exist is dropped rather than wrapped, .gate's and .cycle's "
      "discipline, since an index the object has no outlet for is a miscount that silent folding "
      "would hide. The burst is captured before the first send and the outlets fire right to left, "
      "Max's universal order and one this object's own description states outright: the send path "
      "is synchronous, so a patch looping an outlet back into the inlet re-enters inside the Send "
      "and the interrupted burst must go on emitting the values it started with. A first number of "
      "-1 is Max's broadcast, 'the remaining elements of the list will be repeated to all "
      "outlets', "
      "tested on the number as written and before the offset is applied so that an offset -1 "
      "cannot "
      "silently turn a broadcast into an ordinary index; with several values 'repeated' is taken "
      "as "
      "repeated around the outlets, so -1 0 1 sets alternate outlets and a single value still "
      "reaches all of them. The second creation argument and the 'offset' message are one value "
      "described from opposite ends — Max's 'an offset for the numbering of the outlets' and 'the "
      "output of the object ... shifted to the left' both mean the outlet reached is index minus "
      "offset — and it exists because the numbers a patch sprays usually start where their source "
      "starts, MIDI channels at 1 or a controller's buttons at 64, where the alternative is a .- "
      "in "
      "front of every stream. 'offset' is run-time state rather than a parameter, as .cycle's "
      "thresh and .bucket's R2L are, so a saved patch carries the creation argument. The third "
      "creation argument is Max's list mode, in which 'an entire list is output through the "
      "indicated outlet ... instead of unpacking the list and sending the individual elements out "
      "sequential outlets' — what a patch wants when the elements belong together, since a note "
      "and "
      "its velocity are one message and splitting them across two outlets makes two facts out of "
      "one. Each element leaves as the kind it was spelled as, so 0 1 2 does not arrive downstream "
      "as 1. 2.; Max's 'the list may contain only ints or floats; symbols will be ignored' is "
      "honoured in place, a symbol costing its outlet a send while the elements after it keep "
      "their "
      "positions, because closing the gap would silently rewrite the destination of every later "
      "element in a positional object. A bare int, float or bang is not accepted at all, Max's 'an "
      "error-message ... stating that spray requires a list', and neither is a one-element list or "
      "a message whose first token is a symbol. At most 256 outlets, built once before the object "
      "is published; Calculate() does nothing, and no message path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  PARAM_DOC("outlets", "2",
            "Max's argument list, in Max's order. The first whole number is how many outlets to "
            "build, clamped to 1-256 and defaulting to 2. The second is the offset added to the "
            "outlet numbering, so an index of that value reaches outlet 0; it defaults to 0 and is "
            "the starting value of the 'offset' message. A third whole number set to non-zero is "
            "Max's list mode: the whole remainder of a message goes out the one indicated outlet "
            "instead of one element per outlet. A float is truncated; anything that is not a whole "
            "finite number is ignored and leaves the defaults in place.",
            "1-256, optional offset and list mode");
}

void gSpray::ShapePorts() {
  // Rebuilt rather than resized: the outlet *count* comes from the argument.
  // Safe because every caller runs before the object is wired or published —
  // the constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  int count = DEFAULT_PORTS;
  int requested = DEFAULT_PORTS;
  bool clamped = false;
  int read = 0;

  offset = 0;
  listMode = false;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    if (read == 0) {
      requested = ExprToInt(number);
      count = requested;
      if (count > MAX_PORTS) count = MAX_PORTS;
      if (count < MIN_PORTS) count = MIN_PORTS;
      clamped = (requested != count);
      read++;
      continue;
    }

    if (read == 1) {
      // Max: "The second argument sets an offset for the numbering of the
      // outlets." Not clamped — an offset larger than the outlet count is a
      // patch that has simply muted the object, which is a thing a patch may
      // legitimately mean, and the arithmetic is done in 64 bits so no value
      // here can overflow the index it is subtracted from.
      offset = ExprToInt(number);
      read++;
      continue;
    }

    // Max: "The third argument, if set to '1', sets the object to 'list mode.'"
    // Any non-zero value means on, the reading the second arguments of `.cycle`
    // and `.bucket` already take.
    listMode = (ExprToInt(number) != 0);
    break;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // OutletCount().
  if (clamped) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .spray outlet count " + IntText(requested) +
                                            " is outside " + IntText(MIN_PORTS) + "-" +
                                            IntText(MAX_PORTS) + "; clamped to " + IntText(count));
  }

  outputs.clear();
  for (int i = 0; i < count; i++) {
    // ANY rather than a fixed type: an outlet carries whichever kind the element
    // that addressed it was spelled as, and a list in list mode.
    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kOutletDoc, "any");
  }

  // The one allocation a list-mode send would otherwise need.
  listText.reserve(TEXT_CAPACITY);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous outlet
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

void gSpray::SendSlot(const Slot& slot, int outlet, YSE::THREAD thread) {
  if (slot.isFloat) {
    outputs[(std::size_t)outlet].SendFloat(slot.value, thread);
    return;
  }
  // Spelled as an int, so it leaves as an int atom. Through the range-checked
  // conversion rather than a cast: the token may have been wider than an int,
  // since ReadNumericToken only promised a finite float.
  outputs[(std::size_t)outlet].SendInt(ExprToInt(slot.value), thread);
}

void gSpray::Scatter(const std::string& text, std::size_t at, I64 start, YSE::THREAD thread) {
  const int count = (int)outputs.size();

  // On the stack, not in a member: a patch that loops an outlet back into the
  // inlet re-enters inside the send loop below, and a shared member buffer would
  // be overwritten by the inner burst before the outer one had finished with it.
  // Bounded by MAX_PORTS, so no allocation either way.
  Slot burst[MAX_PORTS];

  const std::size_t size = text.size();
  std::size_t i = at;
  I64 target = start;

  // Everything past the last outlet is dropped, so the walk can stop there.
  while (i < size && target < (I64)count) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t tokenStart = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    // A negative target is an element that fell off the left end — possible once
    // an offset is in play — and is dropped exactly as one past the right end is.
    if (target >= 0) {
      const char* token = text.c_str() + tokenStart;
      const std::size_t length = i - tokenStart;

      float number = 0.f;
      // Max: "The list may contain only ints or floats; symbols will be
      // ignored." Ignored *in place*: the outlet is spent and the elements after
      // it keep their positions, because closing the gap would move every later
      // element one outlet to the left in an object whose whole contract is
      // positional.
      if (ReadNumericToken(token, length, number)) {
        Slot& slot = burst[(std::size_t)target];
        slot.value = number;
        // The int-atom / float-atom test lives in pListArgs.h next to the reader
        // that agreed the token is a number, so an int element is not widened
        // into a float on its way through.
        slot.isFloat = TokenLooksLikeFloat(token, length);
        slot.filled = true;
      }
    }

    target++;
  }

  // Max's universal order, which this object's own description states outright:
  // "sent out that outlet and those to its right, in right-to-left order".
  for (int outlet = count - 1; outlet >= 0; outlet--) {
    const Slot& slot = burst[(std::size_t)outlet];
    if (slot.filled) SendSlot(slot, outlet, thread);
  }
}

void gSpray::Broadcast(const std::string& text, std::size_t at, YSE::THREAD thread) {
  const int count = (int)outputs.size();

  // The pattern to repeat, captured before the first send for the same
  // re-entrancy reason a scattered burst is.
  Slot pattern[MAX_PORTS];
  int found = 0;

  const std::size_t size = text.size();
  std::size_t i = at;

  // More elements than there are outlets can never be reached: outlet i takes
  // pattern[i % found], and i is below the outlet count, so once `found` has
  // passed that the modulo is the identity and the rest of the list is unused.
  while (i < size && found < MAX_PORTS) {
    while (i < size && (text[i] == ' ' || text[i] == '\t'))
      i++;
    if (i >= size) break;

    const std::size_t tokenStart = i;
    while (i < size && text[i] != ' ' && text[i] != '\t')
      i++;

    const char* token = text.c_str() + tokenStart;
    const std::size_t length = i - tokenStart;

    float number = 0.f;
    // A symbol here is skipped rather than spending a place: there is no outlet
    // *position* to spend, since a broadcast repeats a pattern rather than
    // placing elements.
    if (ReadNumericToken(token, length, number)) {
      pattern[(std::size_t)found].value = number;
      pattern[(std::size_t)found].isFloat = TokenLooksLikeFloat(token, length);
      pattern[(std::size_t)found].filled = true;
      found++;
    }
  }

  // Nothing numeric to repeat, so nothing is sent — the same answer a list of
  // symbols gets on the ordinary path.
  if (found == 0) return;

  for (int outlet = count - 1; outlet >= 0; outlet--)
    SendSlot(pattern[(std::size_t)(outlet % found)], outlet, thread);
}

void gSpray::SendWhole(const std::string& text, std::size_t at, I64 outlet, bool broadcast,
                       YSE::THREAD thread) {
  const int count = (int)outputs.size();

  // Trailing whitespace is not part of the list a downstream object should see.
  std::size_t end = text.size();
  while (end > at && (text[end - 1] == ' ' || text[end - 1] == '\t'))
    end--;
  const std::size_t length = end - at;

  if (broadcast) {
    for (int i = count - 1; i >= 0; i--) {
      // Refilled immediately before *each* send rather than once before the
      // loop: a re-entrant message arriving during send i would otherwise leave
      // sends i-1 and below carrying the loop's list instead of this one's. The
      // source is the caller's own string, which outlives the whole burst.
      listText.assign(text, at, length);
      outputs[(std::size_t)i].SendList(listText, thread);
    }
    return;
  }

  // Out of range is ignored, as it is on the ordinary path and as `.gate`
  // ignores a select index it has no outlet for.
  if (outlet < 0 || outlet >= (I64)count) return;

  listText.assign(text, at, length);
  outputs[(std::size_t)outlet].SendList(listText, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  std::size_t at = 0;
  if (MatchWord(value, "offset", 6, at)) {
    // Max: "The word offset followed by a number will offset the output of the
    // object by the number of outlets given shifted to the left (a negative
    // number will specify the number of outlets offset to the right)." Which is
    // the same value the second creation argument sets, described from the other
    // end. Run-time state: it does not write back to the parameters, so it does
    // not survive a save.
    int shift = 0;
    if (!ReadIntArg(value, at, shift)) return;
    offset = shift;
    return;
  }

  // The leading token, which Max's parser uses to tell a `list` from an
  // `anything` — the same question `.bondo`, `.cycle` and `.bucket` ask.
  const std::size_t size = value.size();
  std::size_t i = 0;
  while (i < size && (value[i] == ' ' || value[i] == '\t'))
    i++;
  const std::size_t start = i;
  while (i < size && value[i] != ' ' && value[i] != '\t')
    i++;

  float number = 0.f;
  // A symbol-leading message is Max's `anything`, which spray has no method for.
  if (i == start || !ReadNumericToken(value.c_str() + start, i - start, number)) return;

  while (i < size && (value[i] == ' ' || value[i] == '\t'))
    i++;

  // An index with nothing after it. Max's `int` method exists only to say that
  // "spray requires a list", and a one-element list is the same case: an outlet
  // with nothing to put in it.
  if (i >= size) return;

  const int index = ExprToInt(number);

  if (index == BROADCAST_INDEX) {
    // Tested on the index as written, *before* the offset: an `offset -1` would
    // otherwise turn every broadcast into an ordinary index and a patch would
    // lose it without a word.
    if (listMode) {
      SendWhole(value, i, 0, true, thread);
    } else {
      Broadcast(value, i, thread);
    }
    return;
  }

  // Max's two descriptions of the offset agree on this: the outlet reached is
  // the index minus it. In 64 bits, because both sides may be a saturated `int`
  // and their difference need not be one.
  const I64 target = (I64)index - (I64)offset;

  if (listMode) {
    // Max: "an entire list is output through the indicated outlet (with the
    // optional offset provided by the second object argument), instead of
    // unpacking the list and sending the individual elements out sequential
    // outlets."
    SendWhole(value, i, target, false, thread);
    return;
  }

  Scatter(value, i, target, thread);
}
