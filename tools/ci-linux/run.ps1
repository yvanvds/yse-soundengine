# Build and run the Linux CI reproduction container.
#
# From repo root:
#   .\tools\ci-linux\run.ps1            # full configure + build + ctest pipeline
#   .\tools\ci-linux\run.ps1 -Shell     # interactive bash shell in the container
#   .\tools\ci-linux\run.ps1 -Rebuild   # force re-build of the image

[CmdletBinding()]
param(
    [switch]$Shell,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

Push-Location $repoRoot
try {
    docker image inspect yse-ci 2>$null | Out-Null
    $imageExists = ($LASTEXITCODE -eq 0)

    if ($Rebuild -or -not $imageExists) {
        docker build -t yse-ci -f tools/ci-linux/Dockerfile .
        if ($LASTEXITCODE -ne 0) { throw "docker build failed" }
    }

    if ($Shell) {
        docker run --rm -it -v "${repoRoot}:/workspace" --entrypoint bash yse-ci
    } else {
        docker run --rm -v "${repoRoot}:/workspace" yse-ci
    }
} finally {
    Pop-Location
}
