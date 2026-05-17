import org.gradle.api.tasks.Copy

plugins {
    id("com.android.application")
}

// Repo root, three levels up from Tests/Android/app/.
val repoRoot: File = rootProject.projectDir.parentFile.parentFile

// ABIs the prebuilt .so files exist for. We probe each candidate build dir at
// configuration time so a partial native build (e.g. arm64-v8a only) still
// produces an APK — Gradle just packages whatever's there. The APK manifest's
// abiFilters is set from the same list further down.
data class Abi(val name: String, val buildDir: File)
val candidateAbis = listOf(
    Abi("arm64-v8a", repoRoot.resolve("build-android-arm64")),
    Abi("x86_64",    repoRoot.resolve("build-android-x86_64")),
)
val presentAbis = candidateAbis.filter { it.buildDir.resolve("bin/libyse_tests.so").exists() }

android {
    namespace = "net.attrx.yse.tests"
    compileSdk = 34
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "net.attrx.yse.tests"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        ndk {
            // Restrict the manifest's abiFilters to ABIs we actually have .so
            // artefacts for, so PackageManager doesn't reject the install on
            // emulators of the missing arch.
            abiFilters += presentAbis.map { it.name }
        }
    }

    sourceSets["main"].apply {
        // libyse_tests.so is dropped under build/intermediates/jniLibs/<abi>/
        // by the prepareJniLibs task below. Point Gradle at that staging dir
        // (NOT at the build-android-* tree directly, so a `gradle clean` only
        // removes Gradle's view of the artefacts, never the CMake build).
        jniLibs.srcDir(layout.buildDirectory.dir("intermediates/jniLibs"))
        // Assets staging mirrors Tests/support/fixtures/ — populated by
        // prepareFixtureAssets so the source tree is untouched.
        assets.srcDir(layout.buildDirectory.dir("intermediates/assets"))
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// Stage prebuilt .so artefacts from each CMake build dir into the
// jniLibs/<abi>/ layout AGP expects when packaging.
val prepareJniLibs by tasks.registering(Copy::class) {
    into(layout.buildDirectory.dir("intermediates/jniLibs"))
    presentAbis.forEach { abi ->
        from(abi.buildDir.resolve("bin/libyse_tests.so")) {
            into(abi.name)
        }
    }
    doFirst {
        if (presentAbis.isEmpty()) {
            throw GradleException(
                "No libyse_tests.so found under build-android-<abi>/bin/. " +
                "Run the root CMake build first: " +
                "cmake -B build-android-arm64 -DCMAKE_TOOLCHAIN_FILE=\$NDK/build/cmake/android.toolchain.cmake " +
                "-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DYSE_BUILD_TESTS=ON && " +
                "cmake --build build-android-arm64 --target yse_tests"
            )
        }
    }
}

// Copy test fixtures into the APK's assets/fixtures/ tree. android_main()
// extracts them to /data/local/tmp/yse_fixtures at runtime so the
// YSE_TEST_FIXTURES_DIR-driven test code finds them via plain fopen.
val prepareFixtureAssets by tasks.registering(Copy::class) {
    from(repoRoot.resolve("Tests/support/fixtures")) {
        into("fixtures")
    }
    into(layout.buildDirectory.dir("intermediates/assets"))
}

// Wire the staging tasks ahead of every variant's merge step so a clean
// `./gradlew assembleDebug` always picks up fresh artefacts.
androidComponents {
    onVariants { variant ->
        afterEvaluate {
            val capitalized = variant.name.replaceFirstChar { it.uppercase() }
            tasks.named("merge${capitalized}JniLibFolders").configure {
                dependsOn(prepareJniLibs)
            }
            tasks.named("merge${capitalized}Assets").configure {
                dependsOn(prepareFixtureAssets)
            }
        }
    }
}
