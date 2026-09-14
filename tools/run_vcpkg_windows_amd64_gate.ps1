# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$BuildRoot = "",
  [string]$SourceRoot = "",
  [string]$Generator = "",
  [ValidateRange(1, 64)]
  [int]$ParallelJobs = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-MmxisfCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Program,
    [Parameter(Mandatory = $true)]
    [string[]]$Arguments
  )

  & $Program @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $Program $Arguments"
  }
}

function Invoke-MmxisfTests {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Directory,
    [Parameter(Mandatory = $true)]
    [string[]]$RuntimeDirectories
  )

  $savedPath = $env:PATH
  try {
    $runtimePath = $RuntimeDirectories -join ";"
    $env:PATH = "$runtimePath;$savedPath"
    Invoke-MmxisfCommand "ctest.exe" @(
      "--test-dir", $Directory,
      "-C", "Release",
      "--output-on-failure",
      "--timeout", "60"
    )
  }
  finally {
    $env:PATH = $savedPath
  }
}

function Assert-MmxisfByteIdentity {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Primary,
    [Parameter(Mandatory = $true)]
    [string]$Reproduction
  )

  if (-not (Test-Path -LiteralPath $Primary -PathType Leaf) -or
      -not (Test-Path -LiteralPath $Reproduction -PathType Leaf)) {
    throw "Reproducibility artifact is missing: $Primary or $Reproduction"
  }
  $primaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Primary).Hash
  $reproductionHash =
    (Get-FileHash -Algorithm SHA256 -LiteralPath $Reproduction).Hash
  if ($primaryHash -ne $reproductionHash) {
    throw "Artifacts are not byte-identical: $Primary and $Reproduction"
  }
}

if ($env:OS -ne "Windows_NT" -or
    -not [Environment]::Is64BitOperatingSystem) {
  throw "This gate requires a native 64-bit Windows host"
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
  $repositoryRoot = (Resolve-Path (Join-Path $scriptDirectory "..")).Path
}
else {
  $repositoryRoot = (Resolve-Path $SourceRoot).Path
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
  throw "Pass -VcpkgRoot or set VCPKG_ROOT to a dedicated vcpkg checkout"
}
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$vcpkgExecutable = Join-Path $VcpkgRoot "vcpkg.exe"
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $vcpkgExecutable -PathType Leaf) -or
    -not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
  throw "The vcpkg checkout must be bootstrapped for Windows"
}

foreach ($commandName in @("cmake.exe", "ctest.exe", "git.exe")) {
  if ($null -eq (Get-Command $commandName -ErrorAction SilentlyContinue)) {
    throw "Missing required command: $commandName"
  }
}

$manifestPath = Join-Path $repositoryRoot "vcpkg.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$expectedBaseline = $manifest.'builtin-baseline'
$actualBaseline = (& git.exe -C $VcpkgRoot rev-parse HEAD 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or
    $expectedBaseline -notmatch '^[0-9a-f]{40}$' -or
    $actualBaseline -ne $expectedBaseline) {
  throw "vcpkg checkout does not match manifest baseline. Expected $expectedBaseline, got $actualBaseline"
}

if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
  $BuildRoot = Join-Path $repositoryRoot "build-vcpkg-windows-amd64"
}
elseif (-not [IO.Path]::IsPathRooted($BuildRoot)) {
  $BuildRoot = [IO.Path]::GetFullPath((Join-Path (Get-Location) $BuildRoot))
}
if (Test-Path -LiteralPath $BuildRoot) {
  throw "Refusing to reuse existing gate directory: $BuildRoot"
}

$installedDirectory = Join-Path $BuildRoot "vcpkg_installed"
$vcpkgRuntime = Join-Path $installedDirectory "x64-windows\bin"
$warningFlags = "/EHsc /W4 /WX /permissive- /Zc:__cplusplus"
$env:VCPKG_DISABLE_METRICS = "1"
$env:VCPKG_ROOT = $VcpkgRoot

