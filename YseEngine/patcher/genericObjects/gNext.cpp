#include "gNext.h"
#include "../pObjectList.hpp"
#include <cstdint>
#include <string>

using namespace YSE::PATCHER;

#define className gNext

CONSTRUCT() {
  // One inlet. Max gives bang, int and float the description "Performs the same
  // as anything", and anything is the method that runs the test, so all four
  // message kinds arrive at the same place and the payload never matters.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // Max, under Output: "Out left outlet: A bang is sent out if the current
  // message is not part of the same logical event as the previously received
  // message. Out right outlet: A bang is sent out if the current message is
  // part of the same logical event as the previously received message." Two
  // halves of one test, so exactly one of them fires per message.
  ADD_OUT_BANG;
  ADD_OUT_BANG;

  // No parameters at all. Max: "Arguments: None." There is no state a creation
  // argument could pre-declare — the object's memory is the previous message's
  // event, and an event that has not happened cannot be named.

  ADD_DESCRIPTION(
      "Tells the first message of a burst from the rest: a bang leaves the left outlet when a "
      "message starts a new logical event, and the right outlet when it belongs to the same event "
      "as the message before it.");
  ADD_CATEGORY(pCategory::CONTROL);
  INLET_DOC(0, "in",
            "Bang, int, float or list. Only which event it belongs to matters; every word is an "
            "ordinary message.",
            "any");
  OUTLET_DOC(0, "separated",
             "A bang when this message starts a new logical event. The first message ever always "
             "goes here.",
             "bang");
  OUTLET_DOC(1, "same", "A bang when this message belongs to the same event as the previous one.",
             "bang");
}

void gNext::Test(YSE::THREAD thread) {
  // The dispatch layer's answer to "which stimulus is being delivered right
  // now". Opaque and unique: it is only ever compared, never interpreted.
  const std::uint64_t event = CurrentMessageEvent();

  // Three conditions, and each one is load-bearing:
  //
  //  - `seen`: the first message ever has no previous message to share an event
  //    with, so it is separated. Max compares against "a previous message".
  //  - `event != 0`: 0 means no dispatch is in progress, which happens when a
  //    handler is called directly rather than through the inlet. Two such calls
  //    are two stimuli that happen to carry the same non-id, and folding them
  //    together would report a burst that never existed.
  //  - `event == lastEvent`: the actual test.
  const bool same = seen && event != 0 && event == lastEvent;

  // Settled before the send, as .onebang and .togedge settle theirs. The send
  // path is synchronous with no queue in between, so a patch that loops an
  // outlet back into this inlet re-enters here inside the SendBang below; with
  // the stores after the send it would find the object still describing the
  // previous message and report the loop as a fresh event every time.
  lastEvent = event;
  seen = true;
  separated = !same;

  // Max: exactly the two halves of one test, so one outlet and only one.
  outputs[same ? 1 : 0].SendBang(thread);
}

BANG_IN(SetBang) {
  if (inlet != 0) return;
  Test(thread);
}

INT_IN(SetInt) {
  // Max: "Performs the same as anything." The value is discarded, including a
  // 0 — nothing this object emits could carry it.
  (void)value;
  SetBang(inlet, thread);
}

FLOAT_IN(SetFloat) {
  (void)value;
  SetBang(inlet, thread);
}

LIST_IN(SetList) {
  // Max's `anything`, which is where the test lives. The text is never read:
  // there is no word this object knows, and no content that could change the
  // answer, so there is nothing to parse and nothing to allocate.
  (void)value;
  SetBang(inlet, thread);
}

GUI_VALUE() {
  // Which outlet the last message left by — the only thing an editor could
  // usefully show, and the only thing about this object a patch can observe
  // without wiring both outlets.
  return separated ? "1" : "0";
}
