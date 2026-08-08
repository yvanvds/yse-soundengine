#include "gForward.h"
#include "../../implementations/logImplementation.h"
#include "../../internal/global.h"
#include "../../internal/namedBus.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;
#define className gForward

namespace {
  using YSE::INTERNAL::Bus;
  using YSE::INTERNAL::BusValue;

  // The bus is owned by INTERNAL::Global() between init() and close(); skip
  // publishing outside that window so tests instantiating a patcher without
  // first calling `System::init()` keep working through the local path — the
  // gSend rule, and for the same reason.
  inline bool busAvailable() {
    return YSE::INTERNAL::Global().isActive();
  }

  // The bus truncates a published name at kNameCapacity while the in-patcher
  // PassData path does not, so the two would disagree about where an over-long
  // destination points. gForward refuses such a name outright; this keeps the
  // limit it refuses by pinned to the limit that motivates it.
  static_assert(gForward::MAX_NAME_LENGTH == YSE::INTERNAL::NamedBus::kNameCapacity,
                "gForward::MAX_NAME_LENGTH must track NamedBus::kNameCapacity");
} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  // The destination inlet. A list is a symbol; an int is a computed name,
  // spelled the way a creation argument spells one. Bang and float are declined
  // rather than swallowed — see the class documentation.
  ADD_IN_1;
  REG_INT_IN(SetDestinationInt);
  REG_LIST_IN(SetDestinationList);

  ADD_PARAM(destination);
  ADD_PARAM(globalOnly);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The one allocation each would otherwise need on the message path. Reserved
  // here, on the control thread, for the longest destination the object will
  // accept; RefreshBusAddress() grows the address buffer again once the patcher
  // name it is prefixed with is known.
  destination.reserve(MAX_NAME_LENGTH);
  busAddress.reserve(MAX_NAME_LENGTH);

  ADD_DESCRIPTION(
      "Named send endpoint whose destination is chosen at runtime. Delivers incoming values "
      "exactly as .s does — to every .r in the patcher whose dataName matches, and on the global "
      "bus as \"<patcherName>.<destination>\" — but takes the destination name from its right "
      "inlet, so a patch can re-aim it with a message instead of being rewired. Sends nothing "
      "until a destination is set.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Value inlet — accepts bang / int / float / list. Forwarded verbatim to the current "
            "destination; no word in it is reserved.",
            "");
  INLET_DOC(1, "destination",
            "Sets the destination for subsequent values. A list is read as a name (first token "
            "only); an int is read as the name it spells. A name longer than 63 characters is "
            "refused and the previous destination kept.",
            "any identifier, at most 63 characters");
  PARAM_DOC("destination", "",
            "Destination name to start from. Empty means the object sends nothing until its right "
            "inlet supplies one.",
            "any identifier, at most 63 characters");
  PARAM_DOC("globalOnly", "0",
            "When 1, skip in-patcher delivery and publish only to the global bus.", "0 or 1");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes `SetParams("")` a real
// reset rather than a no-op.
PARM_CLEAR() {
  destination.clear();
  globalOnly = 0;
  RefreshBusAddress();
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford to say why it refused. An over-long creation argument leaves the
  // object with no destination at all rather than a truncated one that would
  // reach a different receiver on each of the two delivery paths.
  if (destination.size() > MAX_NAME_LENGTH) {
    INTERNAL::LogImpl().emit(E_ERROR, "patcher: .forward destination \"" + destination +
                                          "\" is longer than 63 characters; ignored");
    destination.clear();
  }
  RefreshBusAddress();
}

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors the PassData calls below.
void gForward::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  RefreshBusAddress();
}

void gForward::RefreshBusAddress() {
  if (parent == nullptr) {
    busPrefix.clear();
  } else {
    auto* p = static_cast<patcherImplementation*>(parent);
    busPrefix = p->Name() + ".";
  }
  // Control thread. Size the address for the longest destination this object
  // will ever accept under the current prefix, so SetDestination() only ever
  // refills it.
  busAddress.reserve(busPrefix.size() + MAX_NAME_LENGTH);
  busAddress.assign(busPrefix);
  busAddress.append(destination);
}

void gForward::SetDestination(const char* text, std::size_t length) {
  // Take the first whitespace-separated token: a name with a space in it can
  // never match a .r, whose creation argument is tokenised the same way.
  std::size_t begin = 0;
  while (begin < length && (text[begin] == ' ' || text[begin] == '\t'))
    begin++;
  std::size_t end = begin;
  while (end < length && text[end] != ' ' && text[end] != '\t')
    end++;

  const std::size_t size = end - begin;
  // Nothing but whitespace names nothing, and an over-long name would address
  // one receiver locally and a truncated one on the bus. Keep the destination
  // the object already had in both cases — silently, since this may be the
  // audio thread.
  if (size == 0 || size > MAX_NAME_LENGTH) return;

  destination.assign(text + begin, size);
  busAddress.assign(busPrefix);
  busAddress.append(destination);
}

LIST_IN(SetDestinationList) {
  SetDestination(value.c_str(), value.size());
}

INT_IN(SetDestinationInt) {
  // WriteInt is the patcher's only int-to-text writer, so the name this spells
  // is the name `.r <n>` was given by its own creation argument. Stack buffer,
  // no allocation, no locale.
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(value, digits);
  SetDestination(digits, written);
}

BANG_IN(SetBangValue) {
  if (!Addressable()) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassBang(destination, thread);
  }
  // CallingThread, not the tag: a `.forward` reached from a deferred delivery
  // carries T_GUI on the audio callback, where the bus's parked-publish path
  // takes a mutex and allocates. gSend.cpp carries the full note (issue #690).
  if (busAvailable() && p->CallingThread(thread) == YSE::T_GUI) {
    // Bang on the bus is a monostate publish — only delivered on T_GUI.
    Bus().publish(busAddress, BusValue{}, thread);
  }
}

INT_IN(SetIntValue) {
  if (!Addressable()) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, destination, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress, BusValue{value}, p->CallingThread(thread));
  }
}

FLOAT_IN(SetFloatValue) {
  if (!Addressable()) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, destination, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress, BusValue{value}, p->CallingThread(thread));
  }
}

LIST_IN(SetListValue) {
  if (!Addressable()) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, destination, thread);
  }
  // Built only when it can be delivered: the variant copy is the allocation.
  if (busAvailable() && p->CallingThread(thread) == YSE::T_GUI) {
    Bus().publish(busAddress, BusValue{value}, thread);
  }
}
