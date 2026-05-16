#!/usr/bin/env python3
"""YSE Sound Engine — development workflow CLI.

Wraps cmake --preset / ctest --preset calls so you don't need to remember
preset names or build-directory paths.  On Windows run via:

    python yse.py <command>

On Unix you can also chmod +x yse.py and use ./yse.py <command>.

All commands print the underlying cmake/ctest invocation before running it,
so the behaviour is fully transparent.  Exit codes from the underlying tool
are propagated unchanged.
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
IS_WINDOWS = platform.system() == "Windows"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _print_cmd(cmd, cwd=None):
    if cwd:
        print(f"+ cd {cwd}", flush=True)
    print("+", " ".join(str(c) for c in cmd), flush=True)


def run(cmd, cwd=None):
    """Print and run *cmd*, exiting with the child's return code on failure."""
    _print_cmd(cmd, cwd)
    result = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        sys.exit(result.returncode)


def run_to_file(cmd, output_path, cwd=None):
    """Print and run *cmd*, writing stdout to output_path."""
    _print_cmd(cmd, cwd)
    print(f"  (stdout → {output_path})", flush=True)
    with open(output_path, "w") as f:
        result = subprocess.run(cmd, stdout=f, cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        sys.exit(result.returncode)


def _demo_exe(name):
    """Return the Path to a demo executable in the debug build."""
    suffix = ".exe" if IS_WINDOWS else ""
    return ROOT / "build-debug" / "bin" / (name + suffix)


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_build(args):
    preset = "release" if args.release else "debug"
    run(["cmake", "--preset", preset])
    run(["cmake", "--build", "--preset", preset])


def cmd_test(args):
    run(["cmake", "--preset", "tests-debug"])
    run(["cmake", "--build", "--preset", "tests-debug"])
    run(["ctest", "--preset", "tests-debug"])

    if args.integration:
        suffix = ".exe" if IS_WINDOWS else ""
        exe = ROOT / "build-tests" / "bin" / ("yse_tests" + suffix)
        if not exe.exists():
            print(f"error: {exe} not found after build.")
            sys.exit(1)
        # ctest's DISABLED property on yse_tests_integration blocks the suite
        # even with `-L integration`, so invoke the doctest binary directly.
        # WORKING_DIRECTORY = bin/ matches the per-suite CTest entries.
        bin_dir = exe.parent
        _print_cmd([exe.name, "--test-suite=integration"], cwd=bin_dir)
        result = subprocess.run(
            [str(exe), "--test-suite=integration"], cwd=str(bin_dir)
        )
        if result.returncode != 0:
            sys.exit(result.returncode)


def cmd_bench(args):
    run(["cmake", "--preset", "bench"])
    run(["cmake", "--build", "--preset", "bench"])

    suffix = ".exe" if IS_WINDOWS else ""
    exe = ROOT / "build-bench" / "bin" / ("yse_benchmarks" + suffix)
    if not exe.exists():
        print(f"error: {exe} not found after build.")
        sys.exit(1)

    cmd = [str(exe)]
    if args.filter:
        cmd.append(f"--benchmark_filter={args.filter}")
    if args.json:
        cmd.append("--benchmark_format=json")
    cmd.extend(args.extra)

    # Working dir = bin/ so libyse.dll / libyse.so is found alongside the
    # binary, matching how Tests/ runs.
    bin_dir = exe.parent
    _print_cmd([exe.name] + cmd[1:], cwd=bin_dir)
    result = subprocess.run(cmd, cwd=str(bin_dir))
    sys.exit(result.returncode)


def _cmd_coverage_linux():
    run(["cmake", "--preset", "coverage"])
    run(["cmake", "--build", "--preset", "coverage"])
    run(["ctest", "--preset", "coverage"])

    if shutil.which("gcovr") is None:
        print("\nNote: gcovr not found — skipping coverage report generation.")
        print("Install: pip install gcovr  or  sudo apt install gcovr")
        return

    report = ROOT / "coverage.xml"
    run([
        "gcovr",
        "--root", str(ROOT),
        "--filter", "./YseEngine/",
        "--filter", "./Tests/",
        "--sonarqube",
        "--output", str(report),
    ])
    print(f"\nCoverage report written to {report}")
    print(f"SonarQube property: sonar.coverageReportPaths={report.name}")


def _cmd_coverage_windows():
    for tool in ("llvm-profdata", "llvm-cov"):
        if shutil.which(tool) is None:
            print(f"error: {tool} not found on PATH.")
            print("These ship with the MSYS2 Clang64 compiler package:")
            print("  pacman -S mingw-w64-clang-x86_64-clang")
            sys.exit(1)

    build_dir = ROOT / "build-coverage"
    profdata = build_dir / "coverage.profdata"
    test_exe = build_dir / "bin" / "yse_tests.exe"
    report = ROOT / "coverage-llvm.json"

    run(["cmake", "--preset", "coverage-windows"])
    run(["cmake", "--build", "--preset", "coverage-windows"])

    # LLVM_PROFILE_FILE controls where the instrumented binary writes its raw
    # profile data.  %p expands to PID, keeping per-process files distinct.
    os.environ["LLVM_PROFILE_FILE"] = str(build_dir / "coverage-%p.profraw")
    run(["ctest", "--preset", "coverage-windows"])

    profraw_files = sorted(build_dir.glob("coverage-*.profraw"))
    if not profraw_files:
        print("error: no .profraw files found after running tests.")
        sys.exit(1)

    run(["llvm-profdata", "merge", "-sparse"]
        + [str(f) for f in profraw_files]
        + ["-o", str(profdata)])

    # llvm-cov export writes the JSON to stdout; redirect to report file.
    # SonarQube ingests this via sonar.cfamily.llvm-cov.reportPath.
    run_to_file(
        ["llvm-cov", "export",
         str(test_exe),
         f"--instr-profile={profdata}",
         "--format=json",
         str(ROOT / "YseEngine"),
         str(ROOT / "Tests")],
        output_path=str(report),
    )
    print(f"\nCoverage report written to {report}")
    print(f"SonarQube property: sonar.cfamily.llvm-cov.reportPath={report.name}")


def cmd_coverage(args):
    if IS_WINDOWS:
        _cmd_coverage_windows()
    else:
        _cmd_coverage_linux()


def cmd_run(args):
    demo = args.demo
    exe = _demo_exe(demo)
    if not exe.exists():
        print(f"error: {exe} not found.")
        print(f"Run 'python yse.py build' first, then try again.")
        sys.exit(1)

    # Demos reference audio via hardcoded paths ("../../TestResources/..."),
    # so they must be launched from build-debug/bin/.
    bin_dir = exe.parent
    _print_cmd([exe.name], cwd=bin_dir)
    result = subprocess.run([str(exe)], cwd=str(bin_dir))
    sys.exit(result.returncode)


def cmd_debug(args):
    demo = args.demo
    exe = _demo_exe(demo)
    if not exe.exists():
        print(f"error: {exe} not found.")
        print(f"Run 'python yse.py build' first, then try again.")
        sys.exit(1)

    if shutil.which("lldb") is None:
        print("error: lldb not found on PATH.")
        if IS_WINDOWS:
            print("Install: pacman -S mingw-w64-clang-x86_64-lldb")
        else:
            print("Install: sudo apt install lldb  (or equivalent)")
        sys.exit(1)

    bin_dir = exe.parent
    run(["lldb", str(exe)], cwd=bin_dir)


def cmd_clean(args):
    dirs_to_remove = [
        ROOT / "build",
        ROOT / "build-debug",
        ROOT / "build-tests",
        ROOT / "build-coverage",
        ROOT / "build-bench",
    ]
    files_to_remove = [
        ROOT / "coverage.xml",
        ROOT / "coverage-llvm.json",
    ]

    existing = [p for p in dirs_to_remove + files_to_remove if p.exists()]

    if not existing:
        print("Nothing to clean.")
        return

    if not args.yes:
        print("Will delete:")
        for p in existing:
            kind = "dir " if p.is_dir() else "file"
            print(f"  [{kind}] {p}")
        answer = input("Continue? [y/N] ").strip().lower()
        if answer != "y":
            print("Aborted.")
            return

    for p in existing:
        print(f"Removing {p}")
        if p.is_dir():
            shutil.rmtree(p)
        else:
            p.unlink()

    print("Done.")


def cmd_analyze(args):
    # Find compile_commands.json — prefer debug, fall back to tests or release.
    candidates = [
        ROOT / "build-debug" / "compile_commands.json",
        ROOT / "build-tests" / "compile_commands.json",
        ROOT / "build" / "compile_commands.json",
    ]
    compile_commands_dir = None
    for c in candidates:
        if c.exists():
            compile_commands_dir = c.parent
            break

    if compile_commands_dir is None:
        print("error: compile_commands.json not found.")
        print("Run 'python yse.py build' first to generate it.")
        sys.exit(1)

    clang_tidy = shutil.which("clang-tidy")
    sonar_scanner = shutil.which("sonar-scanner")

    if clang_tidy:
        target = getattr(args, "target", None)
        if target:
            target_path = Path(target)
            if not target_path.is_absolute():
                target_path = (ROOT / target_path).resolve()
            if not target_path.exists():
                print(f"error: target {target} not found.")
                sys.exit(1)
            if target_path.is_file():
                if target_path.suffix != ".cpp":
                    print(f"error: target {target} is not a .cpp file.")
                    sys.exit(1)
                files = [target_path]
            else:
                files = sorted(target_path.rglob("*.cpp"))
        else:
            files = []
            for d in [ROOT / "YseEngine", ROOT / "Tests"]:
                if d.exists():
                    files.extend(sorted(d.rglob("*.cpp")))

        if not files:
            if target:
                print(f"No .cpp files found under {target}.")
            else:
                print("No .cpp files found under YseEngine/ or Tests/.")
            return

        if sonar_scanner:
            print(f"Note: sonar-scanner is also on PATH ({sonar_scanner}) "
                  "and provides a deeper analysis including SonarCloud upload.\n")

        run([clang_tidy, f"-p={compile_commands_dir}"] + [str(f) for f in files])

    elif sonar_scanner:
        print("clang-tidy not found — falling back to sonar-scanner (heavier analysis).")
        run([sonar_scanner])

    else:
        print("Neither clang-tidy nor sonar-scanner found on PATH.")
        if IS_WINDOWS:
            print("  Windows (MSYS2/Clang64):")
            print("    pacman -S mingw-w64-clang-x86_64-clang-tools-extra")
        else:
            print("  Debian/Ubuntu:  sudo apt install clang-tidy")
            print("  Fedora/RHEL:    sudo dnf install clang-tools-extra")
        sys.exit(1)


def cmd_format(args):
    clang_format = shutil.which("clang-format")
    if clang_format is None:
        print("error: clang-format not found on PATH.")
        if IS_WINDOWS:
            print("  Install: pacman -S mingw-w64-clang-x86_64-clang-tools-extra")
        else:
            print("  Install: sudo apt install clang-format  (or equivalent)")
        sys.exit(1)

    config = ROOT / ".clang-format"
    if not config.exists():
        print(f"error: .clang-format not found at {config}.")
        sys.exit(1)

    files = []
    for d in [ROOT / "YseEngine", ROOT / "Tests"]:
        if d.exists():
            for ext in ("*.cpp", "*.hpp", "*.h"):
                files.extend(sorted(d.rglob(ext)))

    if not files:
        print("No source files found to format.")
        return

    run([clang_format, "-i"] + [str(f) for f in files])
    print(f"Formatted {len(files)} file(s).")


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        prog="yse.py",
        description="YSE Sound Engine development workflow CLI.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""examples:
  python yse.py build              configure + build (debug)
  python yse.py build --release    configure + build (release)
  python yse.py test               build tests-debug preset, run ctest
  python yse.py test --integration same, plus the integration suite (needs real audio device)
  python yse.py bench              build bench preset, run benchmark binary
  python yse.py bench --filter Buffer  run only benchmarks matching 'Buffer'
  python yse.py coverage           coverage preset + report (Linux: gcovr→coverage.xml; Windows: llvm-cov→coverage-llvm.json)
  python yse.py run                run Demo00 from build-debug/bin/
  python yse.py run Demo05         run Demo05 (Reverb) from build-debug/bin/
  python yse.py debug Demo00       launch Demo00 under lldb
  python yse.py clean              remove all build directories
  python yse.py clean --yes        same, without confirmation prompt
  python yse.py analyze            run clang-tidy across YseEngine/ and Tests/ (slow)
  python yse.py analyze YseEngine/dsp/     same, limited to one directory
  python yse.py analyze YseEngine/sound.cpp  same, limited to a single file
  python yse.py format             run clang-format on YseEngine/ and Tests/
""",
    )
    sub = parser.add_subparsers(dest="command", metavar="command")
    sub.required = True

    # build
    p = sub.add_parser(
        "build",
        help="Configure and build the project",
        description="Run cmake --preset debug (or release) followed by cmake --build.",
    )
    p.add_argument(
        "--release", action="store_true",
        help="Use the release preset instead of debug",
    )
    p.set_defaults(func=cmd_build)

    # test
    p = sub.add_parser(
        "test",
        help="Build the tests-debug preset and run ctest",
        description=(
            "Configures with YSE_BUILD_TESTS=ON (tests-debug preset), builds, "
            "then runs ctest --preset tests-debug.\n\n"
            "With --integration, additionally runs the integration suite by "
            "invoking yse_tests --test-suite=integration directly.  These tests "
            "are DISABLED in CTest because they require a real audio output "
            "device (and on Windows probe the RtMidi backend), so they only "
            "run when explicitly requested."
        ),
    )
    p.add_argument(
        "--integration", action="store_true",
        help="Also run the integration suite (requires a real audio device)",
    )
    p.set_defaults(func=cmd_test)

    # bench
    p = sub.add_parser(
        "bench",
        help="Build the bench preset and run the benchmark binary",
        description=(
            "Configures with YSE_BUILD_BENCHMARKS=ON (bench preset), builds in "
            "Release mode, then runs yse_benchmarks from build-bench/bin/.  "
            "google-benchmark is fetched on first configure via FetchContent — "
            "no system package required.\n\n"
            "Filter to a subset with --filter (passed to Google Benchmark as "
            "--benchmark_filter, supports regex).  Use --json for machine-readable "
            "output suitable for the CI comparison action.  Anything after `--` "
            "is forwarded verbatim to the binary."
        ),
    )
    p.add_argument(
        "--filter", default=None,
        help="Regex passed to --benchmark_filter (e.g. 'Buffer.*')",
    )
    p.add_argument(
        "--json", action="store_true",
        help="Emit JSON output (--benchmark_format=json)",
    )
    p.add_argument(
        "extra", nargs="*",
        help="Extra arguments forwarded to yse_benchmarks (use -- to separate)",
    )
    p.set_defaults(func=cmd_bench)

    # coverage
    p = sub.add_parser(
        "coverage",
        help="Coverage build + ctest + report (Linux: gcovr; Windows: llvm-cov)",
        description=(
            "Builds with coverage instrumentation, runs ctest, then generates a "
            "coverage report.\n\n"
            "Linux: uses the 'coverage' preset (YSE_ENABLE_COVERAGE=ON, --coverage "
            "flags, GCC/Clang).  Calls gcovr --sonarqube and writes coverage.xml.  "
            "Configure SonarQube with sonar.coverageReportPaths=coverage.xml.\n\n"
            "Windows (MSYS2 Clang64): uses the 'coverage-windows' preset "
            "(YSE_LLVM_COVERAGE=ON, -fprofile-instr-generate/-fcoverage-mapping).  "
            "Merges .profraw files with llvm-profdata, exports JSON with llvm-cov, "
            "and writes coverage-llvm.json.  "
            "Configure SonarQube with sonar.cfamily.llvm-cov.reportPath=coverage-llvm.json.  "
            "Requires llvm-profdata and llvm-cov on PATH (ship with "
            "mingw-w64-clang-x86_64-clang)."
        ),
    )
    p.set_defaults(func=cmd_coverage)

    # run
    p = sub.add_parser(
        "run",
        help="Run a demo from the debug build",
        description=(
            "Runs a demo executable from build-debug/bin/.  "
            "The working directory is set to build-debug/bin/ because demos "
            "reference audio files via hardcoded relative paths "
            "(../../TestResources/...).  "
            "Default demo: Demo00 (plays drone.ogg — the simplest self-contained demo)."
        ),
    )
    p.add_argument(
        "demo", nargs="?", default="Demo00",
        help="Demo name without path or extension (default: Demo00)",
    )
    p.set_defaults(func=cmd_run)

    # debug
    p = sub.add_parser(
        "debug",
        help="Launch a demo under lldb",
        description=(
            "Launches the named demo under lldb from build-debug/bin/.  "
            "Requires lldb on PATH."
        ),
    )
    p.add_argument(
        "demo", nargs="?", default="Demo00",
        help="Demo name without path or extension (default: Demo00)",
    )
    p.set_defaults(func=cmd_debug)

    # clean
    p = sub.add_parser(
        "clean",
        help="Remove build directories and coverage artifacts",
        description=(
            "Removes build/, build-debug/, build-tests/, build-coverage/, "
            "and coverage.xml.  Asks for confirmation unless --yes is passed."
        ),
    )
    p.add_argument(
        "--yes", "-y", action="store_true",
        help="Skip the confirmation prompt",
    )
    p.set_defaults(func=cmd_clean)

    # analyze
    p = sub.add_parser(
        "analyze",
        help="Run clang-tidy (or sonar-scanner) against the source",
        description=(
            "Runs clang-tidy against compile_commands.json.  "
            "With no target, analyzes every .cpp file under YseEngine/ and "
            "Tests/ (slow — full tree).  "
            "Pass a file or directory to narrow the scope, e.g. "
            "'analyze YseEngine/dsp/' or 'analyze YseEngine/sound.cpp'.  "
            "If clang-tidy is not found but sonar-scanner is on PATH, falls back "
            "to sonar-scanner (target argument is ignored in that case).  "
            "If neither is found, prints install hints."
        ),
    )
    p.add_argument(
        "target", nargs="?",
        help="Optional .cpp file or directory to limit analysis to (default: full tree)",
    )
    p.set_defaults(func=cmd_analyze)

    # format
    p = sub.add_parser(
        "format",
        help="Run clang-format -i on YseEngine/ and Tests/",
        description=(
            "Formats all .cpp/.hpp/.h files under YseEngine/ and Tests/ "
            "using the .clang-format file at the repo root.  "
            "Run 'git diff' afterwards to review changes."
        ),
    )
    p.set_defaults(func=cmd_format)

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
