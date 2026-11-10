#include "fileScheduler.h"
#include "../../headers/defines.hpp"
#include "../../internal/customFileReader.h"
#include "../../internal/global.h"
#include "../../io.hpp"
#include "../../utils/fileFunctions.hpp"
#include "../graphState.h"
#include "../inlet.h"
#include "../pObject.h"
#include "../patcher.hpp"
#include <chrono>
#include <cstring>
#include <exception>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace YSE::PATCHER;

namespace {

#ifdef YSE_WINDOWS
  constexpr char kPathDelim = '\\';
#else
  constexpr char kPathDelim = '/';
#endif

  // The prologue soundImplementation::create runs, in the one place a patcher
  // object can afford to run it: the background pool. A relative name is
  // resolved against the working directory exactly as a sound file's is, so a
  // patch and a sound loaded by the same host agree on where "data.txt" is.
  // Names handed to the host's virtual file system are opaque and passed
  // through untouched — only the host knows what they mean.
  std::string ResolvePath(const char* path) {
    std::string name(path);
    if (YSE::IO().getActive()) return name;
    // IsPathAbsolute indexes character 0 unconditionally on POSIX, so the empty
    // case is answered here rather than there. Arm() already refuses it.
    if (name.empty()) return name;
    if (YSE::IsPathAbsolute(name)) return name;
    return YSE::GetCurrentWorkingDirectory() + kPathDelim + name;
  }

  // ─── the host's path resolver (issue #1255) ────────────────────────────────

  // Every live scheduler, so Reply can find the slot a request id belongs to
  // without trusting a pointer the host held on to. Locked by the scheduler's
  // constructor and destructor (control thread, or the background pool for a
  // patcher a sound owned) and by Reply (the host's thread, or the background
  // pool inside a synchronous resolver). Never by the audio thread: requests
  // are posted on the pool, and the timeout check in DeliverComplete is one
  // CAS on the slot. A scheduler unregisters before it frees its slots, so a
  // reply that holds the lock is never inside a dying scheduler.
  struct schedulerRegistry {
    std::mutex mutex;
    std::vector<fileScheduler*> live;
  };

