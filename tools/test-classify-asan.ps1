<#
    test-classify-asan.ps1 -- the regression suite for the ASan gate classifier.

    Run it by hand:

        powershell -NoProfile -ExecutionPolicy Bypass -File tools\test-classify-asan.ps1

    Exit code 0 if every fixture in tests\data\asan classifies the way the table
    below says it must, 1 otherwise.

    ----------------------------------------------------------------------------
    THIS RUNS INSIDE verify.bat
    ----------------------------------------------------------------------------
    It is the FIRST thing step [6/6] does, before the classifier is pointed at
    the real log; see :classify_asan.  A self-test failure prints

        [FAIL] ASan classifier self-test failed; this leg's verdict is not
               trustworthy

    and reddens the gate WITHOUT running the classification at all, because a
    classifier that fails its own fixtures has no verdict worth reading.

    It used to sit outside the gate, on the grounds that verify.bat is a six-step
    pipeline with a fixed output shape and this would have been a seventh step.
    That was a real constraint applied to the wrong thing.  The output shape is a
    CONVENTION -- and it is preserved: still six banners, still the same summary
    table, still the same exit-code meanings, with one extra line inside step 6.
    The component that decides whether the memory-safety leg can speak, not being
    checked by the gate it gates, is a DEFECT.  Conventions yield to defects.

    ----------------------------------------------------------------------------
    WALL CLOCK, AND WHY THE CASES DO NOT EACH GET THEIR OWN PROCESS
    ----------------------------------------------------------------------------
    Cold-starting powershell.exe costs ~0.6s.  One process per fixture put the
    old 13-case suite at 8.5s, which is most of the budget for something that
    runs on every single gate invocation, and it got worse with every fixture
    added -- i.e. the design punished exactly the behaviour the freeze rule
    demands (add a fixture before you touch a predicate).

    So the table below runs IN PROCESS: classify-asan.ps1 is dot-sourced once
    with -DefineOnly and Invoke-ClassifyAsan is called directly per fixture.

    That skips one thing -- the `exit` statement and $LASTEXITCODE, which is the
    only part verify.bat actually reads -- so that part is covered separately,
    by $exitCodePlumbing below: a handful of fixtures driven through the real
    `powershell -File` path, one for each of the four exit codes.  Four spawns,
    constant cost, and the table can now grow to a hundred cases for free.

    ----------------------------------------------------------------------------
    THE POINT OF THIS FILE IS THE SECOND COLUMN
    ----------------------------------------------------------------------------
    The classifier decides whether a whole class of memory-safety defect reaches
    a release, and the temptation when it goes red on something that "obviously
    is not ours" is to widen the predicate on the spot.

    NO PREDICATE IN classify-asan.ps1 MAY BE WIDENED WITHOUT A CASE HERE FIRST,
    carrying the REAL log text.  Write the case, watch it fail, then change the
    rule.  The next person can then see what you traded away.
#>

