// Run a test from inside a fresh directory, and put the process back where it
// was afterwards (issue #1249).
//
// The working directory is process state, so a test that moves it has to
// restore it even when a REQUIRE unwinds out of the test — the suites after it
// in the same ctest process resolve fixtures and output paths against it. The
// directory is made empty first, so a leftover from an earlier run cannot make
// a "the file landed here" check pass for the wrong reason, and it is removed
// on the way out.
//
// It is deliberately not the executable's folder or the system temp directory
// itself: #1249 was a working directory that quietly meant the executable's
// folder on Windows, and a ctest WORKING_DIRECTORY of build-X/bin/ is that
// folder, so a test run from there could not tell the two apart.

#ifndef YSE_TESTS_SUPPORT_SCOPED_CWD_HPP
#define YSE_TESTS_SUPPORT_SCOPED_CWD_HPP

#include <filesystem>
#include <system_error>

namespace TestHelpers {

  class ScopedWorkingDirectory {
  public:
    explicit ScopedWorkingDirectory(const char* name)
      : previous_(std::filesystem::current_path()),
        dir_(std::filesystem::temp_directory_path() / name) {
      std::error_code ec;
      std::filesystem::remove_all(dir_, ec);
      std::filesystem::create_directories(dir_);
      std::filesystem::current_path(dir_);
    }

    ~ScopedWorkingDirectory() {
      std::error_code ec;
      std::filesystem::current_path(previous_, ec);
      std::filesystem::remove_all(dir_, ec);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    /** The directory the process is in while this object lives. */
    const std::filesystem::path& path() const {
      return dir_;
    }

  private:
    std::filesystem::path previous_;
    std::filesystem::path dir_;
  };

} // namespace TestHelpers

#endif // YSE_TESTS_SUPPORT_SCOPED_CWD_HPP
