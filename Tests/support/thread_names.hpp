// Read thread names back the way a host does (issue #1259): Linux and Android
// through /proc/self/task/<tid>/comm, Windows through GetThreadDescription.
//
// currentThreadName() works on Linux, Android, Windows and macOS.
// processThreadNames() lists every thread of the process, which needs /proc
// or a Toolhelp snapshot; elsewhere (macOS) it reports itself unsupported and
// callers skip. The platform code lives in thread_names.cpp, so <windows.h>
// stays out of the test files that include this.

#ifndef YSE_TESTS_SUPPORT_THREAD_NAMES_HPP
#define YSE_TESTS_SUPPORT_THREAD_NAMES_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace TestHelpers {

  // Whether this platform can name threads at all, and so whether
  // currentThreadName() reads back what the engine set.
  bool threadNamesSupported();

  // The calling thread's name, as the OS reports it; empty if unavailable.
  std::string currentThreadName();

  // Every thread of this process's name. `supported` is false where a process
  // cannot list its threads' names without a debugger API (macOS).
  std::vector<std::string> processThreadNames(bool& supported);

  // How many of `names` are exactly `name`.
  std::size_t countThreadsNamed(const std::vector<std::string>& names, const std::string& name);

} // namespace TestHelpers

#endif // YSE_TESTS_SUPPORT_THREAD_NAMES_HPP
