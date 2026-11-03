#include "bufferBinding.h"
#include "../io/fileScheduler.h"
#include "../pObject.h"
#include "../pSelector.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>

using namespace YSE::PATCHER;
using YSE::PATCHER::detail::bindingMailbox;
using YSE::PATCHER::detail::bufferBindingCore;

// The handoff between a bind task and the thread that uses the binding
// (issue #1256, after ~cycle's in #1192). Shared by the object and the tasks
// it sends, which find it by id, so whichever lets go last frees it, and it
// frees every binding it still holds.
//
//   task  --incoming-->  Take()  --retired-->  task
//
// `current` is the taking thread's alone once the object is published.
// `incoming` is written by tasks (an unread binding is replaced, and freed by
// the task) and taken with one exchange. `retired` holds the binding Take()
// let go; Take() fills it only while it is empty and any task empties it, so
// the taking thread never frees a binding and never waits for one.
struct YSE::PATCHER::detail::bindingMailbox {
  bindingMailbox(std::uint64_t id, bindingOps ops) : id(id), ops(ops) {}
  ~bindingMailbox();
  bindingMailbox(const bindingMailbox&) = delete;
  bindingMailbox& operator=(const bindingMailbox&) = delete;
  bindingMailbox(bindingMailbox&&) = delete;
  bindingMailbox& operator=(bindingMailbox&&) = delete;

  static std::shared_ptr<bindingMailbox> Make(bindingOps ops);
  static std::shared_ptr<bindingMailbox> Find(std::uint64_t id);

  const std::uint64_t id;
  const bindingOps ops;
  void* current = nullptr;
  std::atomic<void*> incoming{nullptr};
  std::atomic<void*> retired{nullptr};
  // A release task is queued or running. Set by Take() before it asks, so the
  // task's clear always comes after it.
  std::atomic<bool> releaseQueued{false};
};

namespace {
  enum class BINDING_OP : std::uint8_t {
    BIND, // bind `name` and leave it in `incoming`
    RELEASE, // free the binding Take() let go
  };

  // The record a request becomes. Copied whole into a scheduler slot, so it
  // stays trivially copyable.
  struct bindingTask {
    std::uint64_t box = 0;
    BINDING_OP op = BINDING_OP::BIND;
    char name[fileScheduler::PATH_CAPACITY] = {}; // empty: unbind
  };

  // The process-wide mailboxes, by id. Weak: a mailbox lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct mailboxTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<bindingMailbox>> boxes;
    std::uint64_t next = 1;
  };

  mailboxTable& Mailboxes() {
    // Never destroyed, so a mailbox freed during static destruction still
    // finds its table.
    static mailboxTable* const instance =
        new mailboxTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  bool Submit(pObject& owner, std::uint64_t box, bindingTask& task);

  // The fileTaskFunction: background pool, never touches the object.
  bool RunTask(char* args, std::size_t argBytes) {
    static_assert(std::is_trivially_copyable<bindingTask>::value,
                  "a bindingTask is copied into a scheduler slot with memcpy");
    if (argBytes != sizeof(bindingTask)) return false;
    bindingTask task;
    std::memcpy(&task, args, sizeof(task));

    // By id, not through the object: it may have been deleted since.
    std::shared_ptr<bindingMailbox> found = bindingMailbox::Find(task.box);
    if (found == nullptr) return false;

    // Whatever Take() let go, whichever kind of task this is. The flag first:
    // a binding Take() retires after the exchange below asks again.
    found->releaseQueued.store(false, std::memory_order_release);
    void* retired = found->retired.exchange(nullptr, std::memory_order_acq_rel);
    if (retired != nullptr) found->ops.destroy(retired);
    if (task.op == BINDING_OP::RELEASE) return true;

    task.name[sizeof(task.name) - 1] = '\0';
    void* next = found->ops.create(
        task.name[0] != '\0' ? YSE::DSP::sampleBufferRegistry::acquire(task.name) : nullptr);
    // A binding nobody has taken yet is replaced: the last request wins. Tasks
    // run one at a time, so nothing else fills `incoming` meanwhile.
    void* unread = found->incoming.exchange(next, std::memory_order_acq_rel);
    if (unread != nullptr) found->ops.destroy(unread);
    return true;
  }

  bool Submit(pObject& owner, std::uint64_t box, bindingTask& task) {
    fileScheduler* io = owner.FileIO();
    if (io == nullptr) return false;
    task.box = box;
    return io->RequestTask(&owner, BUFFER_BINDING_TAG, &RunTask, &task, sizeof(task));
  }
} // namespace

