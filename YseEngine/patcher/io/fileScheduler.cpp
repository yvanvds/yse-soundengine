#include "fileScheduler.h"
#include "../../headers/defines.hpp"
#include "../../internal/customFileReader.h"
#include "../../internal/global.h"
#include "../../io.hpp"
#include "../../utils/fileFunctions.hpp"
#include "../graphState.h"
#include "../inlet.h"
#include "../pObject.h"
#include <cstring>
#include <fstream>
#include <string>

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

} // namespace

fileScheduler::fileScheduler() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a request needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

fileScheduler::~fileScheduler() {
  // Join every file job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob. That join runs in the *base* destructor —
  // after ~fileJob has already reset the vtable — so a worker that picks the
  // job up inside that window calls threadPoolJob::run(), which is pure
  // virtual (issue #706). Two more windows close with it: ~unique_ptr nulls
  // `entries_` before it destroys the elements, and ~Entry destroys the fields
  // a running job reads. Control thread: the patcher's destructor, where the
  // audio thread is already stopped and Clear() has run, so nothing can arm a
  // new request.
  WaitIdle();
}

void fileScheduler::fileJob::run() {
  owner->RunSlot(*slot);
}

bool fileScheduler::Arm(pObject* target, int tag, FILE_OP op, const char* path,
                        std::size_t pathLength, const char* bytes, std::size_t byteCount) {
  if (target == nullptr || path == nullptr || pathLength == 0 || pathLength >= PATH_CAPACITY) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (op == FILE_OP::WRITE && (byteCount > BYTES_CAPACITY || (byteCount > 0 && bytes == nullptr))) {
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
    std::memcpy(e.path, path, pathLength);
    e.path[pathLength] = '\0';
    if (op == FILE_OP::WRITE && byteCount > 0) {
      std::memcpy(e.bytes, bytes, byteCount);
    }
    e.byteCount = op == FILE_OP::WRITE ? (std::uint32_t)byteCount : 0;
    e.target = target;
    e.targetTag = target->InstanceTag();
    e.tag = tag;
    e.op = op;
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

void fileScheduler::RunSlot(Entry& e) {
  std::uint32_t armed = STATE_ARMED;
  if (!e.state.compare_exchange_strong(armed, STATE_RUNNING, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
    // Already completed as a failure by Arm() because the pool refused it.
    return;
  }
  e.ok = e.op == FILE_OP::READ ? ReadSlot(e) : WriteSlot(e);
  e.state.store(STATE_COMPLETE, std::memory_order_release);
}

bool fileScheduler::ReadSlot(Entry& e) {
  // The host's virtual file system, honoured the way soundFile::loadNonStreaming
  // and fileBuffer::load honour it, so a packed-asset host still works. It has
  // no path semantics of its own — the name goes through as given.
  if (YSE::IO().getActive()) {
    if (INTERNAL::CALLBACK::fileExists == nullptr || INTERNAL::CALLBACK::readPtr == nullptr) {
      return false;
    }
    if (!INTERNAL::CALLBACK::fileExists(e.path)) return false;

    long long size = 0;
    void* handle = nullptr;
    if (!INTERNAL::customFileReader::Open(e.path, &size, &handle)) return false;

    bool ok = false;
    if (size >= 0 && (unsigned long long)size <= (unsigned long long)BYTES_CAPACITY) {
      const long long got = INTERNAL::CALLBACK::readPtr(e.bytes, size, handle);
      if (got >= 0) {
        e.byteCount = (std::uint32_t)got;
        ok = true;
      }
    }
    INTERNAL::customFileReader::Close(handle);
    return ok;
  }

  const std::string full = ResolvePath(e.path);
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

  const std::string full = ResolvePath(e.path);
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  if (e.byteCount > 0) out.write(e.bytes, (std::streamsize)e.byteCount);
  out.flush();
  return out.good();
}

void fileScheduler::DeliverComplete(const GraphState* graph, YSE::THREAD thread) {
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_acquire) != STATE_COMPLETE) continue;
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
