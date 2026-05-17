#include "android_asset_bridge.hpp"

#if defined(__ANDROID__)

#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace yse_tests {

namespace {

constexpr const char * kLogTag = "yse_tests";
constexpr const char * kAssetSubdir = "fixtures";

// Cache the resolved fixture root so test code (which can't pass app state) can
// retrieve it via fixturesDir(). Empty until extractFixturesFromAssets runs.
std::string g_fixtures_dir;

bool ensureDir(const std::string & path) {
  if (mkdir(path.c_str(), 0775) == 0) return true;
  if (errno == EEXIST) return true;
  __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                      "mkdir(%s) failed: %s", path.c_str(), std::strerror(errno));
  return false;
}

bool copyAssetToFile(AAssetManager * mgr, const std::string & assetPath, const std::string & outPath) {
  AAsset * asset = AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_STREAMING);
  if (!asset) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "AAssetManager_open(%s) returned null", assetPath.c_str());
    return false;
  }

  FILE * out = std::fopen(outPath.c_str(), "wb");
  if (!out) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "fopen(%s) failed: %s", outPath.c_str(), std::strerror(errno));
    AAsset_close(asset);
    return false;
  }

  char buf[4096];
  int n;
  bool ok = true;
  while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
    if (std::fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
      __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                          "fwrite(%s) short write", outPath.c_str());
      ok = false;
      break;
    }
  }
  if (n < 0) ok = false;

  std::fclose(out);
  AAsset_close(asset);
  return ok;
}

bool extractDirRecursive(AAssetManager * mgr,
                        const std::string & assetDir,
                        const std::string & outDir) {
  if (!ensureDir(outDir)) return false;

  AAssetDir * dir = AAssetManager_openDir(mgr, assetDir.c_str());
  if (!dir) return false;

  bool ok = true;
  // AAssetDir_getNextFileName lists files but NOT subdirectories (an NDK quirk),
  // so the caller must enumerate subdirs themselves. Our fixtures tree is flat
  // (Tests/support/fixtures/ contains only files), so this is acceptable for now.
  const char * name;
  while ((name = AAssetDir_getNextFileName(dir)) != nullptr) {
    const std::string assetPath = assetDir + "/" + name;
    const std::string outPath = outDir + "/" + name;
    if (!copyAssetToFile(mgr, assetPath, outPath)) ok = false;
  }
  AAssetDir_close(dir);
  return ok;
}

}  // namespace

bool extractFixturesFromAssets(struct android_app * app) {
  if (!app || !app->activity || !app->activity->assetManager) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "no AAssetManager available");
    return false;
  }
  const char * internalDataPath = app->activity->internalDataPath;
  if (!internalDataPath) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "internalDataPath is null");
    return false;
  }
  g_fixtures_dir = std::string(internalDataPath) + "/fixtures";
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "extracting fixtures from APK assets to %s", g_fixtures_dir.c_str());
  return extractDirRecursive(app->activity->assetManager, kAssetSubdir, g_fixtures_dir);
}

const char * fixturesDir() {
  return g_fixtures_dir.empty() ? nullptr : g_fixtures_dir.c_str();
}

}  // namespace yse_tests

#endif  // __ANDROID__
