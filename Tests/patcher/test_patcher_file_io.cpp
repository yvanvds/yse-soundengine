// Tests for the patcher's file-I/O scheduler (issue #683) — the shared plumbing
// under `.coll`'s read/write and, next, `.textfile` (#687), `.qlist` (#689),
// `.mtr` (#691) and `.seq` (#692).
//
// The user-visible behaviour it enables is asserted end to end in
// test_patcher_coll.cpp, where a real collection round-trips through a real file
// on disk. What is left for here is the part no consumer can show on its own,
// and every one of these is a rule a plausible implementation drops silently:
//
//   - **a request never blocks and never grows.** The table is fixed, an
//     over-long path or an over-large payload is refused rather than truncated
//     or allocated for, and a full table refuses instead of waiting. Refusals
//     are counted, not logged, because the requesting thread may be the audio
//     callback.
//   - **a request outliving its object is safe by construction.** The
//     background job holds no pObject at all, and delivery re-resolves the
//     target against the block's pinned snapshot — so deleting an object with a
//     read in flight drops the result rather than writing into freed memory.
//     This is the case that would otherwise be a use-after-free found in
//     production rather than here.
//   - **a patcher pays nothing for plumbing it does not use.** The slot table is
//     half a megabyte, so it is built only when an object that can read or write
//     files joins.
//
// No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/patcherImplementation.h"

