#include "signalBusPort.h"
#include "../../implementations/logImplementation.h"
#include "../../internal/rtLogQueue.h"
#include "../pObject.h"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cstring>
#include <thread>

using namespace YSE::PATCHER;

namespace {
  // A line for the log from any thread: the RT-safe queue, never the heap.
  void Post(const char* text) {
    YSE::INTERNAL::RtLog().post(text, std::strlen(text));
  }

  // The word starting at `at` (separators skipped), as [begin, end).
  void Word(const std::string& message, std::size_t& at, std::size_t& begin, std::size_t& end) {
    const std::size_t length = message.size();
    while (at < length && IsSelectorSeparator(message[at]))
      at++;
    begin = at;
    while (at < length && !IsSelectorSeparator(message[at]))
      at++;
    end = at;
  }
} // namespace

void signalBusPort::Reset(std::string& name, const char* type) {
  if (name.size() > MAX_NAME) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + type + " name \"" + name +
                                          "\" is longer than " + std::to_string(MAX_NAME) +
                                          " characters; ignored");
    name.clear();
  }
  current = name;
  waiting.store(false, std::memory_order_release);
}

std::shared_ptr<YSE::DSP::signalBus> signalBusPort::Bus(const pObject* parent) const {
  if (parent == nullptr || current.empty()) return nullptr;
  const auto* p = static_cast<const patcherImplementation*>(parent);
  return DSP::signalBus::acquire(p->ScopedAddress(current));
}

bool signalBusPort::FirstWordIs(const std::string& message, const char* word) {
  std::size_t at = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  Word(message, at, begin, end);
  const std::size_t length = std::strlen(word);
  return end - begin == length && std::memcmp(message.data() + begin, word, length) == 0;
}

bool signalBusPort::RequestSet(pObject& owner, const std::string& message) {
  // Walked in place: no copy, no allocation on whichever thread sends.
  std::size_t at = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  Word(message, at, begin, end);
  if (end - begin != 3 || std::memcmp(message.data() + begin, "set", 3) != 0) return false;
  Word(message, at, begin, end);
  const std::size_t length = end - begin;
  if (length > MAX_NAME) {
    Post("patcher: a set name is longer than 63 characters; ignored");
    return true;
  }
  if (writing.exchange(true, std::memory_order_acquire)) {
    Post("patcher: a set arrived while another was being stored; dropped");
    return true;
  }
  if (length > 0) std::memcpy(pending, message.data() + begin, length);
  pending[length] = '\0';
  pendingLength = length;
  waiting.store(true, std::memory_order_relaxed);
  writing.store(false, std::memory_order_release);

  // Outside a patcher there is nothing to sync: a set does nothing.
  if (owner.Parent() != nullptr) {
    static_cast<patcherImplementation*>(owner.Parent())->RequestSignalBusSync();
  }
  return true;
}

bool signalBusPort::TakePending() {
  // The writer holds the flag for one short copy, never across a wait.
  while (writing.exchange(true, std::memory_order_acquire))
    std::this_thread::yield();
  bool changed = false;
  if (waiting.load(std::memory_order_relaxed)) {
    waiting.store(false, std::memory_order_relaxed);
    if (current.size() != pendingLength ||
        std::memcmp(current.data(), pending, pendingLength) != 0) {
      current.assign(pending, pendingLength);
      changed = true;
    }
  }
  writing.store(false, std::memory_order_release);
  return changed;
}
