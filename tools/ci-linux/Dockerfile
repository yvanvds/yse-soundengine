# Reproduces the SonarQube CI build environment for local testing.
# Mirrors .github/workflows/build.yml step-for-step so the patcher-hang and
# any other Linux-specific test failures can be reproduced from Windows.
#
# Usage from PowerShell at repo root:
#   docker build -t yse-ci -f tools/ci-linux/Dockerfile .
#   docker run --rm -v ${PWD}:/workspace yse-ci
# Or use tools/ci-linux/run.ps1.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        portaudio19-dev \
        libsndfile1-dev \
        librtmidi-dev \
        gcovr \
        ca-certificates \
        git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Default command: configure + build + test, same as the CI job.
# `build-linux/` keeps the bind-mounted build dir separate from the Windows
# `build/` so artifacts from the two toolchains don't collide.
CMD ["bash", "-c", "set -ex && \
    cmake -B build-linux -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DYSE_ENABLE_COVERAGE=ON && \
    cmake --build build-linux && \
    ctest --test-dir build-linux --output-on-failure"]