using YSE::PATCHER::fileScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A target for a request. The scheduler only ever reads its id, never
  // dereferences it after the request is made, so this is all a target needs to
  // be for the refusal and capacity cases.
  struct Target : YSE::PATCHER::pObject {
    Target() : pObject(false) {}
    const char* Type() const override {
      return "file_io_target";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── what a request refuses ─────────────────────────────────────────────────

  TEST_CASE("fileScheduler: a malformed request is refused and counted (#683)") {
    fileScheduler io;
    Target target;
    const std::string ok = "somewhere.txt";
    const std::string tooLong(fileScheduler::PATH_CAPACITY, 'x');

    CHECK_FALSE(io.RequestRead(nullptr, 0, ok.c_str(), ok.size()));
    CHECK_FALSE(io.RequestRead(&target, 0, nullptr, 0));
    CHECK_FALSE(io.RequestRead(&target, 0, ok.c_str(), 0));
    CHECK_FALSE(io.RequestRead(&target, 0, tooLong.c_str(), tooLong.size()));
    // Refused rather than truncated: a path cut short names a different file,
    // and a payload cut short is a different file's worth of contents.
    CHECK_FALSE(io.RequestWrite(&target, 0, ok.c_str(), ok.size(), tooLong.c_str(),
                                fileScheduler::BYTES_CAPACITY + 1));

    CHECK(io.Dropped() == 5);
    CHECK(io.PendingCount() == 0);
  }

  TEST_CASE("fileScheduler: a full table refuses rather than waits (#683)") {
    // Nothing here calls DeliverComplete, so every finished request stays in its
    // slot: exactly the state a patcher that is not rendering ends up in. The
    // point is that the request past the bound comes back false — a message
    // handler that may be the audio callback cannot be made to wait for a disk.
    fileScheduler io;
    Target target;
    const std::string path = TempFile("yse_file_io_absent_683.txt");

    for (std::size_t i = 0; i < fileScheduler::CAPACITY; i++) {
      CHECK(io.RequestRead(&target, (int)i, path.c_str(), path.size()));
    }
    CHECK(io.PendingCount() == fileScheduler::CAPACITY);

    CHECK_FALSE(io.RequestRead(&target, 99, path.c_str(), path.size()));
    CHECK(io.Dropped() == 1);

    io.WaitIdle();
  }

  // ─── the disk half ──────────────────────────────────────────────────────────

  TEST_CASE("fileScheduler: a write lands on disk and reads back byte for byte (#683)") {
    // The plumbing on its own, with no object in the way: what goes into a
    // request is what ends up in the file. Binary rather than text, because a
    // consumer that writes a standard MIDI file (#692) needs the bytes untouched.
    const std::string path = TempFile("yse_file_io_bytes_683.bin");
    std::string payload;
    for (int i = 0; i < 256; i++)
      payload.push_back((char)i);

    fileScheduler io;
    Target target;
    REQUIRE(io.RequestWrite(&target, 0, path.c_str(), path.size(), payload.data(), payload.size()));
    io.WaitIdle();

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::string written((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    CHECK(written.size() == payload.size());
    CHECK(written == payload);

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  // ─── lifetime ───────────────────────────────────────────────────────────────

  TEST_CASE("fileScheduler: a patcher has no file table until a file-capable object joins (#683)") {
    // The table is half a megabyte of inline slots. A patch of oscillators and
    // arithmetic should not carry one, so it is built by the first object that
    // can read or write — `.coll` asks from its SetParent override.
    patcherImplementation p(1, nullptr);
    CHECK(p.FileIO() == nullptr);

    REQUIRE(p.CreateObject(YSE::OBJ::G_FLOAT, "") != nullptr);
    CHECK(p.FileIO() == nullptr);

    REQUIRE(p.CreateObject(YSE::OBJ::G_COLL, "") != nullptr);
    fileScheduler* io = p.FileIO();
    CHECK(io != nullptr);

    // And exactly one: a second file-capable object finds the table that exists.
    REQUIRE(p.CreateObject(YSE::OBJ::G_COLL, "") != nullptr);
    CHECK(p.FileIO() == io);
  }

  TEST_CASE("fileScheduler: a result for an object deleted mid-flight is dropped (#683)") {
    // The lifetime guarantee, and the reason the job holds no pObject: a live
    // edit can retire the requesting object between the `read` and the block
    // that would deliver it. Delivery re-resolves the target against the pinned
    // snapshot, so a deleted object is simply absent and its result goes
    // nowhere — and the slot still comes back.
    const std::string path = TempFile("yse_file_io_deleted_683.txt");
    {
      std::ofstream out(path, std::ios::binary);
      out << "0, gone;\n";
    }

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);

    coll->SetListData(0, "read " + path);
    CHECK(io->PendingCount() == 1);

    // Retired while the read is still in flight.
    p.DeleteObject(coll);

    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(io->PendingCount() == 0);

    // Still usable afterwards: the dropped result freed its slot rather than
    // stranding it.
    YSE::pHandle* fresh = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(fresh != nullptr);
    fresh->SetListData(0, "read " + path);
    CHECK(io->PendingCount() == 1);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(io->PendingCount() == 0);

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("fileScheduler: a request still on the pool survives the teardown (#706)") {
    // The scheduler used to leave the join to ~threadPoolJob, which runs in the
    // *base* destructor — after ~fileJob has reset the vtable to one whose
    // run() is pure virtual, and after ~unique_ptr has nulled `entries_`. A
    // worker picking the job up inside that window aborted with "pure virtual
    // function called" (both faults reproduced in the #688 bridge, which has the
    // same construction). Nothing in the suite hit it because a request always
    // finished long before the patcher went away.
    //
    // So make the window instead of waiting for it. The background pool has
    // exactly one worker: park it in a blocker job, arm a read behind it, and
    // only let the blocker go once teardown is under way. On the unfixed code
    // the worker then reaches the job with the derived half already gone and the
    // process aborts; with the join moved to the top of ~fileScheduler the job
    // runs against a whole object and the destructor simply waits for it.
    struct Blocker : YSE::INTERNAL::threadPoolJob {
      std::atomic<bool> running{false};
      std::atomic<bool> release{false};
      void run() override {
        running.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      }
    };

    const std::string path = TempFile("yse_file_io_teardown_706.txt");
    {
      std::ofstream out(path, std::ios::binary);
      out << "0, teardown;\n";
    }

    Blocker blocker;
    YSE::INTERNAL::Global().addSlowJob(&blocker);
    // Bounded rather than an open spin: a pool that never started would
    // otherwise hang the suite instead of reporting that it cannot run this.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blocker.running.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    REQUIRE(blocker.running.load(std::memory_order_acquire));

    fileScheduler* io = new fileScheduler();
    Target target;
    REQUIRE(io->RequestRead(&target, 0, path.c_str(), path.size()));
    // Queued behind the blocker on the one worker, so it cannot have run yet.
    CHECK(io->PendingCount() == 1);

    std::atomic<bool> tearingDown{false};
    std::thread releaser([&] {
      while (!tearingDown.load(std::memory_order_acquire))
        std::this_thread::yield();
      // Long enough for the destructor to get past the vtable reset (three
      // no-op ~Entry calls away), short enough that the pool is not held up.
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      blocker.release.store(true, std::memory_order_release);
    });

    tearingDown.store(true, std::memory_order_release);
    delete io; // aborted here before #706

    releaser.join();
    blocker.join();

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
}
