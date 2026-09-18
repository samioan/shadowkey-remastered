<#
.SYNOPSIS
    Runs the port's smoke-test suite.

.DESCRIPTION
    M113. Until now this runner lived in whatever scratch directory the
    session happened to have, and was rewritten from memory each time -- which
    went wrong at least once (a `$(...)` substitution between the executable
    and the `if ($?)` reading its result, so every test reported failure while
    the code was fine). It is in the repo now because it has to be: CI calls
    it, and because port/build/ has contained a GUI executable since this
    milestone, anything that naively runs every .exe there hangs forever
    waiting for a window to close.

    Tests take the game-data root as argv[1] and resolve it against the
    current directory, so this runs from the repository root.

.PARAMETER DataRoot
    Passed to each test as argv[1]. Empty by default, and deliberately so:
    the tests do not share one argv convention. Most take the game-data root
    and already default to the in-repo path relative to the current
    directory, but m15_font_smoke takes a *font* path there and
    m2_simkin_smoke takes a script, so handing every test the same string
    makes those fail for no reason. Set it only when every test being run
    agrees on what it means.

.PARAMETER NoGameData
    Run only the tests that need no game data -- what a CI machine, which
    has neither the dump nor the Nokia font, can actually verify.

.PARAMETER Filter
    Wildcard against the test name, e.g. -Filter 'menu*'.

.EXAMPLE
    ./port/run_tests.ps1
    ./port/run_tests.ps1 -NoGameData
#>
[CmdletBinding()]
param(
    [string] $DataRoot = "",
    [switch] $NoGameData,
    [string] $Filter = "*",
    [string] $BuildDir = "port/build"
)

$ErrorActionPreference = 'Stop'

# Not tests, and each for its own reason:
#   Shadowkey        the launcher -- a GUI app that would wait for a window
#   shadowkey_port   the game itself, likewise
#   render_at_smoke  a rendering *tool*, not a pass/fail check: it writes
#                    .ppm files and says nothing about whether they are right
#   scratch_check_rat  a one-off investigation left in the tree
$skip = @('Shadowkey', 'shadowkey_port', 'render_at_smoke', 'scratch_check_rat')

# The tests that read nothing outside the repository, so they still mean
# something on a machine that has neither the dump nor Ceurope.gdr.
# Deliberately not font_smoke: it needs no *game* data but it does need the
# Nokia font, which .gitignore keeps out of the repo, so on CI it would fail
# for a reason that is not a bug. launcher_smoke belongs here because it
# skips its own dump- and font-dependent checks when those are absent.
$noDataTests = @('aspect_viewport_smoke', 'save_archive_smoke',
                 'debug_suite_smoke', 'launcher_smoke')

if (-not (Test-Path $BuildDir)) {
    Write-Error "$BuildDir does not exist -- run port/build.bat first."
}

$executables = Get-ChildItem -Path $BuildDir -Filter *.exe |
    Where-Object { $skip -notcontains $_.BaseName } |
    Where-Object { $_.BaseName -like $Filter } |
    Sort-Object BaseName

if ($NoGameData) {
    $executables = $executables | Where-Object { $noDataTests -contains $_.BaseName }
}

$passed = 0
$failed = 0
$softFails = 0
$failedNames = @()

# From here on a test's own stderr must not be fatal. Windows PowerShell
# wraps every stderr line from a native executable in an ErrorRecord, so
# with $ErrorActionPreference = 'Stop' the first test that writes anything
# to stderr aborts the entire run -- not that test, the run. It happened
# immediately: WIC prints "Unsupported marker type 0xd9" while
# launcher_smoke feeds it deliberately corrupt image data, which is a
# passing check, and the suite stopped dead on it.
#
# Exit codes are what decides pass or fail here, so stderr is just text.
$ErrorActionPreference = 'Continue'

foreach ($exe in $executables) {
    if ([string]::IsNullOrEmpty($DataRoot)) {
        $output = & $exe.FullName 2>&1 | Out-String
    } else {
        $output = & $exe.FullName $DataRoot 2>&1 | Out-String
    }
    # Read this on the very next line. Anything in between -- even a
    # subexpression that only counts something -- overwrites it with its own
    # result, which is exactly the bug this script exists to stop repeating.
    $rc = $LASTEXITCODE

    $softFails += ([regex]::Matches($output, '\[soft-fail\]')).Count

    if ($rc -eq 0) {
        $passed++
    } else {
        $failed++
        $failedNames += $exe.BaseName
        Write-Host "--- $($exe.BaseName) (exit $rc) ---" -ForegroundColor Red
        # The tail is where the summary line and the failing checks are.
        ($output -split "`n" | Select-Object -Last 20) | ForEach-Object {
            Write-Host "    $($_.TrimEnd())"
        }
    }
}

$total = $passed + $failed
Write-Host "passed=$passed failed=$failed total=$total soft-fail=$softFails"

# A run that found nothing to run is a failure, not a pass. Without this a
# CI job whose build step silently produced no executables reports green.
if ($total -eq 0) {
    Write-Host "no test executables matched -- nothing was verified" -ForegroundColor Red
    exit 1
}
if ($failed -gt 0) {
    Write-Host "FAILED: $($failedNames -join ' ')" -ForegroundColor Red
    exit 1
}
exit 0
