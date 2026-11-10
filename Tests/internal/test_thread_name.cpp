// OS-visible names for the engine's threads (issue #1259). A host listing its
// process's threads (Linux /proc/<pid>/task/<tid>/comm, Windows
// GetThreadDescription) must be able to tell the engine's threads from its
// own. These cases read the names back through those same OS interfaces
// (Tests/support/thread_names.hpp), not through anything the engine reports.
//
// The audio callback's name is covered in test_device_layer.cpp (it drives
// paCallback), the render workers' in test_render_placement.cpp (an engine
// session).

#include <doctest/doctest.h>
#include "internal/threadName.h"
#include "internal/threadPool.h"
#include "support/thread_names.hpp"
#include <string>
#include <thread>

using YSE::INTERNAL::nameForeignThreadOnce;
using YSE::INTERNAL::setCurrentThreadName;
using YSE::INTERNAL::threadPool;
using YSE::INTERNAL::threadPoolJob;

namespace {
  // Run `body` on a fresh thread, so naming never touches the test runner's
  // own thread (on Linux the main thread's name is the process name).
  template <typename F> void onFreshThread(F body) {
    std::thread t(body);
    t.join();
  }

  // Records the name of the thread it runs on.
  struct NameJob : threadPoolJob {
    std::string seen;
    void run() override {
      seen = TestHelpers::currentThreadName();
    }
  };
} // namespace

TEST_SUITE("internal") {

  TEST_CASE("threadName: setCurrentThreadName names the calling thread (#1259)") {
    if (!TestHelpers::threadNamesSupported()) {
      MESSAGE("no thread-name API on this platform; skipped");
      return;
    }
    std::string seen;
    onFreshThread([&] {
      setCurrentThreadName("YSE test");
      seen = TestHelpers::currentThreadName();
    });
    CHECK(seen == "YSE test");
  }

  TEST_CASE("threadName: a name past the 15-character limit is cut, not dropped (#1259)") {
    // Linux refuses a name of 16 characters or more outright (ERANGE): an
    // uncut "YSE worker 12345" would leave the thread unnamed there.
    if (!TestHelpers::threadNamesSupported()) {
      MESSAGE("no thread-name API on this platform; skipped");
      return;
    }
    std::string seen;
    onFreshThread([&] {
      setCurrentThreadName("YSE worker 1234567");
      seen = TestHelpers::currentThreadName();
    });
    CHECK(seen == "YSE worker 1234");
    CHECK(seen.size() == YSE::INTERNAL::MAX_THREAD_NAME);
  }

  TEST_CASE("threadName: nameForeignThreadOnce names a thread once, first name wins (#1259)") {
    if (!TestHelpers::threadNamesSupported()) {
      MESSAGE("no thread-name API on this platform; skipped");
      return;
    }
    std::string first, afterSecond, afterRename;
    onFreshThread([&] {
      nameForeignThreadOnce("YSE first");
      first = TestHelpers::currentThreadName();
      nameForeignThreadOnce("YSE second");
      afterSecond = TestHelpers::currentThreadName();
      // A later call does not touch the OS at all: a name set in between
      // (here directly) survives it.
      setCurrentThreadName("host renamed");
      nameForeignThreadOnce("YSE first");
      afterRename = TestHelpers::currentThreadName();
    });
    CHECK(first == "YSE first");
    CHECK(afterSecond == "YSE first");
    CHECK(afterRename == "host renamed");

    // The flag is per thread: another thread is named on its own first call.
    std::string other;
    onFreshThread([&] {
      nameForeignThreadOnce("YSE second");
      other = TestHelpers::currentThreadName();
    });
    CHECK(other == "YSE second");
  }

  TEST_CASE("threadName: the background pool's workers are named YSE background (#1259)") {
    if (!TestHelpers::threadNamesSupported()) {
      MESSAGE("no thread-name API on this platform; skipped");
      return;
    }
    threadPool pool(1);
    NameJob job;
    pool.addJob(&job);
    job.join();
    CHECK(job.seen == "YSE background");

    // And a host listing the process's threads sees it.
    bool listed = false;
    const auto names = TestHelpers::processThreadNames(listed);
    if (listed) CHECK(TestHelpers::countThreadsNamed(names, "YSE background") >= 1);
  }

} // TEST_SUITE("internal")