function Invoke-MmxisfConfigure {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [Parameter(Mandatory = $true)]
    [string]$Binary,
    [string[]]$ExtraArguments = @()
  )

  $arguments = @(
    "-S", $Source,
    "-B", $Binary,
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows",
    "-DVCPKG_INSTALLED_DIR=$installedDirectory",
    "-DCMAKE_CXX_FLAGS=$warningFlags",
    "-DCMAKE_STATIC_LINKER_FLAGS=/Brepro",
    "-DCMAKE_SHARED_LINKER_FLAGS=/Brepro"
  )
  if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $arguments = @("-G", $Generator) + $arguments
  }
  $arguments += $ExtraArguments
  Invoke-MmxisfCommand "cmake.exe" $arguments
}

function Invoke-MmxisfConsumer {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ConsumerDirectory,
    [Parameter(Mandatory = $true)]
    [string]$InstallDirectory,
    [Parameter(Mandatory = $true)]
    [ValidateSet("static", "shared")]
    [string]$Linkage
  )

  $arguments = @(
    "-DCMAKE_PREFIX_PATH=$InstallDirectory",
    "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$InstallDirectory",
    "-DMMXISF_EXPECTED_LINKAGE=$Linkage"
  )
  if ($Linkage -eq "shared") {
    $arguments += @(
      "--no-warn-unused-cli",
      "-DCMAKE_DISABLE_FIND_PACKAGE_EXPAT=TRUE",
      "-DCMAKE_DISABLE_FIND_PACKAGE_LZ4=TRUE",
      "-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE",
      "-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE",
      "-DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=TRUE"
    )
  }
  Invoke-MmxisfConfigure `
    -Source (Join-Path $repositoryRoot "tests\package_consumer") `
    -Binary $ConsumerDirectory `
    -ExtraArguments $arguments
}

function Invoke-MmxisfVariant {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("static", "shared")]
    [string]$Variant,
    [Parameter(Mandatory = $true)]
    [ValidateSet("ON", "OFF")]
    [string]$Shared
  )

  $buildDirectory = Join-Path $BuildRoot $Variant
  $installDirectory = Join-Path $BuildRoot "install-$Variant"
  $consumerDirectory = Join-Path $BuildRoot "consumer-$Variant"
  $relocatedInstallDirectory =
    Join-Path $BuildRoot "install-$Variant-relocated"
  $relocatedConsumerDirectory =
    Join-Path $BuildRoot "consumer-$Variant-relocated"
  $subdirectoryConsumerDirectory =
    Join-Path $BuildRoot "subdirectory-consumer-$Variant"
  $reproductionDirectory = Join-Path $BuildRoot "reproduction-$Variant"

  Invoke-MmxisfConfigure -Source $repositoryRoot -Binary $buildDirectory `
    -ExtraArguments @(
      "-DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON",
      "-DMMXISF_BUILD_TESTS=ON",
      "-DMMXISF_BUILD_TOOLS=ON",
      "-DMMXISF_BUILD_VIEWER=OFF",
      "-DMMXISF_BUILD_DOCS=OFF",
      "-DBUILD_SHARED_LIBS=$Shared"
    )
  Invoke-MmxisfCommand "cmake.exe" @(
    "--build", $buildDirectory,
    "--config", "Release",
    "--parallel", $ParallelJobs
  )
  Invoke-MmxisfTests $buildDirectory @($vcpkgRuntime)
  Invoke-MmxisfCommand "cmake.exe" @(
    "--install", $buildDirectory,
    "--config", "Release",
    "--prefix", $installDirectory
  )

  Invoke-MmxisfConsumer $consumerDirectory $installDirectory $Variant
  Invoke-MmxisfCommand "cmake.exe" @(
    "--build", $consumerDirectory,
    "--config", "Release",
    "--parallel", $ParallelJobs
  )
  $consumerRuntime = @($vcpkgRuntime)
  if ($Variant -eq "shared") {
    $consumerRuntime = @((Join-Path $installDirectory "bin"), $vcpkgRuntime)
  }
  Invoke-MmxisfTests $consumerDirectory $consumerRuntime

  Invoke-MmxisfCommand "cmake.exe" @(
    "-E", "copy_directory", $installDirectory, $relocatedInstallDirectory
  )
  Invoke-MmxisfConsumer `
    $relocatedConsumerDirectory $relocatedInstallDirectory $Variant
  Invoke-MmxisfCommand "cmake.exe" @(
    "--build", $relocatedConsumerDirectory,
    "--config", "Release",
    "--parallel", $ParallelJobs
  )
  $relocatedRuntime = @($vcpkgRuntime)
  if ($Variant -eq "shared") {
    $relocatedRuntime =
      @((Join-Path $relocatedInstallDirectory "bin"), $vcpkgRuntime)
  }
  Invoke-MmxisfTests $relocatedConsumerDirectory $relocatedRuntime

  Invoke-MmxisfConfigure `
    -Source (Join-Path $repositoryRoot "tests\subdirectory_consumer") `
    -Binary $subdirectoryConsumerDirectory `
    -ExtraArguments @(
      "-DMMXISF_SOURCE_DIR=$repositoryRoot",
      "-DBUILD_SHARED_LIBS=$Shared"
    )
  Invoke-MmxisfCommand "cmake.exe" @(
    "--build", $subdirectoryConsumerDirectory,
    "--config", "Release",
    "--parallel", $ParallelJobs
  )
  $subdirectoryRuntime = @($vcpkgRuntime)
  if ($Variant -eq "shared") {
    $subdirectoryRuntime = @(
      (Join-Path $subdirectoryConsumerDirectory "mmxisf-source\Release"),
      $vcpkgRuntime
    )
  }
  Invoke-MmxisfTests $subdirectoryConsumerDirectory $subdirectoryRuntime

  $buildSbom = Join-Path $buildDirectory "binary-dependencies.spdx.json"
  $installedSbom =
    Join-Path $installDirectory "share\mmxisf\sbom\binary-dependencies.spdx.json"
  if (-not (Test-Path -LiteralPath $buildSbom -PathType Leaf) -or
      -not (Test-Path -LiteralPath $installedSbom -PathType Leaf)) {
    throw "The $Variant binary SBOM is missing"
  }

  Invoke-MmxisfConfigure -Source $repositoryRoot -Binary $reproductionDirectory `
    -ExtraArguments @(
      "-DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON",
      "-DMMXISF_BUILD_TESTS=OFF",
      "-DMMXISF_BUILD_TOOLS=OFF",
      "-DMMXISF_BUILD_EXAMPLES=OFF",
      "-DMMXISF_BUILD_VIEWER=OFF",
      "-DMMXISF_BUILD_DOCS=OFF",
      "-DMMXISF_INSTALL=OFF",
      "-DBUILD_SHARED_LIBS=$Shared"
    )
  Invoke-MmxisfCommand "cmake.exe" @(
    "--build", $reproductionDirectory,
    "--config", "Release",
    "--target", "mmxisf",
    "--parallel", $ParallelJobs
  )

  $primaryLibrary = Join-Path $buildDirectory "Release\mmxisf.lib"
  $reproducedLibrary =
    Join-Path $reproductionDirectory "Release\mmxisf.lib"
  Assert-MmxisfByteIdentity $primaryLibrary $reproducedLibrary
  if ($Variant -eq "shared") {
    Assert-MmxisfByteIdentity `
      (Join-Path $buildDirectory "Release\mmxisf.dll") `
      (Join-Path $reproductionDirectory "Release\mmxisf.dll")
  }
}

Push-Location $repositoryRoot
try {
  Invoke-MmxisfCommand $vcpkgExecutable @(
    "install",
    "--triplet=x64-windows",
    "--x-manifest-root=$repositoryRoot",
    "--x-install-root=$installedDirectory"
  )
  Invoke-MmxisfVariant "static" "OFF"
  Invoke-MmxisfVariant "shared" "ON"
}
finally {
  Pop-Location
}

Write-Host "mmxisf vcpkg native Windows amd64 MSVC gate: PASS"
