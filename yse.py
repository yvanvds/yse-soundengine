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
import datetime
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
IS_WINDOWS = platform.system() == "Windows"

# Anchored to the exact form in YseEngine/system.hpp so we never edit the wrong
# constant. Captures: (prefix)(major).(minor).(patch)(suffix)
VERSION_RE = re.compile(
    r'^(\s*const\s+std::string\s+VERSION\s*=\s*")'
    r'(\d+)\.(\d+)\.(\d+)'
    r'("\s*;\s*)$',
    re.MULTILINE,
)
VERSION_FILE = ROOT / "YseEngine" / "system.hpp"
REPO_URL = "https://github.com/yvanvds/yse-soundengine"

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
    # Find compile_commands.json. Prefer build-tests because it includes both
    # engine and test sources; build-debug omits the Tests/ targets and would
    # fail clang-tidy on test files with "doctest/doctest.h not found".
    candidates = [
        ROOT / "build-tests" / "compile_commands.json",
        ROOT / "build-debug" / "compile_commands.json",
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
# Release / package helpers
# ---------------------------------------------------------------------------

def _read_version():
    """Parse (major, minor, patch) from YseEngine/system.hpp.

    Returns (text, match, (major, minor, patch))."""
    if not VERSION_FILE.exists():
        print(f"error: {VERSION_FILE} not found.")
        sys.exit(1)
    text = VERSION_FILE.read_text(encoding="utf-8")
    m = VERSION_RE.search(text)
    if not m:
        print(f"error: could not find `const std::string VERSION = \"X.Y.Z\";` in {VERSION_FILE}.")
        sys.exit(1)
    return text, m, (int(m.group(2)), int(m.group(3)), int(m.group(4)))


def _version_string(triple):
    return f"{triple[0]}.{triple[1]}.{triple[2]}"


def _git_branch():
    return subprocess.check_output(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=str(ROOT), text=True
    ).strip()


def _git_tag_exists(tag):
    return subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", tag],
        cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0


def cmd_release(args):
    text, m, (major, minor, patch) = _read_version()
    if args.level == "major":
        new = (major + 1, 0, 0)
    elif args.level == "minor":
        new = (major, minor + 1, 0)
    else:  # patch
        new = (major, minor, patch + 1)

    cur_str = _version_string((major, minor, patch))
    new_str = _version_string(new)
    tag = f"v{new_str}"

    print(f"Current version: {cur_str}")
    print(f"New version:     {new_str}")
    print(f"Git tag:         {tag}")

    if args.dry_run:
        print("\n--dry-run: not writing, committing, or pushing.")
        return

    # Pre-flight: branch must be master or dev (the two branches CI watches).
    branch = _git_branch()
    if branch not in ("master", "dev"):
        print(f"\nerror: current branch is {branch!r}; releases must come from 'master' or 'dev'.")
        sys.exit(1)

    # Pre-flight: working tree must be clean.
    status = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=str(ROOT), text=True
    )
    if status.strip():
        print("\nerror: working tree is dirty. Commit or stash before releasing:")
        print(status)
        sys.exit(1)

    # Pre-flight: tag must not exist locally or on origin.
    if _git_tag_exists(tag):
        print(f"\nerror: tag {tag} already exists locally.")
        sys.exit(1)

    # Rewrite the VERSION line in-place, preserving surrounding whitespace.
    new_line = f'{m.group(1)}{new[0]}.{new[1]}.{new[2]}{m.group(5)}'
    new_text = text[:m.start()] + new_line + text[m.end():]
    VERSION_FILE.write_text(new_text, encoding="utf-8")
    print(f"\nUpdated {VERSION_FILE.relative_to(ROOT)}.")

    run(["git", "add", str(VERSION_FILE)], cwd=ROOT)
    run(["git", "commit", "-m", f"Release {tag}"], cwd=ROOT)
    run(["git", "tag", tag], cwd=ROOT)

    if args.no_push:
        print(f"\n--no-push: stopped before push. To publish:")
        print(f"    git push && git push origin {tag}")
        return

    run(["git", "push"], cwd=ROOT)
    run(["git", "push", "origin", tag], cwd=ROOT)
    print(f"\nReleased {tag}.")
    print(f"CI release workflow:")
    print(f"    {REPO_URL}/actions")


# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------

def _collect_mingw_dlls(dll_path, mingw_prefix):
    """Walk libyse.dll's PE imports recursively and return every dep DLL that
    lives under <mingw_prefix>/bin/.  System DLLs (KERNEL32.dll, etc.) are
    filtered out automatically because they don't live in MINGW_PREFIX."""
    objdump = shutil.which("objdump")
    if objdump is None:
        print("error: objdump not found on PATH; cannot enumerate runtime DLLs.")
        sys.exit(1)

    bin_dir = Path(mingw_prefix) / "bin"
    if not bin_dir.is_dir():
        print(f"error: MINGW_PREFIX/bin not found at {bin_dir}.")
        sys.exit(1)

    found = set()                   # Path objects of MinGW DLLs to ship
    seen_names = {dll_path.name.lower()}
    queue = [dll_path]
    while queue:
        cur = queue.pop()
        try:
            out = subprocess.check_output(
                [objdump, "-p", str(cur)], text=True, errors="replace"
            )
        except subprocess.CalledProcessError:
            continue
        for line in out.splitlines():
            line = line.strip()
            if not line.startswith("DLL Name:"):
                continue
            dep_name = line.split(":", 1)[1].strip()
            key = dep_name.lower()
            if key in seen_names:
                continue
            seen_names.add(key)
            candidate = bin_dir / dep_name
            if candidate.exists():
                found.add(candidate)
                queue.append(candidate)
    return sorted(found)


def _make_release_readme(version, platform_name, dlls):
    today = datetime.date.today().isoformat()
    if platform_name == "windows":
        layout = (
            "    include/        Public C++ headers (use `#include \"yse.hpp\"`)\n"
            "    bin/            libyse.dll + bundled runtime dependencies\n"
            "    lib/            libyse.dll.a (MinGW import library)"
        )
        deps_section = (
            "All runtime dependencies are bundled in `bin/`. Just drop the contents of\n"
            "`bin/` next to your executable (or add `bin/` to `PATH`). The bundled DLLs\n"
            "come from MSYS2 Clang64 and include:\n\n"
        )
        deps_section += "\n".join(f"- `{d.name}`" for d in dlls) if dlls else "- (none — see bin/)"
        link_hint = (
            "Compile: `-I<archive>/include`\n"
            "Link:    `-L<archive>/lib -lyse`\n"
            "Run:     ensure `<archive>/bin/` is on `PATH` or copy its contents next to your `.exe`."
        )
    else:  # linux
        layout = (
            "    include/        Public C++ headers (use `#include \"yse.hpp\"`)\n"
            "    lib/            libyse.so"
        )
        deps_section = (
            "Install the system runtime packages:\n\n"
            "```sh\n"
            "# Debian/Ubuntu\n"
            "sudo apt install libportaudio2 libsndfile1 librtmidi6\n\n"
            "# Fedora/RHEL\n"
            "sudo dnf install portaudio libsndfile rtmidi\n"
            "```"
        )
        link_hint = (
            "Compile: `-I<archive>/include`\n"
            "Link:    `-L<archive>/lib -lyse`\n"
            "Run:     ensure `<archive>/lib/` is on `LD_LIBRARY_PATH` (or install libyse.so system-wide)."
        )

    return f"""# libYSE {version} — {platform_name} x64

A cross-platform sound engine written in C++. This archive contains the
prebuilt shared library and public headers for **{platform_name} x86_64**.

- Repository:   <{REPO_URL}>
- Version:      {version}
- Built:        {today}
- Architecture: x86_64

## Contents

```
{layout}
    LICENSE.md      Eclipse Public License v2.0
    README.md       This file
```

## Runtime dependencies

{deps_section}

## Using libYSE

{link_hint}

See the [project README]({REPO_URL}#readme) and
[GitHub Issues]({REPO_URL}/issues) for build instructions, the full API
surface, and known issues.

## License

libYSE is distributed under the Eclipse Public License, v 2.0. See
`LICENSE.md` for the full text.
"""


