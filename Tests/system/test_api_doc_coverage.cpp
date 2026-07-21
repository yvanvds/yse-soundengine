// Failsafe test for issue #398, sibling of patcher/test_doc_coverage.cpp:
// the Sphinx C-API reference (documentation/source/api/c_api.rst) lists each
// yse_c/*.h header with an explicit doxygenfile directive, so a brand-new
// header never appears on the published docs site by itself. That failed
// silently three times (yse_clip.h, yse_python.h, yse_bus.h — wired in by
// PR #397); this test turns the gap into a build failure at PR time.
//
// Two-way guard:
//   forward — every header under YseEngine/c_api/include/yse_c/ has a
//             doxygenfile directive in c_api.rst (header added, docs not
//             wired);
//   reverse — every yse_c doxygenfile directive in c_api.rst points at an
//             existing header (header renamed or removed, stale directive
//             that would otherwise only surface as a Sphinx build error on
//             the next docs deploy).
//
// The test reads the source tree via the compile-time YSE_REPO_ROOT path, so
// it is desktop-only: on Android the tests run from an APK with no source
// tree and this TU is compiled out (see Tests/CMakeLists.txt).

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

  std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

  // Directive targets in c_api.rst that point into the yse_c header dir,
  // as written relative to the Doxygen INPUT root (../YseEngine).
  std::vector<std::string> yseCDirectiveTargets(const std::string& rst) {
    static const std::string directive = "doxygenfile:: ";
    static const std::string prefix = "c_api/include/yse_c/";
    std::vector<std::string> targets;
    std::istringstream lines(rst);
    std::string line;
    while (std::getline(lines, line)) {
      const auto pos = line.find(directive);
      if (pos == std::string::npos) continue;
      std::string target = line.substr(pos + directive.size());
      while (!target.empty() &&
             (target.back() == '\r' || target.back() == ' ' || target.back() == '\t')) {
        target.pop_back();
      }
      if (target.rfind(prefix, 0) == 0) targets.push_back(target);
    }
    return targets;
  }

} // namespace

TEST_SUITE("system") {

  TEST_CASE("doc coverage: every yse_c header is wired into c_api.rst") {
    const fs::path root{YSE_REPO_ROOT};
    const fs::path headerDir = root / "YseEngine" / "c_api" / "include" / "yse_c";
    const fs::path rstPath = root / "documentation" / "source" / "api" / "c_api.rst";

    REQUIRE(fs::is_directory(headerDir));
    REQUIRE(fs::is_regular_file(rstPath));

    const std::string rst = readFile(rstPath);
    REQUIRE_FALSE(rst.empty());

    const auto targets = yseCDirectiveTargets(rst);

    // Sanity check — guards against a parse that silently found nothing
    // (e.g. the directive spelling changed). Any non-zero count is
    // acceptable; the per-item assertions below carry the real work.
    REQUIRE(targets.size() > 0);

    // Reverse: every directive references a header that exists.
    for (const auto& target : targets) {
      CAPTURE(target);
      CHECK_MESSAGE(fs::is_regular_file(root / "YseEngine" / target),
                    "stale doxygenfile directive in c_api.rst — no such header");
    }

    // Forward: every header on disk is referenced by some directive.
    int headerCount = 0;
    for (const auto& entry : fs::directory_iterator(headerDir)) {
      if (entry.path().extension() != ".h") continue;
      ++headerCount;
      const std::string name = entry.path().filename().string();
      CAPTURE(name);
      bool wired = false;
      for (const auto& target : targets) {
        if (fs::path(target).filename().string() == name) {
          wired = true;
          break;
        }
      }
      CHECK_MESSAGE(wired, "header has no doxygenfile directive in c_api.rst — "
                           "add it so the docs site renders it");
    }
    REQUIRE(headerCount > 0);
  }

} // TEST_SUITE
