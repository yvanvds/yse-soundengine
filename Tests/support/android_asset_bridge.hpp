#pragma once

// Bridges APK assets (read-only, accessed via AAssetManager) onto a regular
// filesystem path that the existing test fixture-loading code can use without
// modification. yse_tests references fixtures via the compile-time
// YSE_TEST_FIXTURES_DIR macro and treats them as plain files (fopen / ifstream),
// so we must materialise them on the actual filesystem before tests run.

#if defined(__ANDROID__)

struct android_app;

namespace yse_tests {

// Walk every file under the APK's `assets/fixtures/` tree and copy it under
// app->activity->internalDataPath/fixtures/ (the only filesystem location a
// NativeActivity is reliably allowed to write to). Idempotent — re-running
// over an already-populated dest is safe (existing files are overwritten).
//
// Returns false if the activity has no asset manager or any file fails to
// extract; on failure, callers should log and continue so tests that don't
// load fixtures still run.
bool extractFixturesFromAssets(struct android_app * app);

// Path to the extracted fixtures directory (valid after a successful
// extractFixturesFromAssets call). Returns nullptr otherwise.
const char * fixturesDir();

}  // namespace yse_tests

#endif  // __ANDROID__
