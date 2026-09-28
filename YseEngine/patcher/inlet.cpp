#include "inlet.h"
#include "outlet.h"
#include "pObject.h"
#include "graphState.h"
#include "pListArgs.h"
#include "pSelector.h"
#include "math/gExprEval.h"
#include <atomic>
#include <cstring>

using namespace YSE::PATCHER;

namespace {
  // Logical message events (issue #471), the patcher's stand-in for the thing
  // Max calls an "event": one mouse click, one key press, one MIDI event, one
  // tick of the scheduler, and everything that single stimulus goes on to
  // cause. See CurrentMessageEvent() in inlet.h for what the notion means and
  // why `.next` needs it.
  //
  // The whole mechanism is a nesting counter. Message delivery here is
  // synchronous and depth-first — an inlet calls the object's handler, which
  // calls outlet::Send*, which calls the next inlet, all on one stack — so
  // "caused by the same stimulus" is exactly "dispatched inside the outermost
  // dispatch". The outermost one takes a fresh id; everything below it reads
  // that same id.
  //
  // The counter has to sit on the **inlet** side. outlet.cpp's tSendDepth
  // already tracks nesting, but it is scoped to one send rather than to one
  // handler: a `.trigger b b` returns to depth 0 between its two outlets, so a
  // send-scoped counter would split one stimulus into two events — precisely
  // the case Max's reference names as one ("if you put bang, bang in a message
  // box, or use the uzi object to send out two bangs in a row, these bangs are
  // part of the same logical event"). Reusing tSendDepth itself would also
  // halve the recursion ceiling #236 relies on, so this is a second, separate
  // counter.
  //
  // RT-safety, on the same terms as that send-depth guard: the depth and the
  // current id are thread_local and constant-initialised, so touching them is a
  // plain TLS load/store — no allocation, no lock, no syscall — which is what
  // makes this safe on the audio-thread dispatch path. The one shared write is
  // a relaxed atomic increment, and it happens once per *outermost* dispatch
  // rather than once per message: a 1 kHz `.metro` costs a thousand of them a
  // second, spread across whatever thread the stimulus arrived on.
  thread_local unsigned int tDispatchDepth = 0;
  thread_local std::uint64_t tEventId = 0;

  // Globally unique ids, so an object fed from two threads cannot see two
  // unrelated events wearing the same number. Starts at 1 because 0 is reserved
  // for "no dispatch in progress" — the value CurrentMessageEvent() reports to
  // a handler called directly rather than through an inlet, which must not read
  // as an event other messages can belong to.
  std::atomic<std::uint64_t> sNextEventId{1};

} // namespace

std::uint64_t YSE::PATCHER::CurrentMessageEvent() {
  return tEventId;
}

// RAII event scope. Opening the outermost one on this thread starts a new
// logical event; nested ones leave the current id alone, which is what makes
// an object's whole fan-out one event. Public (inlet.h) since #628: the
// deferred-message scheduler opens one around each delivery so a deferred send
// carries a proper event id; the inlet setters below are the other users.
messageEventScope::messageEventScope() {
  if (tDispatchDepth++ == 0) {
    tEventId = sNextEventId.fetch_add(1, std::memory_order_relaxed);
  }
}

messageEventScope::~messageEventScope() {
  // Cleared on the way out, so "no dispatch in progress" is 0 rather than
  // the id of whatever ran last. Without this a handler called directly —
  // outside any inlet — would inherit a stale id and read as part of an
  // event that has already finished.
  if (--tDispatchDepth == 0) {
    tEventId = 0;
  }
}

inlet::inlet(pObject* obj, bool active, int position)
  : obj(obj),
    dspReady(false),
    active(active),
    position(position),
    onInt(nullptr),
    onBang(nullptr),
    onFloat(nullptr),
    onList(nullptr),
    onBuffer(nullptr),
    dspConnection(nullptr) {}