[CmdletBinding()]
param(
    # -Detail, not -Verbose: CmdletBinding already owns that name.
    [switch] $Detail,

    # One line of output on success instead of a table.  verify.bat uses this;
    # on failure everything is printed regardless of this switch.
    [switch] $Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'

$here       = $PSScriptRoot
$classifier = Join-Path $here 'classify-asan.ps1'
$dataDir    = Join-Path (Split-Path -Parent $here) 'tests\data\asan'

# Absolute path, for the same reason verify.bat uses one: a bare `powershell.exe`
# resolves through PATH, and the whole point of the gate pinning the interpreter
# is defeated if its own self-test then picks up whatever a PATH edit put first.
$pwsh = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

# The fixtures were written with paths under this root; pinning it here rather
# than letting the classifier default to the real checkout is what makes them
# work on a machine that cloned somewhere else.
$fixtureRoot = 'E:\Develop\tools\GeeyoouUI'

# 0 = no report at all   1 = ours, gate RED   2 = third-party, [known]   3 = internal
#
# Expect  -- the exit code.
# Match   -- regexes that MUST appear in the printed output.  A verdict with no
#            exit code of its own (the shadow ownership line) is only tested if
#            the text is asserted on.
# Reject  -- regexes that must NOT appear.
$cases = @(
    @{ File = 'clean.log'; Expect = 0
       Why  = 'A run with no report in it must cost nothing and say nothing.' }

    @{ File = 'third-party-sogou-uaf.log'; Expect = 2
       Why  = 'THE CASE THIS SCRIPT EXISTS FOR, in the shape this build really
               produces.  Our Window::~Window is on the use stack because it is
               what called DestroyWindow, and our gyFree/operator delete are on
               the free stack because tests\framework\Test.cpp replaces the
               global allocator for the whole process.  Both are bystanders.
               The old any-occurrence findstr went red on this.'
       Reject = @('SHADOW')
       WhyReject = 'And the ownership judgment must not drag it back in: the
                    allocation stack crosses the same plumbing and lands on
                    SogouPY.ime, so there is nothing of ours at any of the three
                    ends.  A SHADOW marker here would mean phase B reopens
                    exactly the flaky red this classifier was written to kill.' }

    @{ File = 'third-party-sogou-uaf-stock-thunk.log'; Expect = 2
       Why  = 'Same report from a build with no global-allocator replacement.
               The plumbing skip has to work with and without gyFree.  This
               fixture also carries its suite-summary line in GBK rather than
               UTF-8, which is what keeps the integrity sentinel honest about
               matching the shape of that line rather than its bytes.' }

    @{ File = 'ours-uaf-nested-delete.log'; Expect = 1
       Why  = 'D1.  A nested destructor puts a SECOND operator delete frame
               FURTHER OUT than the real free site.  The old "scan the first 8
               frames for the outermost operator frame" skipped straight past
               our Widget::~Widget at #3 and blamed the third-party frame at #5
               -- a false GREEN on a use-after-free of ours, which is the one
               direction this classifier may never fail in.  The anchored skip
               stops at the first frame from a different file, so #3 ends the
               plumbing run and takes the blame.  If this ever reads 2 again,
               somebody reintroduced a lookahead window.' }

    @{ File = 'ours-uaf-at-use-site.log'; Expect = 1
       Why  = 'The plain case: we read a Widget after we deleted it.' }

    @{ File = 'ours-uaf-at-free-site-only.log'; Expect = 1
       Why  = 'Byte-for-byte the same allocator plumbing as the SogouPY fixture,
               and the use site is third-party too -- the ONLY difference is the
               frame behind the plumbing.  If this and third-party-sogou-uaf.log
               ever agree, the classifier is broken in one direction or the
               other.  Also the case a "both sites must be ours" rule ships.' }

    @{ File = 'ours-alloc-thirdparty-both-ends.log'; Expect = 2
       Why  = 'D2 phase A.  Use site user32, free site the IME, allocation site
               OURS -- SetWindowTextW on a string we new-ed and something else
               freed.  The exit code stays 2 because ownership is in SHADOW mode
               and shadow judgments do not fail builds.  The day this reads 1,
               check the phase B evidence in the header of classify-asan.ps1 was
               actually collected; promoting it on a hunch is how the SogouPY
               noise gets back in.'
       Match = @(
           'alloc:.*geeyoou::Label::Label.*\[Ours\]',
           'SHADOW: phase B would call this report OURS'
       )
       WhyMatch = 'The whole deliverable of phase A is these two lines.  They
                   have no exit code, so without this assertion the feature is
                   untested and would rot silently.' }

    @{ File = 'ours-uaf-behind-stl-header.log'; Expect = 1
       Why  = 'Innermost frame is inlined MSVC basic_string.  Toolchain frames
               are transparent, so the walk must reach Geeyoou::ListView.  If
               this one ever flips to 2, the transparency list broke and a whole
               class of dangling-std::string defects just went invisible.' }

    @{ File = 'unknown-module-uaf.log'; Expect = 1
       Why  = 'Nothing is symbolised.  Cannot tell -> ours.  Fail closed.' }

    @{ File = 'truncated-report.log'; Expect = 1
       Why  = 'A COMPLETE run carrying an INCOMPLETE report: header, no stacks.
               A report we cannot parse is not a pass.  Contrast
               no-suite-summary.log, which is the other half of this question.' }

    @{ File = 'no-suite-summary.log'; Expect = 3
       Why  = 'D4b.  The run died before the suite finished -- which is what
               happens the day the toolchain drops continue_on_error, since the
               first report on this machine is the IME s and it fires mid-run.
               No ASan text survived, so the classifier used to answer "no
               reports found" = 0 and the gate went GREEN for a run that never
               happened.  The sentinel must fire BEFORE the report count is
               consulted; if this ever reads 0, it was moved or removed.'
       Match = @('continue_on_error')
       WhyMatch = 'A red gate that does not name the thing to check gets muted.
                   The message is load bearing, not decoration.' }

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

# Driven through the REAL `powershell -File` path, so that the exit statement
# and $LASTEXITCODE -- the only part of this script verify.bat consumes -- are
# covered for every code the gate distinguishes.  One fixture per code; adding
# more here costs 0.6s each, adding more to $cases costs nothing.
$exitCodePlumbing = @(
    @{ File = 'clean.log';                       Expect = 0 }
    @{ File = 'ours-uaf-at-use-site.log';        Expect = 1 }
    @{ File = 'third-party-sogou-uaf.log';       Expect = 2 }
    @{ File = 'no-suite-summary.log';            Expect = 3 }
)

$failures = 0
$ran      = 0
$report   = New-Object System.Collections.ArrayList

function Add-Line { param([string] $Text) [void] $report.Add($Text) }

# ---------------------------------------------------------------------------
# Load the classifier as a library.  A syntax error in it lands here, and an
# exception out of this is a failed self-test, which is a red gate.  Correct
# direction: an unloadable classifier has no verdict.
#
# MIND THE SCOPE.  Dot-sourcing runs the other script's param() block IN THIS
# SCOPE, and classify-asan.ps1 declares $Quiet and $LogPath of its own -- so the
# dot-source silently overwrites OUR $Quiet with $false.  Measured: -Quiet was
# accepted, ignored, and the gate printed the whole 21-line table inside step
# [6/6] while looking like it had been asked not to.  Read our switches out
# first and never touch $Quiet again below.
# ---------------------------------------------------------------------------
$wantQuiet  = [bool] $Quiet
$wantDetail = [bool] $Detail

try {
    . $classifier -DefineOnly
}
catch {
    Write-Host ('[FAIL] classify-asan.ps1 could not even be loaded: ' + $_.Exception.Message)
    Write-Host ('       ' + $_.InvocationInfo.PositionMessage)
    exit 1
}

foreach ($c in $cases) {
    $path = Join-Path $dataDir $c.File
    $ran++

    if (-not (Test-Path -LiteralPath $path)) {
        Add-Line ("[FAIL] {0} -- fixture missing" -f $c.File)
        $failures++
        continue
    }

    $rc  = Invoke-ClassifyAsan -LogPath $path -RepoRoot $fixtureRoot -Silent
    $out = @($script:Emitted)

    $problems = New-Object System.Collections.ArrayList
    if ($rc -ne $c.Expect) {
        [void] $problems.Add(('exit {0}, expected {1}' -f $rc, $c.Expect))
    }
    if ($c.ContainsKey('Match')) {
        foreach ($m in $c.Match) {
            if (-not ($out -match $m)) { [void] $problems.Add(('output does not match /{0}/' -f $m)) }
        }
    }
    if ($c.ContainsKey('Reject')) {
        foreach ($m in $c.Reject) {
            if ($out -match $m) { [void] $problems.Add(('output must NOT match /{0}/' -f $m)) }
        }
    }

    if ($problems.Count -eq 0) {
        Add-Line ("[ ok ] {0}  -> {1}" -f $c.File.PadRight(38), $rc)
        if ($wantDetail) { foreach ($l in $out) { Add-Line ('         ' + $l) } }
    }
    else {
        $failures++
        Add-Line ("[FAIL] {0}  -> {1}" -f $c.File.PadRight(38), ($problems -join '; '))
        Add-Line ('       because: ' + ($c.Why -replace '\s+', ' '))
        if ($c.ContainsKey('WhyMatch'))  { Add-Line ('       and    : ' + ($c.WhyMatch  -replace '\s+', ' ')) }
        if ($c.ContainsKey('WhyReject')) { Add-Line ('       and    : ' + ($c.WhyReject -replace '\s+', ' ')) }
        foreach ($l in $out) { Add-Line ('       | ' + $l) }
    }
}

# A missing log must fail closed too -- the gate treats 3 as red.
$ran++
$rc = Invoke-ClassifyAsan -LogPath (Join-Path $dataDir 'no-such-file.log') -RepoRoot $fixtureRoot -Silent
if ($rc -eq 3) {
    Add-Line ("[ ok ] {0}  -> {1}" -f '<missing log>'.PadRight(38), $rc)
}
else {
    $failures++
    Add-Line ("[FAIL] {0}  -> {1}, expected 3" -f '<missing log>'.PadRight(38), $rc)
    foreach ($l in @($script:Emitted)) { Add-Line ('       | ' + $l) }
}

# And an empty -LogPath must not be read as "nothing to do".
$ran++
$null = & $pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $classifier 2>&1
if ($LASTEXITCODE -eq 3) {
    Add-Line ("[ ok ] {0}  -> {1}" -f '<no -LogPath argument>'.PadRight(38), $LASTEXITCODE)
}
else {
    $failures++
    Add-Line ("[FAIL] {0}  -> {1}, expected 3" -f '<no -LogPath argument>'.PadRight(38), $LASTEXITCODE)
}

# ---------------------------------------------------------------------------
# The exit-code plumbing, through the real interpreter and the real -File path.
# ---------------------------------------------------------------------------
foreach ($c in $exitCodePlumbing) {
    $path = Join-Path $dataDir $c.File
    $ran++
    $out = & $pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $classifier `
               -LogPath $path -RepoRoot $fixtureRoot 2>&1
    $rc = $LASTEXITCODE
    if ($rc -eq $c.Expect) {
        Add-Line ("[ ok ] {0}  -> {1}" -f ('(spawned) ' + $c.File).PadRight(38), $rc)
    }
    else {
        $failures++
        Add-Line ("[FAIL] {0}  -> {1}, expected {2}" -f ('(spawned) ' + $c.File).PadRight(38), $rc, $c.Expect)
        Add-Line '       The in-process table passed, so this is the exit statement or'
        Add-Line '       $LASTEXITCODE plumbing, which is the only part verify.bat reads.'
        foreach ($l in $out) { Add-Line ('       | ' + $l) }
    }
}

# ---------------------------------------------------------------------------
if ($failures -eq 0) {
    if (-not $wantQuiet) { foreach ($l in $report) { Write-Host $l }; Write-Host '' }
    Write-Host ("[ ok ] classify-asan self-test: {0} cases, 0 failures" -f $ran)
    exit 0
}

# On failure, everything, regardless of -Quiet.  A gate that hides the reason
# is a gate somebody disables.
foreach ($l in $report) { Write-Host $l }
Write-Host ''
Write-Host ("[FAIL] classify-asan self-test: {0} cases, {1} failures" -f $ran, $failures)
# The Chinese half of the gate's message is printed HERE and not in verify.bat.
# verify.bat has LF line endings, and cmd.exe walks a batch file by byte offset
# while counting consumed text in characters; one multi-byte character in an
# LF-terminated .bat desynchronises the two and the parser resumes mid-line
# somewhere else in the file.  Measured: it made the gate try to execute
# `C_REL_BUILD=0' as a command at step [1/6].  A .ps1 with a UTF-8 BOM has no
# such problem -- [Console]::Out encodes to the console code page correctly --
# so the bilingual message lives on this side of the boundary.
Write-Host '[FAIL] ASan 分类器自检失败，本腿判定不可信。'
exit 1