  schedulerRegistry& Registry() {
    // Never destroyed, so a scheduler freed during static destruction still
    // finds it.
    static schedulerRegistry* const instance =
        new schedulerRegistry(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  // Process-wide, so a reply can never reach another patcher's request. 0 is
  // "no request" and is skipped when the counter wraps.
  std::atomic<unsigned int> lastRequestId{0};

  unsigned int NextRequestId() {
    unsigned int id = 0;
    while (id == 0)
      id = lastRequestId.fetch_add(1, std::memory_order_relaxed) + 1;
    return id;
  }

  std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // The scheduler whose resolver this thread is inside, so a resolver that
  // re-installs from inside Resolve does not wait on itself.
  thread_local const fileScheduler* tResolvingFor = nullptr;

} // namespace

fileScheduler::fileScheduler() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a request needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
  schedulerRegistry& registry = Registry();
  const std::lock_guard<std::mutex> lock(registry.mutex);
  registry.live.push_back(this);
}

fileScheduler::~fileScheduler() {
  // Join every file job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob. That join runs in the *base* destructor —
  // after ~fileJob has already reset the vtable — so a worker that picks the
  // job up inside that window calls threadPoolJob::run(), which is pure
  // virtual (issue #706). Two more windows close with it: ~unique_ptr nulls
  // `entries_` before it destroys the elements, and ~Entry destroys the fields
  // a running job reads. Called from the patcher's destructor, where the
  // audio thread no longer renders the patch and Clear() has run, so nothing
  // can arm a new request. That destructor may be on the background pool's
  // own worker (a patcher a sound owned); join() help-runs there (#1257).
  //
  // Out of the reply registry first (issue #1255): a reply that already holds
  // the lock finishes first, and any job it re-armed is joined below; a later
  // one finds nothing and is refused. Requests still parked fail with the
  // patcher, unanswered.
  {
    schedulerRegistry& registry = Registry();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    for (auto it = registry.live.begin(); it != registry.live.end(); ++it) {
      if (*it == this) {
        registry.live.erase(it);
        break;
      }
    }
  }
  WaitIdle();
}

void fileScheduler::fileJob::run() {
  owner->RunSlot(*slot);
}

bool fileScheduler::Arm(pObject* target, int tag, FILE_OP op, const char* path,
                        std::size_t pathLength, const char* bytes, std::size_t byteCount,
                        fileTaskFunction task, filePathTaskFunction pathTask, PATH_ACCESS access) {
  // A task carries its own arguments and needs no path (issue #1039), unless
  // it opens a file by name (issue #1255).
  const bool pathRequired = op != FILE_OP::TASK || pathTask != nullptr;
  if (target == nullptr || pathLength >= PATH_CAPACITY ||
      (pathRequired && (path == nullptr || pathLength == 0)) ||
      (op == FILE_OP::TASK && task == nullptr && pathTask == nullptr)) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const bool carriesBytes = op != FILE_OP::READ;
  if (carriesBytes && (byteCount > BYTES_CAPACITY || (byteCount > 0 && bytes == nullptr))) {
    // Refused rather than truncated: half a collection is a different
    // collection, and the caller cannot tell a clipped file from a saved one.
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // One CAS attempt per slot, CAPACITY slots: a bounded, lock-free walk. A
  // failed CAS means another thread just claimed that slot — skip it rather
  // than retry, so no handler ever spins here.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_relaxed) != STATE_FREE) continue;
    std::uint32_t expected = STATE_FREE;
    if (!e.state.compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
      continue;
    }

    // The slot is ours; nothing else reads or writes its payload while it is
    // CLAIMED. Both copies are bounded memcpys into storage that already
    // exists — no allocation, which is what lets this run on the audio thread.
    if (pathLength > 0) std::memcpy(e.path, path, pathLength);
    e.path[pathLength] = '\0';
    if (carriesBytes && byteCount > 0) {
      std::memcpy(e.bytes, bytes, byteCount);
    }
    e.byteCount = carriesBytes ? (std::uint32_t)byteCount : 0;
    e.target = target;
    e.targetTag = target->InstanceTag();
    e.tag = tag;
    e.op = op;
    e.task = task;
    e.pathTask = pathTask;
    e.access = op == FILE_OP::WRITE ? PA_WRITE : (op == FILE_OP::READ ? PA_READ : access);
    // What the host's resolver is told about the requester (issue #1255).
    // Type() answers a literal and GetID() reads a field: nothing allocates.
    e.objectType = target->Type();
    e.objectId = target->GetID();
    e.resolved = false;
    e.ok = false;

    // Publish. The release pairs with the job's acquire CAS, which is what
    // makes every plain payload field above visible to the pool thread.
    e.state.store(STATE_ARMED, std::memory_order_release);
    INTERNAL::Global().addSlowJob(&e.job);

    // addSlowJob is a no-op on a pool that was never started or has been shut
    // down (an engine that is not running), and drops on a full background
    // ring. Either way nothing will ever move this slot, so complete it as a
    // failure rather than stranding it: the CAS can only win while the slot is
    // still ARMED, so a job that has already picked it up is unaffected.
    if (!e.job.isQueued()) {
      std::uint32_t armed = STATE_ARMED;
      e.state.compare_exchange_strong(armed, STATE_COMPLETE, std::memory_order_acq_rel,
                                      std::memory_order_relaxed);
    }
    return true;
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

bool fileScheduler::RequestRead(pObject* target, int tag, const char* path,
                                std::size_t pathLength) {
  return Arm(target, tag, FILE_OP::READ, path, pathLength, nullptr, 0);
}

bool fileScheduler::RequestWrite(pObject* target, int tag, const char* path, std::size_t pathLength,
                                 const char* bytes, std::size_t byteCount) {
  return Arm(target, tag, FILE_OP::WRITE, path, pathLength, bytes, byteCount);
}

bool fileScheduler::RequestTask(pObject* target, int tag, fileTaskFunction task, const void* args,
                                std::size_t argBytes) {
  return Arm(target, tag, FILE_OP::TASK, nullptr, 0, static_cast<const char*>(args), argBytes,
             task);
}

bool fileScheduler::RequestPathTask(pObject* target, int tag, PATH_ACCESS access, const char* path,
                                    std::size_t pathLength, filePathTaskFunction task,
                                    const void* args, std::size_t argBytes) {
  if (task == nullptr) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return Arm(target, tag, FILE_OP::TASK, path, pathLength, static_cast<const char*>(args), argBytes,
             nullptr, task, access);
}

void fileScheduler::RunSlot(Entry& e) {
  std::uint32_t armed = STATE_ARMED;
  if (!e.state.compare_exchange_strong(armed, STATE_RUNNING, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
    // Already completed as a failure by Arm() because the pool refused it.
    return;
  }
  // A request that names a file asks the host's resolver first (issue #1255).
  // Posted, the slot is parked and this run is over; a reply re-arms the job,
  // which comes back here with `resolved` set.
  const bool namesFile = e.op != FILE_OP::TASK || e.pathTask != nullptr;
  if (namesFile && !e.resolved && PostToResolver(e)) return;

  switch (e.op) {
  case FILE_OP::READ:
    e.ok = ReadSlot(e);
    break;
  case FILE_OP::WRITE:
    e.ok = WriteSlot(e);
    break;
  case FILE_OP::TASK:
    e.ok = e.pathTask != nullptr
               ? e.pathTask(e.bytes, e.byteCount, e.resolved ? e.resolvedPath : nullptr)
               : e.task(e.bytes, e.byteCount);
    break;
  }
  e.state.store(STATE_COMPLETE, std::memory_order_release);
}

bool fileScheduler::PostToResolver(Entry& e) {
  // The bracket SetResolver's grace period waits on: from before the load of
  // the resolver until after the call into it returns. seq_cst on both sides,
  // so either SetResolver sees this increment or this load sees its store.
  struct bracket {
    fileScheduler& owner;
    const fileScheduler* outer;
    explicit bracket(fileScheduler& o) : owner(o), outer(tResolvingFor) {
      owner.resolving_.fetch_add(1, std::memory_order_seq_cst);
      tResolvingFor = &owner;
    }
    ~bracket() {
      tResolvingFor = outer;
      owner.resolving_.fetch_sub(1, std::memory_order_seq_cst);
    }
    bracket(const bracket&) = delete;
    bracket& operator=(const bracket&) = delete;
    bracket(bracket&&) = delete;
    bracket& operator=(bracket&&) = delete;
  } scope(*this);

  pathResolver* resolver = resolver_.load(std::memory_order_seq_cst);
  if (resolver == nullptr) return false;

  unsigned int timeoutMs = timeoutMs_.load(std::memory_order_relaxed);
  if (timeoutMs == 0) timeoutMs = YSE::patcher::DEFAULT_RESOLVE_TIMEOUT_MS;
  const unsigned int id = NextRequestId();
  e.requestId.store(id, std::memory_order_relaxed);
  e.deadline.store(NowNs() + static_cast<std::int64_t>(timeoutMs) * 1000000,
                   std::memory_order_relaxed);
  // Parked. From here a reply — even one from inside the call below — may
  // resume or fail the slot, and the deadline counts. The release publishes
  // the id and the deadline to Reply's and DeliverComplete's acquire.
  e.state.store(STATE_RESOLVING, std::memory_order_release);

  const pathRequest request{id, e.path, e.access, e.objectType, e.objectId};
  try {
    resolver->Resolve(request);
  } catch (...) {
    // A resolver that throws has refused. Unless it replied before throwing,
    // fail the request now rather than at its deadline.
    std::uint32_t parked = STATE_RESOLVING;
    e.state.compare_exchange_strong(parked, STATE_COMPLETE, std::memory_order_acq_rel,
                                    std::memory_order_relaxed);
  }
  return true;
}

void fileScheduler::Resume(Entry& e, const char* path, std::size_t pathLength) {
  // The slot is REPLYING: nothing else reads or writes it until the store
  // below hands it on.
  if (path == nullptr || pathLength == 0 || pathLength >= PATH_CAPACITY) {
    e.state.store(STATE_COMPLETE, std::memory_order_release);
    return;
  }
  std::memcpy(e.resolvedPath, path, pathLength);
  e.resolvedPath[pathLength] = '\0';
  e.resolved = true;
  e.state.store(STATE_ARMED, std::memory_order_release);
  // The job may still be inside run() — a reply from inside Resolve, or a
  // fast one from another thread. start() then counts a second run, which the
  // worker picks up after this one returns (issue #1190).
  INTERNAL::Global().addSlowJob(&e.job);
  // As in Arm(): a pool that is not running drops the job, and nothing would
  // ever move the slot again.
  if (!e.job.isQueued()) {
    std::uint32_t rearmed = STATE_ARMED;
    e.state.compare_exchange_strong(rearmed, STATE_COMPLETE, std::memory_order_acq_rel,
                                    std::memory_order_relaxed);
  }
}

bool fileScheduler::Reply(unsigned int requestId, const char* path) {
  if (requestId == 0) return false;
  const std::size_t length = path != nullptr ? strnlen(path, PATH_CAPACITY) : 0;
  schedulerRegistry& registry = Registry();
  const std::lock_guard<std::mutex> lock(registry.mutex);
  for (fileScheduler* scheduler : registry.live) {
    for (std::size_t i = 0; i < CAPACITY; ++i) {
      Entry& e = scheduler->entries_[i];
      if (e.requestId.load(std::memory_order_acquire) != requestId) continue;
      // Ids are unique, so this is the only slot that can hold the request.
      // Not parked any more: answered already, timed out or refused.
      std::uint32_t parked = STATE_RESOLVING;
      if (!e.state.compare_exchange_strong(parked, STATE_REPLYING, std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
        return false;
      }
      // The slot finished and was claimed for another request between the
      // two loads above: that one is parked, not ours, so put it back.
      if (e.requestId.load(std::memory_order_relaxed) != requestId) {
        e.state.store(STATE_RESOLVING, std::memory_order_release);
        return false;
      }
      Resume(e, path, length);
      return true;
    }
  }
  return false;
}

void fileScheduler::SetResolver(pathResolver* resolver, unsigned int timeoutMs) {
  timeoutMs_.store(timeoutMs, std::memory_order_relaxed);
  resolver_.store(resolver, std::memory_order_seq_cst);
  // Grace period: wait until no other thread is still inside the resolver this
  // replaced, so the caller may destroy it on return. This thread's own post
  // — a resolver that re-installs from inside Resolve — is excluded.
  const unsigned int own = tResolvingFor == this ? 1u : 0u;
  while (resolving_.load(std::memory_order_seq_cst) > own)
    std::this_thread::yield();
}

bool fileScheduler::HasResolver() const {
  return resolver_.load(std::memory_order_acquire) != nullptr;
}

bool fileScheduler::ReadSlot(Entry& e) {
  // The host's virtual file system, honoured the way soundFile::loadNonStreaming
  // and fileBuffer::load honour it, so a packed-asset host still works. It has
  // no path semantics of its own — the name goes through as given.
  if (YSE::IO().getActive()) {
    // The customFileReader accessors snapshot the callback set race-free and
    // report failure while the layer is (or just went) inactive, which also
    // covers the unset-callback guards this block used to spell out (#837).
    // A resolver's answer is the name the virtual file system gets (#1255).
    const char* name = e.resolved ? e.resolvedPath : e.path;
    if (!INTERNAL::customFileReader::FileExists(name)) return false;

    long long size = 0;
    void* handle = nullptr;
    if (!INTERNAL::customFileReader::Open(name, &size, &handle)) return false;

    bool ok = false;
    if (size >= 0 && (unsigned long long)size <= (unsigned long long)BYTES_CAPACITY) {
      const long long got = INTERNAL::customFileReader::Read(e.bytes, size, handle);
      if (got >= 0) {
        e.byteCount = (std::uint32_t)got;
        ok = true;
      }
    }
    INTERNAL::customFileReader::Close(handle);
    return ok;
  }

  // A resolver's answer is used exactly as given (issue #1255).
  const std::string full = e.resolved ? std::string(e.resolvedPath) : ResolvePath(e.path);
  if (!YSE::FileExists(full)) return false;

  std::ifstream in(full, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  // Sized up front rather than read-until-full: a file that exactly filled the
  // slot would otherwise be indistinguishable from one that overflowed it, and
  // a silently truncated collection is worse than a refused one.
  if (size < 0 || (unsigned long long)size > (unsigned long long)BYTES_CAPACITY) return false;
  in.seekg(0, std::ios::beg);
  if (size > 0) in.read(e.bytes, (std::streamsize)size);
  if (in.gcount() != (std::streamsize)size) return false;
  e.byteCount = (std::uint32_t)size;
  return true;
}

bool fileScheduler::WriteSlot(Entry& e) {
  // The custom-IO backend is read-only: customFileReader exposes open, read and
  // seek callbacks and no write, so there is nowhere to put the bytes while it
  // is active. fileBuffer::save refuses for the same reason.
  if (YSE::IO().getActive()) return false;

  const std::string full = e.resolved ? std::string(e.resolvedPath) : ResolvePath(e.path);
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  if (e.byteCount > 0) out.write(e.bytes, (std::streamsize)e.byteCount);
  out.flush();
  return out.good();
}

void fileScheduler::DeliverComplete(const GraphState* graph, YSE::THREAD thread) {
  std::int64_t now = 0; // read once, and only when a slot is parked
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    std::uint32_t state = e.state.load(std::memory_order_acquire);
    if (state == STATE_RESOLVING) {
      // Parked on the host's resolver (issue #1255). Past its deadline it
      // fails like a missing file. A reply racing this loses the CAS or wins
      // it; either way the slot is moved exactly once. One clock read and one
      // CAS: wait-free.
      if (now == 0) now = NowNs();
      if (now < e.deadline.load(std::memory_order_relaxed)) continue;
      std::uint32_t parked = STATE_RESOLVING;
      if (!e.state.compare_exchange_strong(parked, STATE_COMPLETE, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        continue;
      }
      state = STATE_COMPLETE;
    }
    if (state != STATE_COMPLETE) continue;
    // RunSlot stores COMPLETE as its last act inside run(), but the worker only
    // clears the job's queued flag *after* run() returns. Waiting for that
    // closes the window in which freeing the slot below would let a re-request
    // push a job the pool still has in hand.
    if (e.job.isQueued()) continue;

    pObject* target = e.target;
    const std::uint64_t targetTag = e.targetTag;
    const fileResult result{e.tag, e.op, e.ok, e.bytes, (std::size_t)e.byteCount};

    if (graph != nullptr) {
      // Re-resolve the target against the pinned snapshot: pointer *and*
      // instance tag must match, so a deleted or replaced object's result is
      // dropped, and a recycled allocation at the same address cannot
      // impersonate it. Only the snapshot's own (live) pointers are ever
      // dereferenced — the slot's pointer never is. The tag, not the storage
      // ID: storage IDs are reused as objects come and go (issue #733), so a
      // fresh object at a reclaimed address can legitimately carry the dead
      // one's ID — it can never carry its tag.
      for (pObject* obj : graph->objects) {
        if (obj != target || obj->InstanceTag() != targetTag) continue;
        // The dispatch frame the deferral exists for: everything this delivery
        // causes shares one fresh logical-event id (#471), exactly as if the
        // file had been the stimulus.
        messageEventScope frame;
        obj->DeliverFileResult(result, thread);
        break;
      }
    }

    // Freed after the callback, not before: the callback reads e.bytes, and a
    // request made from inside it must not land in the slot it is still
    // reading. It takes one of the others instead, or is refused.
    e.state.store(STATE_FREE, std::memory_order_release);
  }
}

std::size_t fileScheduler::PendingCount() const {
  std::size_t count = 0;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) != STATE_FREE) ++count;
  }
  return count;
}

std::uint64_t fileScheduler::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

void fileScheduler::WaitIdle() {
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}