inlet::~inlet() {
  // Delegated for symmetry with ~outlet (issue #537), not because this end was
  // the bug. The walk here was sound as written: `outlet::Disconnect` only
  // erases from the outlet's own list and never calls back into this one, so
  // nothing mutated the vector under the loop. It was the *asymmetry* that hid
  // the outlet side's fault — two hand-rolled walks that look alike, one of
  // which is unsound because its callee does call back. Both ends now run the
  // one version that takes the list away before touching a peer.
  UnwireFromPeers();
}

void inlet::RegisterInt(intFunc f) {
  onInt = f;
}

void inlet::RegisterBang(voidFunc f) {
  onBang = f;
}

void inlet::RegisterFloat(floatFunc f) {
  onFloat = f;
}

void inlet::RegisterList(listFunc f) {
  onList = f;
}

void inlet::RegisterBuffer(bufferFunc f) {
  onBuffer = f;
}

void inlet::SetInt(int value, YSE::THREAD thread) {
  if (onInt) {
    // Everything this message goes on to cause is dispatched inside the
    // handler below, so the scope covers the whole stimulus. See the top of
    // this file. Opened only once a handler is known to exist: an inlet that
    // does not take this message type cannot start a cascade.
    messageEventScope event;
    onInt(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetBang(YSE::THREAD thread) {
  if (onBang) {
    messageEventScope event;
    onBang(position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetFloat(float value, YSE::THREAD thread) {
  if (onFloat) {
    messageEventScope event;
    onFloat(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetList(const std::string& value, YSE::THREAD thread) {
  if (onList) {
    messageEventScope event;
    onList(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetBuffer(YSE::DSP::buffer* buffer, YSE::THREAD thread) {
  // Deliberately *not* an event scope. A buffer is the DSP path, and no object
  // that reads logical events accepts one, so scoping it would only add a
  // counter to the hottest path in the patcher to describe a grouping nothing
  // can observe. A control message emitted from a DSP handler still opens its
  // own event through the setters above, which is the honest answer for a
  // stimulus this patcher has no scheduler tick to attribute it to.
  if (onBuffer) {
    onBuffer(buffer, position, thread);
    dspReady = true;
    if (obj->IsDSPObject() && thread == T_GUI) return;
    obj->CalculateIfReady(thread);
  }
}

// A message box's text, delivered the way Max delivers one (issue #933).
//
// `.m` sends through outlet::SendMessage, and until #933 this handed the text
// straight to pObject::SetMessage — a method only seven objects implement —
// and then calculated a hot inlet whether or not the object had taken it. So
// `.m 60` into `.mtof` fired `.mtof` with the note it already held.
//
// The text is now read as the typed message it spells, by the same rules
// SendAtom / SendAtoms apply everywhere else in the patcher (pAtomList.h):
//
//   - one numeric token      -> an int or a float, by its spelling;
//   - the single word `bang` -> a bang;
//   - a list whose first token is a number -> the list;
//   - text that starts with any other word -> Max's "anything": the object's
//     word-command channel (SetMessage) when it has one, otherwise the list.
//
// A typed message the inlet has no handler for still reaches an object that
// implements SetMessage, so the seven objects that relied on this path keep
// every message they took before (a `.m` storing `.m 60`'s text, say). Anything
// else nobody takes is dropped, like a typed message to an inlet without that
// handler — no calculate on a message the object ignored.
//
// RT-safe: a walk of the characters, one ReadNumericToken (a stack copy and a
// strtof), and the typed setters, which pass `message` on by reference. No
// allocation, no lock.
void inlet::SetMessage(const std::string& message, YSE::THREAD thread, float value) {
  const char* text = message.c_str();
  const std::size_t length = message.size();

  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  std::size_t rest = end;
  while (rest < length && IsSelectorSeparator(text[rest]))
    rest++;

  const std::size_t tokenLength = end - begin;
  const bool single = tokenLength > 0 && rest == length;
  float number = 0.f;
  const bool numeric = ReadNumericToken(text + begin, tokenLength, number);

  if (numeric) {
    if (single) {
      if (TokenLooksLikeFloat(text + begin, tokenLength)) {
        if (onFloat) {
          SetFloat(number, thread);
          return;
        }
      } else if (onInt) {
        SetInt(ExprToInt(number), thread);
        return;
      }
    } else if (onList) {
      SetList(message, thread);
      return;
    }
  } else if (single && tokenLength == 4 && std::memcmp(text + begin, "bang", 4) == 0) {
    if (onBang) {
      SetBang(thread);
      return;
    }
  } else if (!obj->HandlesMessages()) {
    // Starts with a word the object has no command channel for: the list.
    if (tokenLength > 0 && onList) SetList(message, thread);
    return;
  }

  if (!obj->HandlesMessages()) return;

  messageEventScope event;
  obj->SetMessage(message, value);
  if (active) {
    if (obj->IsDSPObject() && thread == T_GUI) return;
    obj->CalculateIfReady(thread);
  }
}

bool inlet::WaitingForDSP() const {
  // Whether this inlet has an active buffer input comes from the pinned
  // snapshot on the thread rendering the patcher's block (audio-thread path),
  // else — including a control thread sending while a block is in flight,
  // issue #962 — from the live ``dspConnection`` (control-thread / standalone
  // path). See #226.
  const GraphState* graph = obj ? obj->CurrentBlockGraph() : nullptr;
  bool hasDsp;
  if (graph != nullptr && graphId >= 0 &&
      static_cast<size_t>(graphId) < graph->inletHasDsp.size()) {
    hasDsp = graph->inletHasDsp[graphId] != 0;
  } else {
    hasDsp = (dspConnection != nullptr);
  }
  if (!hasDsp) return false;
  return !dspReady;
}

void inlet::UnwireFromPeers() {
  // Detach from the buffer input and every control-edge source. Clear our own
  // lists first so the peers' Disconnect(this) calls stay consistent.
  outlet* dsp = dspConnection;
  dspConnection = nullptr;
  std::vector<outlet*> conns;
  conns.swap(connections);
  if (dsp != nullptr) dsp->Disconnect(this);
  for (unsigned int i = 0; i < conns.size(); i++) {
    conns[i]->Disconnect(this);
  }
}

bool inlet::Connect(outlet* out) {
  if (out->Type() == OUT_TYPE::BUFFER) {
    if (dspConnection == nullptr) {
      dspConnection = out;
      return true;
    } else
      return false;
  } else {
    for (unsigned int i = 0; i < connections.size(); i++) {
      if (connections[i] == out) return false;
    }
    connections.push_back(out);
    return true;
  }
}

void inlet::Disconnect(outlet* out) {
  if (dspConnection == out) {
    dspConnection->Disconnect(this);
    dspConnection = nullptr;
    return;
  }
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == out) {
      connections[i]->Disconnect(this);
      connections.erase(connections.begin() + i);
    }
  }
}

bool inlet::AcceptsDSP() const {
  if (onBuffer) return true;
  return false;
}

bool inlet::HasActiveDSPConnection() const {
  return dspConnection != nullptr;
}

int inlet::GetObjectID() {
  return obj->GetID();
}

int inlet::GetPosition() {
  return position;
}

void inlet::SetDoc(const std::string& label, const std::string& doc, const std::string& range) {
  docLabel = label;
  docDescription = doc;
  docRange = range;
}

unsigned int inlet::GetAcceptedTypes() const {
  unsigned int mask = IT_NONE;
  if (onBuffer) mask |= IT_BUFFER;
  if (onFloat) mask |= IT_FLOAT;
  if (onInt) mask |= IT_INT;
  if (onBang) mask |= IT_BANG;
  if (onList) mask |= IT_LIST;
  return mask;
}