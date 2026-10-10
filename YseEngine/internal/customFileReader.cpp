/*
  ==============================================================================

    customFileReader.cpp
    Created: 23 Apr 2014 5:22:57pm
    Author:  yvan

  ==============================================================================
*/

#include "customFileReader.h"

#include <atomic>
#include <memory>
#include <vector>

namespace YSE {
  namespace INTERNAL {
    namespace CALLBACK {
      // Staging slots for the host's custom-IO callbacks. Written by the
      // control thread only (the YSE::io setters and ResetVIO); loader
      // threads never read them. UpdateVIO() copies them into an immutable
      // snapshot that readers reach through one atomic pointer, so
      // activating or deactivating the custom IO layer never races an
      // in-flight load (#837).
      bool (*openPtr)(const char* filename, long long* filesize, void** fileHandle);
      void (*closePtr)(void* fileHandle);
      long long (*readPtr)(void* destBuffer, long long maxBytesToRead, void* fileHandle);
      long long (*getPosPtr)(void* fileHandle);
      bool (*fileExists)(const char* filename);
      long long (*lengthPtr)(void* fileHandle);
      long long (*seekPtr)(long long offset, int whence, void* fileHandle);
    } // namespace CALLBACK

    namespace {
      // One activation's callbacks, immutable once published.
      struct vioSnapshot {
        bool (*open)(const char* filename, long long* filesize, void** fileHandle) = nullptr;
        void (*close)(void* fileHandle) = nullptr;
        long long (*read)(void* destBuffer, long long maxBytesToRead, void* fileHandle) = nullptr;
        bool (*exists)(const char* filename) = nullptr;
        SF_VIRTUAL_IO vio{};
      };

      // The active snapshot, or nullptr while custom IO is inactive.
      std::atomic<vioSnapshot*> publishedVIO{nullptr};

      // Every snapshot ever published, kept until process exit so a loader
      // thread that fetched the pointer just before a deactivation never
      // touches freed memory. Growth is bounded by setActive(true) calls —
      // a control-plane action (cfr. the retired meter blocks of #839).
      std::vector<std::unique_ptr<vioSnapshot>>& SnapshotStore() {
        static std::vector<std::unique_ptr<vioSnapshot>> store;
        return store;
      }

      // All-null table handed out while inactive: sf_open_virtual rejects it
      // cleanly (SFE_BAD_VIRTUAL_IO), so a load racing a deactivation fails
      // like any other failed open instead of calling through torn pointers.
      SF_VIRTUAL_IO nullVIO{};
    } // namespace
  } // namespace INTERNAL
} // namespace YSE

bool YSE::INTERNAL::customFileReader::Open(const char* filename, long long* filesize,
                                           void** fileHandle) {
  const vioSnapshot* snap = publishedVIO.load(std::memory_order_acquire);
  if (snap != nullptr && snap->open != nullptr) {
    return snap->open(filename, filesize, fileHandle);
  }
  return false;
}

void YSE::INTERNAL::customFileReader::Close(void* fileHandle) {
  const vioSnapshot* snap = publishedVIO.load(std::memory_order_acquire);
  if (snap != nullptr && snap->close != nullptr) {
    snap->close(fileHandle);
  }
}

bool YSE::INTERNAL::customFileReader::FileExists(const char* filename) {
  const vioSnapshot* snap = publishedVIO.load(std::memory_order_acquire);
  if (snap != nullptr && snap->exists != nullptr) {
    return snap->exists(filename);
  }
  return false;
}

long long YSE::INTERNAL::customFileReader::Read(void* destBuffer, long long maxBytesToRead,
                                                void* fileHandle) {
  const vioSnapshot* snap = publishedVIO.load(std::memory_order_acquire);
  if (snap != nullptr && snap->read != nullptr) {
    return snap->read(destBuffer, maxBytesToRead, fileHandle);
  }
  return -1;
}

void YSE::INTERNAL::customFileReader::UpdateVIO() {
  auto snap = std::make_unique<vioSnapshot>();
  snap->open = CALLBACK::openPtr;
  snap->close = CALLBACK::closePtr;
  snap->read = CALLBACK::readPtr;
  snap->exists = CALLBACK::fileExists;
// On Windows (LLP64), libsndfile's sf_count_t is `long long` — direct
// assignment of YSE's `long long`-typed callbacks works without a cast.
// On LP64 platforms (Linux, macOS, modern 64-bit Android) sf_count_t is `long`,
// so the callback pointers must be cast through the SF_VIRTUAL_IO signature.
// The legacy 32-bit Android (armeabi-v7a) path matched the Windows shape, but
// this build no longer targets ILP32 ABIs.
#if defined(YSE_WINDOWS)
  snap->vio.get_filelen = CALLBACK::lengthPtr;
  snap->vio.read = CALLBACK::readPtr;
  snap->vio.seek = CALLBACK::seekPtr;
  snap->vio.tell = CALLBACK::getPosPtr;
  snap->vio.write = NULL;
#else
  snap->vio.get_filelen = (long int (*)(void*))CALLBACK::lengthPtr;
  snap->vio.read = (long int (*)(void*, long int, void*))CALLBACK::readPtr;
  snap->vio.seek = (long int (*)(long int, int, void*))CALLBACK::seekPtr;
  snap->vio.tell = (long int (*)(void*))CALLBACK::getPosPtr;
  snap->vio.write = NULL;
#endif
  vioSnapshot* raw = snap.get();
  SnapshotStore().push_back(std::move(snap));
  publishedVIO.store(raw, std::memory_order_release);
}

void YSE::INTERNAL::customFileReader::ResetVIO() {
  // Unpublish first: a loader thread that loads the pointer after this store
  // sees the custom IO layer as inactive and fails its open cleanly. A loader
  // that fetched the old snapshot just before keeps a fully consistent
  // callback set (the snapshot is immutable and stays allocated).
  publishedVIO.store(nullptr, std::memory_order_release);

  // The staging slots are control-thread-only, so clearing them in place
  // cannot race a reader — readers only ever see published snapshots.
  CALLBACK::closePtr = nullptr;
  CALLBACK::fileExists = nullptr;
  CALLBACK::getPosPtr = nullptr;
  CALLBACK::lengthPtr = nullptr;
  CALLBACK::openPtr = nullptr;
  CALLBACK::readPtr = nullptr;
  CALLBACK::seekPtr = nullptr;
}

SF_VIRTUAL_IO& YSE::INTERNAL::customFileReader::GetVIO() {
  vioSnapshot* snap = publishedVIO.load(std::memory_order_acquire);
  return snap != nullptr ? snap->vio : nullVIO;
}
