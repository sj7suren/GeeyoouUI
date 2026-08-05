<#
    test-classify-asan.ps1 -- the regression suite for the ASan gate classifier.

    Run it by hand:

        powershell -NoProfile -ExecutionPolicy Bypass -File tools\test-classify-asan.ps1

    Exit code 0 if every fixture in tests\data\asan classifies the way the table
    below says it must, 1 otherwise.

    This is DELIBERATELY NOT wired into verify.bat.  verify.bat is a six-step
    gate with a fixed output shape, and adding a seventh step would change the
    contract every other tool and habit in this repository is built on.  Run it
    when you touch classify-asan.ps1 -- which is the only time it can break.

    The point of this file is the second column.  The classifier decides whether
    a whole class of memory-safety defect reaches a release, and the temptation
    when it goes red on something that "obviously is not ours" is to widen the
    predicate on the spot.  Widen it here first, with the real log text, and the
    next person can see what you traded away.
#>

[CmdletBinding()]
param(
    # -Detail, not -Verbose: CmdletBinding already owns that name.
    [switch] $Detail
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'

$here       = $PSScriptRoot
$classifier = Join-Path $here 'classify-asan.ps1'
$dataDir    = Join-Path (Split-Path -Parent $here) 'tests\data\asan'

# The fixtures were written with paths under this root; pinning it here rather
# than letting the classifier default to the real checkout is what makes them
# work on a machine that cloned somewhere else.
$fixtureRoot = 'E:\Develop\tools\GeeyoouUI'

# 0 = no report at all   1 = ours, gate RED   2 = third-party, [known]   3 = internal
$cases = @(
    @{ File = 'clean.log'; Expect = 0
       Why  = 'A run with no report in it must cost nothing and say nothing.' }

    @{ File = 'third-party-sogou-uaf.log'; Expect = 2
       Why  = 'THE CASE THIS SCRIPT EXISTS FOR, in the shape this build really
               produces.  Our Window::~Window is on the use stack because it is
               what called DestroyWindow, and our gyFree/operator delete are on
               the free stack because tests\framework\Test.cpp replaces the
               global allocator for the whole process.  Both are bystanders.
               The old any-occurrence findstr went red on this.' }

    @{ File = 'third-party-sogou-uaf-stock-thunk.log'; Expect = 2
       Why  = 'Same report from a build with no global-allocator replacement.
               The plumbing skip has to work with and without gyFree.' }

    @{ File = 'ours-uaf-at-use-site.log'; Expect = 1
       Why  = 'The plain case: we read a Widget after we deleted it.' }

    @{ File = 'ours-uaf-at-free-site-only.log'; Expect = 1
       Why  = 'Byte-for-byte the same allocator plumbing as the SogouPY fixture,
               and the use site is third-party too -- the ONLY difference is the
               frame behind the plumbing.  If this and third-party-sogou-uaf.log
               ever agree, the classifier is broken in one direction or the
               other.  Also the case a "both sites must be ours" rule ships.' }

    @{ File = 'ours-uaf-behind-stl-header.log'; Expect = 1
       Why  = 'Innermost frame is inlined MSVC basic_string.  Toolchain frames
               are transparent, so the walk must reach Geeyoou::ListView.  If
               this one ever flips to 2, the transparency list broke and a whole
               class of dangling-std::string defects just went invisible.' }

    @{ File = 'unknown-module-uaf.log'; Expect = 1
       Why  = 'Nothing is symbolised.  Cannot tell -> ours.  Fail closed.' }

    @{ File = 'truncated-report.log'; Expect = 1
       Why  = 'Header, no stacks.  A report we cannot parse is not a pass.' }

    @{ File = 'ours-heap-buffer-overflow.log'; Expect = 1
       Why  = 'Outside the use-after-free family, so the broad pre-existing rule
               applies unchanged: any frame of ours anywhere is ours.' }

    @{ File = 'third-party-heap-buffer-overflow.log'; Expect = 2
       Why  = 'Broad rule, and genuinely nothing of ours in the report.' }

    @{ File = 'ours-segv.log'; Expect = 1
       Why  = 'A kind with only one stack and no free section still gets a
               definite answer.  No report kind falls through to silence.' }

    @{ File = 'mixed-third-party-then-ours.log'; Expect = 1
       Why  = 'Two reports, one each way.  Noise never cancels a real defect.' }
)

$failures = 0
$ran      = 0

foreach ($c in $cases) {
    $path = Join-Path $dataDir $c.File
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host ("[FAIL] {0} -- fixture missing" -f $c.File)
        $failures++
        continue
    }

    $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $classifier `
               -LogPath $path -RepoRoot $fixtureRoot 2>&1
    $rc = $LASTEXITCODE
    $ran++

    if ($rc -eq $c.Expect) {
        Write-Host ("[ ok ] {0}  -> {1}" -f $c.File.PadRight(38), $rc)
        if ($Detail) { $out | ForEach-Object { Write-Host ('         ' + $_) } }
    }
    else {
        $failures++
        Write-Host ("[FAIL] {0}  -> {1}, expected {2}" -f $c.File.PadRight(38), $rc, $c.Expect)
        Write-Host ('       because: ' + ($c.Why -replace '\s+', ' '))
        $out | ForEach-Object { Write-Host ('       | ' + $_) }
    }
}

# A missing-log run must fail closed too -- the gate treats 3 as red.
$out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $classifier `
           -LogPath (Join-Path $dataDir 'no-such-file.log') -RepoRoot $fixtureRoot 2>&1
$rc = $LASTEXITCODE
$ran++
if ($rc -eq 3) {
    Write-Host ("[ ok ] {0}  -> {1}" -f '<missing log>'.PadRight(38), $rc)
}
else {
    $failures++
    Write-Host ("[FAIL] {0}  -> {1}, expected 3" -f '<missing log>'.PadRight(38), $rc)
    $out | ForEach-Object { Write-Host ('       | ' + $_) }
}

Write-Host ''
if ($failures -eq 0) {
    Write-Host ("[ ok ] classify-asan: {0} cases, 0 failures" -f $ran)
    exit 0
}
Write-Host ("[FAIL] classify-asan: {0} cases, {1} failures" -f $ran, $failures)
exit 1