std::shared_ptr<bindingMailbox> bindingMailbox::Make(bindingOps ops) {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  std::shared_ptr<bindingMailbox> box = std::make_shared<bindingMailbox>(t.next++, ops);
  t.boxes[box->id] = box;
  return box;
}

std::shared_ptr<bindingMailbox> bindingMailbox::Find(std::uint64_t id) {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  auto it = t.boxes.find(id);
  return it == t.boxes.end() ? nullptr : it->second.lock();
}

// Off the audio thread: the last holder is the object's destructor or a task.
bindingMailbox::~bindingMailbox() {
  if (current != nullptr) ops.destroy(current);
  void* waiting = incoming.load(std::memory_order_acquire);
  if (waiting != nullptr) ops.destroy(waiting);
  void* old = retired.load(std::memory_order_acquire);
  if (old != nullptr) ops.destroy(old);
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.boxes.erase(id);
}

// Control thread: the mailbox and the first binding exist before the object
// can be published.
bufferBindingCore::bufferBindingCore(bindingOps ops, void* first) : box(bindingMailbox::Make(ops)) {
  box->current = first;
}

bufferBindingCore::~bufferBindingCore() = default;

void* bufferBindingCore::Current() const {
  return box->current;
}

bool bufferBindingCore::ReleasePending() const {
  return box->retired.load(std::memory_order_acquire) != nullptr;
}

bool bufferBindingCore::Request(pObject& owner, const char* name, std::size_t length) {
  // The record lives on the stack and the scheduler copies it into a slot it
  // already owns: no allocation on the sending thread.
  bindingTask task;
  if (length >= sizeof(task.name)) return false;
  if (length > 0) std::memcpy(task.name, name, length);
  task.name[length] = '\0';
  return Submit(owner, box->id, task);
}

bool bufferBindingCore::RequestSet(pObject& owner, const std::string& message, bool& isSet) {
  // Walked in place: no copy, no allocation on whichever thread sends.
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t at = 0;
  while (at < length && IsSelectorSeparator(text[at]))
    at++;
  const std::size_t begin = at;
  while (at < length && !IsSelectorSeparator(text[at]))
    at++;
  isSet = at - begin == 3 && std::memcmp(text + begin, "set", 3) == 0;
  if (!isSet) return false;
  while (at < length && IsSelectorSeparator(text[at]))
    at++;
  const std::size_t nameBegin = at;
  while (at < length && !IsSelectorSeparator(text[at]))
    at++;
  return Request(owner, text + nameBegin, at - nameBegin);
}

// The taking thread: wait-free. Takes a waiting binding only while the
// retired slot is empty, so the old one always has somewhere to go.
bool bufferBindingCore::Take(pObject& owner) {
  bindingMailbox& b = *box;
  bool swapped = false;
  if (b.retired.load(std::memory_order_acquire) == nullptr &&
      b.incoming.load(std::memory_order_relaxed) != nullptr) {
    void* next = b.incoming.exchange(nullptr, std::memory_order_acq_rel);
    if (next != nullptr) {
      b.retired.store(b.current, std::memory_order_release);
      b.current = next;
      swapped = true;
    }
  }
  // Ask for the retired binding to be freed. A refused request (a full table,
  // or no patcher) is asked again by the next Take(); any bind task frees it
  // too.
  if (b.retired.load(std::memory_order_acquire) != nullptr &&
      !b.releaseQueued.exchange(true, std::memory_order_acq_rel)) {
    bindingTask release;
    release.op = BINDING_OP::RELEASE;
    if (!Submit(owner, b.id, release)) b.releaseQueued.store(false, std::memory_order_release);
  }
  return swapped;
}
