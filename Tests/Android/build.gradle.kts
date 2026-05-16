// Tests/Android/ — minimal Gradle project that packages the prebuilt
// libyse_tests.so (produced by the root CMake build under
// build-android-${ABI}/bin/) into a NativeActivity APK.
//
// Gradle does NOT drive the native build — there is no externalNativeBuild
// block in app/build.gradle.kts. Workflow:
//   1. From repo root, run `cmake` with the NDK toolchain for each ABI,
//      producing build-android-arm64/bin/libyse_tests.so and
//      build-android-x86_64/bin/libyse_tests.so (or any subset).
//   2. cd Tests/Android && ./gradlew assembleDebug
// The Gradle build refuses to start if no .so artefacts exist.

plugins {
    id("com.android.application") version "8.5.0" apply false
}