def cmd_package(args):
    _, _, version_triple = _read_version()
    version = _version_string(version_triple)

    # Resolve target platform.
    requested = args.platform
    if requested == "auto":
        platform_name = "windows" if IS_WINDOWS else "linux"
    else:
        platform_name = requested

    # Resolve source build directory.
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (ROOT / build_dir).resolve()
    bin_dir = build_dir / "bin"
    lib_dir_src = build_dir / "lib"

    if not bin_dir.is_dir():
        print(f"error: {bin_dir} not found. Run 'python yse.py build --release' first.")
        sys.exit(1)

    # Locate the built library.
    if platform_name == "windows":
        dll = bin_dir / "libyse.dll"
        import_lib = lib_dir_src / "libyse.dll.a"
        if not dll.exists():
            print(f"error: {dll} not found.")
            sys.exit(1)
        if not import_lib.exists():
            # MSVC fallback path: yse.lib in lib/, yse.dll in bin/. Not the
            # configuration we ship, but warn rather than miss it silently.
            print(f"warning: {import_lib} not found; archive will lack an import library.")
    else:
        shared = bin_dir / "libyse.so"
        if not shared.exists():
            print(f"error: {shared} not found.")
            sys.exit(1)

    # Prepare staging directory.
    out_root = Path(args.out_dir)
    if not out_root.is_absolute():
        out_root = (ROOT / out_root).resolve()
    pkg_name = f"libyse-v{version}-{platform_name}-x64"
    stage = out_root / pkg_name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    # Copy headers — every .hpp under YseEngine/, preserving subdirectory layout.
    src_engine = ROOT / "YseEngine"
    include_root = stage / "include"
    header_count = 0
    for hpp in sorted(src_engine.rglob("*.hpp")):
        rel = hpp.relative_to(src_engine)
        target = include_root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(hpp, target)
        header_count += 1
    print(f"Copied {header_count} header(s) into include/.")

    # Copy the library + (Windows only) bundled runtime DLLs.
    bundled = []
    if platform_name == "windows":
        out_bin = stage / "bin"
        out_lib = stage / "lib"
        out_bin.mkdir()
        out_lib.mkdir()
        shutil.copy2(dll, out_bin / dll.name)
        print(f"Copied {dll.name} into bin/.")
        if import_lib.exists():
            shutil.copy2(import_lib, out_lib / import_lib.name)
            print(f"Copied {import_lib.name} into lib/.")

        mingw_prefix = os.environ.get("MINGW_PREFIX")
        if not mingw_prefix:
            # On a regular Windows shell (cmd/powershell) MINGW_PREFIX is unset.
            # Try a sensible default for MSYS2 CLANG64.
            mingw_prefix = "C:/msys64/clang64"
            print(f"note: MINGW_PREFIX not set; defaulting to {mingw_prefix}")
        elif mingw_prefix.startswith("/"):
            # Inside an MSYS2 shell, MINGW_PREFIX is a POSIX path ("/clang64").
            # The Windows-native Python can't dereference that — convert with cygpath.
            cygpath = shutil.which("cygpath")
            if cygpath is not None:
                try:
                    mingw_prefix = subprocess.check_output(
                        [cygpath, "-w", mingw_prefix], text=True
                    ).strip()
                except subprocess.CalledProcessError:
                    pass
        bundled = _collect_mingw_dlls(out_bin / dll.name, mingw_prefix)
        for d in bundled:
            shutil.copy2(d, out_bin / d.name)
        print(f"Bundled {len(bundled)} runtime DLL(s) into bin/.")
    else:
        out_lib = stage / "lib"
        out_lib.mkdir()
        shutil.copy2(shared, out_lib / shared.name)
        print(f"Copied {shared.name} into lib/.")

    # LICENSE.md
    license_src = ROOT / "LICENSE.md"
    if not license_src.exists():
        print(f"error: {license_src} not found.")
        sys.exit(1)
    shutil.copy2(license_src, stage / "LICENSE.md")

    # Generated README.md
    (stage / "README.md").write_text(
        _make_release_readme(version, platform_name, bundled),
        encoding="utf-8",
    )

    # Archive — .zip on Windows, .tar.gz on Linux.
    if platform_name == "windows":
        archive = out_root / f"{pkg_name}.zip"
        if archive.exists():
            archive.unlink()
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    zf.write(path, arcname=path.relative_to(out_root))
    else:
        archive = out_root / f"{pkg_name}.tar.gz"
        if archive.exists():
            archive.unlink()
        with tarfile.open(archive, "w:gz") as tf:
            tf.add(stage, arcname=pkg_name)

    print(f"\nPackage written to {archive}")
    print(f"Staging dir:       {stage}")




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
  python yse.py package            build a release archive in dist/ (used by CI)
  python yse.py package --platform linux    explicit target platform
  python yse.py release patch      bump VERSION, commit, tag vX.Y.Z, push
  python yse.py release minor --dry-run     preview without writing
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

    # package
    p = sub.add_parser(
        "package",
        help="Build a release archive (headers + shared lib) under dist/",
        description=(
            "Stages a release tree under dist/libyse-vX.Y.Z-<platform>-x64/ and "
            "produces a zip (Windows) or tar.gz (Linux) of the same.  The shared "
            "library is read from <build-dir>/bin/ — run `python yse.py build "
            "--release` first to populate it.\n\n"
            "Windows packages bundle every runtime DLL libyse.dll depends on "
            "(walked from objdump -p, filtered to MINGW_PREFIX/bin/) so the zip "
            "is drop-in on any Windows machine.  Linux packages do not bundle "
            "deps — consumers install libportaudio2/libsndfile1/librtmidi6 from "
            "their distro package manager.\n\n"
            "The version is read from YseEngine/system.hpp (`VERSION = \"X.Y.Z\"`)."
        ),
    )
    p.add_argument(
        "--platform", choices=("auto", "linux", "windows"), default="auto",
        help="Target platform (default: derived from the host OS)",
    )
    p.add_argument(
        "--build-dir", default="build",
        help="CMake build directory whose bin/ holds libyse.* (default: build)",
    )
    p.add_argument(
        "--out-dir", default="dist",
        help="Where to stage the package and write the archive (default: dist)",
    )
    p.set_defaults(func=cmd_package)

    # release
    p = sub.add_parser(
        "release",
        help="Bump VERSION in system.hpp, commit, tag, push",
        description=(
            "Bumps the version string in YseEngine/system.hpp by one component "
            "(patch/minor/major), commits the change as `Release vX.Y.Z`, tags "
            "the commit `vX.Y.Z`, and pushes both branch and tag.  The tag push "
            "triggers the release workflow at .github/workflows/release.yml, "
            "which builds Windows and Linux archives and publishes a GitHub "
            "Release.\n\n"
            "Pre-flight checks: current branch must be 'master' or 'dev', the "
            "working tree must be clean, and the new tag must not already exist."
        ),
    )
    p.add_argument(
        "level", choices=("patch", "minor", "major"),
        help="Which version component to bump",
    )
    p.add_argument(
        "--dry-run", action="store_true",
        help="Print the planned bump without writing, committing, or pushing",
    )
    p.add_argument(
        "--no-push", action="store_true",
        help="Commit and tag locally, but do not push to origin",
    )
    p.set_defaults(func=cmd_release)

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
