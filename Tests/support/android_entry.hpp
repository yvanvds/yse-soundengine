#pragma once

// Android entry point for the yse_tests library. NativeActivity dlopens the
// .so and calls ANativeActivity_onCreate (provided by android_native_app_glue);
// the glue then invokes android_main() on a dedicated thread.
//
// Responsibilities:
//   1. Wait for NativeActivity to give us a usable activity context
//      (AAssetManager + internalDataPath both arrive via app->activity).
//   2. Extract test fixtures from APK assets to /data/local/tmp/yse_fixtures
//      so the existing YSE_TEST_FIXTURES_DIR macro continues to point at a
//      filesystem path that std::ifstream / fopen can read.
//   3. Redirect stdout + stderr into logcat so doctest's normal text output
//      (including coloured diff hunks) is captured by `adb logcat -s yse_tests`.
//   4. Run the doctest suite, shut down the engine, then finish the activity
//      so the APK exits cleanly.

#if defined(__ANDROID__)

#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <thread>
#include <unistd.h>

#include "../../YseEngine/yse.hpp"
#include "null_device.hpp"
#include "android_asset_bridge.hpp"

namespace {

constexpr const char * kLogTag = "yse_tests";

// Background thread that drains a pipe end-of-file (the writer end is dup2'd
// over stdout/stderr) into logcat one line at a time. doctest's output is
// line-oriented so this preserves the formatting of assertion failures and
// summary tables.
void * pump_pipe_to_logcat(void * arg) {
  int fd = *static_cast<int *>(arg);
  delete static_cast<int *>(arg);

  FILE * input = fdopen(fd, "r");
  if (!input) return nullptr;

  char line[1024];
  while (fgets(line, sizeof(line), input) != nullptr) {
    size_t len = std::strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line);
  }

  fclose(input);
  return nullptr;
}

void redirect_stdio_to_logcat() {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) return;

  // Make stdout/stderr line-buffered so fgets sees a line as soon as doctest
  // emits one; without this, fully-buffered stdio would hold output until the
  // process exits and the logcat dump would be useless mid-run.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IOLBF, 0);

  dup2(pipe_fds[1], STDOUT_FILENO);
  dup2(pipe_fds[1], STDERR_FILENO);
  close(pipe_fds[1]);

  pthread_t tid;
  int * fd_arg = new int(pipe_fds[0]);
  if (pthread_create(&tid, nullptr, pump_pipe_to_logcat, fd_arg) == 0) {
    pthread_detach(tid);
  } else {
    delete fd_arg;
  }
}

// Block until NativeActivity reports its main window is ready (APP_CMD_INIT_WINDOW)
// or until destruction is requested. The activity pointer and asset manager
// are available immediately on app creation, but waiting on a real lifecycle
// event ensures the runtime is fully in foreground before tests start.
void wait_for_activity_ready(struct android_app * app) {
  while (!app->destroyRequested && app->window == nullptr) {
    int events;
    struct android_poll_source * source;
    if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
      if (source != nullptr) source->process(app, source);
    }
  }
}

}  // namespace

extern "C" void android_main(struct android_app * app) {
  redirect_stdio_to_logcat();
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main: starting");

  wait_for_activity_ready(app);
  if (app->destroyRequested) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main: destroy requested before run");
    return;
  }

  // Extract APK assets to app->activity->internalDataPath/fixtures (the only
  // path a NativeActivity reliably has write access to). The compile-time
  // YSE_TEST_FIXTURES_DIR macro in Tests/CMakeLists.txt is set to that same
  // path, so test code finds the files via plain fopen/ifstream.
  if (!yse_tests::extractFixturesFromAssets(app)) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "android_main: fixture extraction failed; tests that load files will fail");
  } else {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "android_main: fixtures available at %s", yse_tests::fixturesDir());
  }

  doctest::Context context;
  // Match desktop CI: skip integration suite (needs live PortAudio device that
  // doesn't exist here), force per-test duration printing.
  context.setOption("test-suite-exclude", "integration");
  context.setOption("duration", true);
  const int res = context.run();

  if (TestHelpers::engineInitialized()) {
    YSE::System().close();
  }

  __android_log_print(ANDROID_LOG_INFO, kLogTag, "android_main: doctest exit code %d", res);

  // Cleanly finish the NativeActivity so the APK process terminates and logcat
  // collectors see a definitive end-of-run.
  ANativeActivity_finish(app->activity);

  // Pump events until NativeActivity confirms destruction, otherwise the glue
  // thread would exit while the activity is still half-alive.
  while (!app->destroyRequested) {
    int events;
    struct android_poll_source * source;
    if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
      if (source != nullptr) source->process(app, source);
    }
  }
}

#endif  // __ANDROID__
