// Tests for YseEngine/utils/fileFunctions.hpp (issue #1249).
//
// GetCurrentWorkingDirectory is what every relative file name the engine opens
// is joined to — sound files in soundImplementation::create, and every patcher
// object's read / write through fileScheduler. On Windows it used to return the
// executable's folder, so a host that changed directory still had its files
// read from and written next to the .exe, while Linux used the real working
// directory. The contract pinned here is the one its name and the patcher docs
// promise: it is the process's working directory, wherever the host has moved
// it, on every platform. The patcher-level consequence is asserted end to end
// in test_patcher_coll.cpp.
//
// No audio device and no engine required.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "support/scoped_cwd.hpp"
#include "utils/fileFunctions.hpp"

TEST_SUITE("utils") {

  TEST_CASE("fileFunctions: the working directory is the process's, not the executable's (#1249)") {
    TestHelpers::ScopedWorkingDirectory cwd("yse_cwd_1249_utils");

    const std::string reported = YSE::GetCurrentWorkingDirectory();
    REQUIRE_FALSE(reported.empty());
    // equivalent() rather than string equality: the temp directory may be
    // reached through a symlink (or an 8.3 short name), and the function may
    // report the resolved form.
    CHECK(std::filesystem::equivalent(reported, cwd.path()));
    CHECK(std::filesystem::path(reported).is_absolute());
  }

  TEST_CASE("fileFunctions: a name joined to it opens the working directory's file (#1249)") {
    // The exact join the engine does, so a working directory reported in a
    // form the file APIs cannot open (a truncated or mis-encoded path) fails
    // here even if it compares equivalent above.
    TestHelpers::ScopedWorkingDirectory cwd("yse_cwd_1249_join");
    {
      std::ofstream out(cwd.path() / "here_1249.txt");
      out << "x";
    }
#if defined(_WIN32) || defined(_WIN64)
    const char delim = '\\';
#else
    const char delim = '/';
#endif
    CHECK(YSE::FileExists(YSE::GetCurrentWorkingDirectory() + delim + "here_1249.txt"));
  }
}
