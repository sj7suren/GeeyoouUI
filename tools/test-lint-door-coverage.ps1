<#
    test-lint-door-coverage.ps1 -- prove tools\lint-door-coverage.ps1 before the
    gate is allowed to believe it.

    Exit code: 0 all cases pass, 1 at least one failed, 3 the harness itself
    could not run.  verify.bat calls this FIRST, inside step [1/6], and does not
    run the lint if this fails: a checker that fails its own fixtures has no
    verdict worth reading, and running it anyway produces an authoritative
    looking answer from a thing we have just proved is broken.  Exactly the rule
    tools\test-classify-asan.ps1 established for the ASan classifier.

    ------------------------------------------------------------------------
    THE TABLE IS RUN IN ONE PROCESS
    ------------------------------------------------------------------------
    The lint is dot-sourced once with -DefineOnly and driven in-process for
    every case.  One powershell.exe per fixture costs about 0.6s of cold start,
    and that design PUNISHES THE BEHAVIOUR WE REQUIRE: the rule is "add a
    fixture before you change a predicate", so making each fixture cost half a
    second is a tax on doing the right thing.  The exit-code plumbing that
    in-process calls skip is covered separately at the bottom, by running four
    fixtures through the real `powershell -File` path.

    ------------------------------------------------------------------------
    THE TWO-DIRECTION PROOF IS A CASE, NOT A CEREMONY
    ------------------------------------------------------------------------
    Section 11.9 property 3 is "an unarchived candidate makes the gate red".
    A lint that is green can be green for two reasons: everything has a home,
    or the lint does not work.  So the table proves BOTH directions, and it
    proves them on the REAL repository and the REAL document, not only on
    fixtures:

        real_repo_is_green            -- the table covers the tree today
        real_repo_reddens_if_a_row_is_deleted
                                      -- delete ONE row from section 11.4 in a
                                         COPY of the document and the same scan
                                         goes red, naming the frame that lost
                                         its home

    The copy matters: the proof must not require editing the document under
    version control, or it is a proof nobody re-runs.
#>

[CmdletBinding()]
param(
    # Print failures only.
    [switch] $Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'

$here     = $PSScriptRoot
$repoRoot = Split-Path -Parent $here
$lint     = Join-Path $here 'lint-door-coverage.ps1'
$fixtures = Join-Path $repoRoot 'tests\data\lint'
$realDoc  = Join-Path $repoRoot 'docs\iterations\02-layout-engine.md'

if (-not (Test-Path -LiteralPath $lint))     { Write-Host '  [FAIL] lint script not found'; exit 3 }
if (-not (Test-Path -LiteralPath $fixtures)) { Write-Host '  [FAIL] fixture directory not found'; exit 3 }

# NOTE the parameter name.  Dot-sourcing runs the sourced file's PARAM BLOCK in
# the CALLER's scope, so a `-Quiet` switch in lint-door-coverage.ps1 silently
# overwrites THIS file's $Quiet with its own default of $false -- the caller
# accepts -Quiet and then ignores it.  Priya hit exactly this in the ASan
# self-test.  Keeping our own copy under a different name is the whole fix.
$script:BeQuiet = [bool] $Quiet

. $lint -DefineOnly

$script:Pass = 0
$script:Fail = 0

function Test-Case {
    param(
        [string] $Name,
        [string] $Why,          # why this case exists.  A fixture whose purpose
                                # is not written down gets deleted the first
                                # time it is inconvenient.
        [string] $Root,
        [string] $Doc,
        [int]    $Expect,
        [string[]] $MustSay   = @(),
        [string[]] $MustNotSay = @()
    )

    # -Quiet, and it is load bearing for every MustNotSay assertion in this
    # table.  Without it the lint also prints the ARCHIVED candidates, so
    # "the output must not name frame X" is satisfied by X appearing in the list
    # of frames that are FINE -- and the assertion silently tests nothing.
    $rc = Invoke-LintDoorCoverage -RepoRoot $Root -DocPath $Doc -Silent -Quiet -MaxList 500
    $text = ($script:Emitted -join "`n")

    $problems = New-Object System.Collections.ArrayList
    if ($rc -ne $Expect) { [void] $problems.Add(('exit {0}, expected {1}' -f $rc, $Expect)) }
    foreach ($s in $MustSay) {
        if ($text.IndexOf($s, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
            [void] $problems.Add(('output never mentions "' + $s + '"'))
        }
    }
    foreach ($s in $MustNotSay) {
        if ($text.IndexOf($s, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            [void] $problems.Add(('output mentions "' + $s + '" and must not'))
        }
    }

    if ($problems.Count -eq 0) {
        $script:Pass++
        if (-not $script:BeQuiet) { Write-Host ('  [ ok ] ' + $Name) }
        return
    }
    $script:Fail++
    Write-Host ('  [FAIL] ' + $Name)
    Write-Host ('         why this case exists: ' + $Why)
    foreach ($p in $problems) { Write-Host ('         ' + $p) }
}

$basic     = Join-Path $fixtures 'basic'
$scanroot  = Join-Path $fixtures 'scanroot'
$plat      = Join-Path $fixtures 'platform'
$platIn    = Join-Path $fixtures 'platform-installed'
$batOk     = Join-Path $fixtures 'bat-ascii'
$batBad    = Join-Path $fixtures 'bat-nonascii'
$batEolOk  = Join-Path $fixtures 'bat-eol-crlf'
$batEolBad = Join-Path $fixtures 'bat-eol-lf'
$gateNone  = Join-Path $fixtures 'gate-no-lint-call'
$gateRem   = Join-Path $fixtures 'gate-call-in-a-comment'

# ---------------------------------------------------------------------------
# 1-2  the two directions, on fixtures
# ---------------------------------------------------------------------------
Test-Case -Name 'archived_candidates_are_green' `
    -Why 'the baseline: both candidates have a row, so the lint must not fire' `
    -Root $basic -Doc (Join-Path $basic 'doc-full.md') -Expect 0

Test-Case -Name 'deleting_one_row_turns_it_red' `
    -Why 'property 3. A green lint is worthless unless removing a home reddens it' `
    -Root $basic -Doc (Join-Path $basic 'doc-one-row-removed.md') -Expect 1 `
    -MustSay @('overrideOnlyVirtualIsStillADoor') `
    -MustNotSay @('unguardedDoorThenMemberRead')

# ---------------------------------------------------------------------------
# 3-5  property 2: a row without a reason, a grade and a round archives nothing
# ---------------------------------------------------------------------------
Test-Case -Name 'a_row_with_no_grade_archives_nothing' `
    -Why 'property 2 requires a grade; a blank one is an unfinished decision, not a decision' `
    -Root $basic -Doc (Join-Path $basic 'doc-missing-grade.md') -Expect 1 `
    -MustSay @('unguardedDoorThenMemberRead')

Test-Case -Name 'a_row_with_no_round_archives_nothing' `
    -Why 'property 2 requires a round; without one the candidate has no owner in time' `
    -Root $basic -Doc (Join-Path $basic 'doc-missing-round.md') -Expect 1 `
    -MustSay @('overrideOnlyVirtualIsStillADoor')

Test-Case -Name 'a_row_naming_only_a_file_archives_nothing' `
    -Why 'the key is (file, function). A row that names a FILE would archive every frame in it' `
    -Root $basic -Doc (Join-Path $basic 'doc-file-but-no-function.md') -Expect 1 `
    -MustSay @('unguardedDoorThenMemberRead')

# ---------------------------------------------------------------------------
# 6  fail closed on an unreadable allowlist
# ---------------------------------------------------------------------------
Test-Case -Name 'a_document_with_no_section_11_4_is_exit_3' `
    -Why 'no allowlist means no verdict. Exit 3 is red; treating it as "nothing archived, nothing found" would be green' `
    -Root $basic -Doc (Join-Path $basic 'doc-no-section.md') -Expect 3

Test-Case -Name 'a_missing_include_root_is_exit_3' `
    -Why 'no headers means no P1 names, which would silently disable half the predicate and still exit 0' `
    -Root (Join-Path $fixtures 'no-such-directory') -Doc (Join-Path $basic 'doc-full.md') -Expect 3

# ---------------------------------------------------------------------------
# 7-12  the false positives that would get this lint muted.  Each of these is a
#       frame that is CORRECT, and each one was a real bug in the first cut.
# ---------------------------------------------------------------------------
$rmv = Join-Path $basic 'doc-one-row-removed.md'

Test-Case -Name 'a_guarded_frame_is_not_a_candidate' `
    -Why 'the frame holds a cursor and checks it; reporting it is how a lint gets ignored' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('guardedDoorThenMemberRead')

Test-Case -Name 'a_door_that_is_the_last_statement_is_not_a_candidate' `
    -Why 'section 11.9 excludes it: with nothing after the door there is nothing to be dangerous' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('doorIsTheLastStatement')

Test-Case -Name 'a_qualified_call_is_not_a_door' `
    -Why 'Base::foo() is statically bound. Section 11.4 says so, and PushButton::sizeHint in IconButton is the live example' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('qualifiedCallIsNotADoor')

Test-Case -Name 'a_door_named_only_in_a_comment_is_not_a_door' `
    -Why 'the comments in this tree are longer than the code and they QUOTE the door names in order to explain them' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('doorNameOnlyInAComment')

Test-Case -Name 'a_door_named_only_in_a_string_is_not_a_door' `
    -Why 'same reason as the comment case, and a string is the one the comment stripper is most likely to miss' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('doorNameOnlyInAString')

Test-Case -Name 'a_destructor_written_as_override_is_not_a_door_name' `
    -Why 'half this tree writes ~Foo() override with no `virtual`; a virtual-tilde-only filter turned five class names into door names' `
    -Root $basic -Doc $rmv -Expect 1 -MustNotSay @('destructorNameIsNotADoorName')

# ---------------------------------------------------------------------------
# 13  property 4, second half: the SCAN ROOT
# ---------------------------------------------------------------------------
Test-Case -Name 'a_virtual_declared_outside_the_widget_base_is_still_found' `
    -Why 'the two holes are different. Declaration-side generation rooted at Widget.hpp still misses every Layout hook' `
    -Root $scanroot -Doc (Join-Path $scanroot 'doc-empty-table.md') -Expect 1 `
    -MustSay @('Layouty.cpp', 'invalidate')

# ---------------------------------------------------------------------------
# 13b-13c  property 4's scan root, ON THE CANDIDATE SIDE.
#
# Two cases and not one, because they are TWO HOLES, and the tree has now been
# taught that lesson three times: `layoutRect()` was missed because a virtual
# call has no name to grep, the Layout hooks were missed because the scan root
# was written too narrowly, and closing either did nothing for the other.  Here
# it is again, one level down: widening the candidate root to include\ finds
# NOTHING unless the splitter also stops reading `template <...>` as
# not-a-function, and teaching the splitter about templates finds nothing while
# the root is still `src`.  So each fixture must be able to redden alone.
# ---------------------------------------------------------------------------
Test-Case -Name 'a_plain_inline_in_a_header_is_a_candidate' `
    -Why 'the candidate side was rooted at src while the librarys code is not all in src' `
    -Root $basic -Doc (Join-Path $basic 'doc-header-inline-not-archived.md') -Expect 1 `
    -MustSay @('Thing.hpp', 'reseat')

Test-Case -Name 'a_template_body_in_a_header_is_a_candidate' `
    -Why '`template` is in NotAFunctionHead, so every template body read as not-a-function and its doors were never scanned' `
    -Root $basic -Doc (Join-Path $basic 'doc-header-template-not-archived.md') -Expect 1 `
    -MustSay @('Thing.hpp', 'adopt')

# ---------------------------------------------------------------------------
# 13d  P2 is not P1: a QUALIFIED P2 call is still a door
#
# `Base::relayout()` picks the implementation; it does not stop the
# implementation running application code.  The lookbehind that expresses "a
# qualified call is statically bound" belongs to P1, and copying it onto P2 was
# a category error whose direction is UNDER-REPORTING.
# ---------------------------------------------------------------------------
Test-Case -Name 'a_qualified_p2_call_is_still_a_door' `
    -Why 'the qualified-call exclusion is about virtual dispatch, and P2 is not dispatch' `
    -Root $basic -Doc (Join-Path $basic 'doc-qualified-p2-not-archived.md') -Expect 1 `
    -MustSay @('qualifiedP2CallIsStillADoor') `
    -MustNotSay @('qualifiedCallIsNotADoor')

# ---------------------------------------------------------------------------
# 13e-13g  THE P2 LIST IS THE DOCUMENT'S, and there is no second copy
#
# It used to be an array in the lint with a comment saying "when you add one
# here, add it there too", and the two copies had already drifted.  These three
# cases are the machine check that the script now READS the document: narrow the
# document's clause and the candidate it named disappears; break the clause and
# the run fails closed rather than falling back on anything.
# ---------------------------------------------------------------------------
Test-Case -Name 'the_p2_primitive_list_comes_from_the_document' `
    -Why 'the differential proof: drop setGeometry from the documents P2 clause and its candidate must vanish' `
    -Root $basic -Doc (Join-Path $basic 'doc-p2-clause-narrowed.md') -Expect 0 `
    -MustNotSay @('qualifiedP2CallIsStillADoor')

Test-Case -Name 'a_document_with_no_p2_clause_is_exit_3' `
    -Why 'no P2 list means half the predicate is missing; silently scanning with none would be a green run' `
    -Root $basic -Doc (Join-Path $basic 'doc-no-p2-clause.md') -Expect 3 `
    -MustSay @('P2 primitive clause')

Test-Case -Name 'a_p2_token_that_is_not_an_identifier_is_exit_3' `
    -Why 'skipping the token instead would let one typo in the document silently delete a primitive' `
    -Root $basic -Doc (Join-Path $basic 'doc-p2-token-not-an-identifier.md') -Expect 3 `
    -MustSay @('cannot reduce')

# ---------------------------------------------------------------------------
# 13h-13i  IS THE GATE STILL PLUGGED IN?
#
# Measured before it was written: delete `call :lint_doors` from verify.bat and
# the gate runs all six steps and prints "gate is GREEN".  Both checkers redden
# the gate by setting a variable from inside a subroutine, so an uncalled
# subroutine is a checker that fails open and looks like a clean run.
# ---------------------------------------------------------------------------
Test-Case -Name 'a_gate_that_stopped_calling_the_lint_reddens' `
    -Why 'a checker nobody invokes fails open, silently, and the run looks exactly like a clean one' `
    -Root $gateNone -Doc (Join-Path $gateNone 'doc-empty-table.md') -Expect 1 `
    -MustSay @('never calls :lint_doors')

Test-Case -Name 'a_call_that_survives_only_as_a_comment_does_not_count' `
    -Why 'verify.bat has more rem than code and its comments quote these very lines; a raw match would pass' `
    -Root $gateRem -Doc (Join-Path $gateRem 'doc-empty-table.md') -Expect 1 `
    -MustSay @('never calls :lint_doors')

# ---------------------------------------------------------------------------
# 14-15  the Platform exemption, and the condition that ends it
# ---------------------------------------------------------------------------
Test-Case -Name 'platform_only_virtual_names_are_exempt' `
    -Why 'no install point means no override interface. Leaving restore/show/close in the P1 set makes every painter.restore() a door' `
    -Root $plat -Doc (Join-Path $plat 'doc-empty-table.md') -Expect 0

Test-Case -Name 'an_install_point_ends_the_platform_exemption' `
    -Why 'section 11.4 wrote the trigger down: the day an install point appears, those virtuals enter the table' `
    -Root $platIn -Doc (Join-Path $platIn 'doc-empty-table.md') -Expect 1 `
    -MustSay @('install point', 'Platform.hpp')

# ---------------------------------------------------------------------------
# 16-19  THE BATCH-FILE PRECONDITION -- BOTH HALVES OF THE CONJUNCTION
#
# cmd.exe mis-executes a .bat that has BOTH bare-LF endings AND multi-byte
# characters; the truth table was measured four ways and neither condition
# alone does anything.  Four cases and not two, because a conjunction breaks if
# ONE half moves, so a suite that only proves the ASCII half is red-able leaves
# the LF half free to come back unobserved -- and it did exactly that until
# R2.4: the LF half was a sentence in a comment ("every .bat here is bare-LF")
# and nothing on this machine could tell you whether it was still true.
#
# ⚠ 16's MEANING CHANGED IN R2.4 AND THE OLD WORDING IS WORTH RECORDING.  It
# used to read "Bare LF alone is harmless and must not redden the gate", which
# was a correct statement about cmd.exe and the wrong policy for this tree: it
# TOLERATED the standing half of an armed conjunction, so the only thing
# between the gate and mis-execution was everyone remembering, forever, not to
# type a non-ASCII character into a .bat.  .gitattributes now pins
# `*.bat text eol=crlf`, bare LF is a defect on its own, and 16 is the control
# for the pinned state instead of for the tolerated one.
# ---------------------------------------------------------------------------
Test-Case -Name 'a_crlf_pure_ascii_batch_file_passes' `
    -Why 'the control: the shape .gitattributes pins is the shape that must be green' `
    -Root $batOk -Doc (Join-Path $batOk 'doc-empty-table.md') -Expect 0

Test-Case -Name 'a_multibyte_batch_file_reddens' `
    -Why 'multi-byte is the half that costs an hour when the other half is also present' `
    -Root $batBad -Doc (Join-Path $batBad 'doc-empty-table.md') -Expect 1 `
    -MustSay @('armed.bat', 'ASCII')

# 18-19  THE LF HALF, AND IT IS A DIFFERENTIAL.
#
# bat-eol-crlf\hygiene.bat and bat-eol-lf\hygiene.bat hold THE SAME CHARACTERS
# -- 711 bytes against 697, and the difference is exactly the fourteen CRs.
# Both are pure ASCII, so the other half of the conjunction is held still.  Two
# roots that differ in nothing else and are required to reach opposite
# verdicts: that is the check being about the bytes, proved rather than
# asserted.
#
# ⚠ bat-eol-lf\hygiene.bat IS PINNED `text eol=lf` IN .gitattributes, BY NAME.
# Without that line the repository-wide `*.bat text eol=crlf` would REPAIR it
# on the next checkout, case 19 would go green because there was nothing left
# to catch, and the LF half would be unguarded again while its test still
# printed [ ok ].  A negative fixture git silently fixes is the same failure as
# a normalised golden, one level down.
Test-Case -Name 'a_crlf_batch_file_passes_the_eol_check' `
    -Why 'the positive half of the differential: identical text, CRLF, must be green' `
    -Root $batEolOk -Doc (Join-Path $batEolOk 'doc-empty-table.md') -Expect 0

Test-Case -Name 'a_bare_lf_batch_file_reddens' `
    -Why 'the same characters with LF must redden, or .gitattributes pin could lapse unobserved' `
    -Root $batEolBad -Doc (Join-Path $batEolBad 'doc-empty-table.md') -Expect 1 `
    -MustSay @('hygiene.bat', 'bare-LF')

# ---------------------------------------------------------------------------
# 18  THE PROOF ON REAL DATA, both directions, IN ONE CASE.
#
# ⚠ THERE IS DELIBERATELY NO `real_repo_is_green` CASE HERE, and the reason is
# worth the paragraph.  There was one, and it was WRONG: it made this file
# assert that the TREE is clean, which is the LINT's job, not the lint
# self-test's.  Measured consequence -- delete one row from section 11.4 and the
# gate printed
#
#     [FAIL] door-coverage lint self-test FAILED, so the lint was NOT run.
#            Fix tools\lint-door-coverage.ps1, or the fixture that caught it.
#
# -- so the one message a developer sees when they have actually written an
# unguarded door tells them to go and fix the LINT.  That is how a checker
# teaches people to edit its fixtures, and this whole family recurs because
# checks got edited instead of read.
#
# The separation is the same one tools\test-classify-asan.ps1 keeps: THIS file
# proves the mechanism works; the lint, run straight afterwards, proves the tree
# is covered, and says so in its own words.
#
# So the real-data proof is DIFFERENTIAL, and therefore holds whether or not the
# tree is green today: the same scan, over the same sources, with one row
# present and then absent, must name the frame that lost its home in the second
# run and not in the first.
# ---------------------------------------------------------------------------
$tmpDoc = Join-Path ([System.IO.Path]::GetTempPath()) ('gy-lint-doc-' + [System.Guid]::NewGuid().ToString('N') + '.md')
try {
    $docLines = [System.IO.File]::ReadAllLines($realDoc, [System.Text.Encoding]::UTF8)
    $kept = New-Object System.Collections.ArrayList
    $dropped = ''
    foreach ($l in $docLines) {
        # Drop exactly ONE archiving row: the first register row.  Any row would
        # do; a deterministic one makes the failure message reproducible.
        if (-not $dropped -and $l -match '^\|\s*L01-') { $dropped = $l; continue }
        [void] $kept.Add($l)
    }
    if (-not $dropped) {
        Write-Host '  [FAIL] deleting_a_real_row_names_the_frame_that_lost_its_home'
        Write-Host '         could not find a row to delete in section 11.4 -- the register is gone?'
        $script:Fail++
    }
    else {
        [System.IO.File]::WriteAllLines($tmpDoc, $kept, (New-Object System.Text.UTF8Encoding($false)))

        [void] (Invoke-LintDoorCoverage -RepoRoot $repoRoot -DocPath $realDoc -Silent -Quiet -MaxList 500)
        $before = ($script:Emitted -join "`n")
        $rcAfter = Invoke-LintDoorCoverage -RepoRoot $repoRoot -DocPath $tmpDoc -Silent -Quiet -MaxList 500
        $after = ($script:Emitted -join "`n")

        $probs = New-Object System.Collections.ArrayList
        if ($rcAfter -ne 1) { [void] $probs.Add(('exit {0} with the row deleted, expected 1' -f $rcAfter)) }
        if ($after -notmatch 'CheckBox\.cpp::onPaint') {
            [void] $probs.Add('with the row deleted, CheckBox.cpp::onPaint is not named as unarchived')
        }
        if ($before -match 'CheckBox\.cpp::onPaint') {
            [void] $probs.Add('with the row present, CheckBox.cpp::onPaint is named anyway -- the row archives nothing')
        }
        if ($probs.Count -eq 0) {
            $script:Pass++
            if (-not $script:BeQuiet) { Write-Host '  [ ok ] deleting_a_real_row_names_the_frame_that_lost_its_home' }
        }
        else {
            $script:Fail++
            Write-Host '  [FAIL] deleting_a_real_row_names_the_frame_that_lost_its_home'
            Write-Host '         why this case exists: property 3, proved on the real document, in both directions'
            foreach ($p in $probs) { Write-Host ('         ' + $p) }
        }
    }
}
finally {
    if (Test-Path -LiteralPath $tmpDoc) { Remove-Item -LiteralPath $tmpDoc -Force }
}

# ---------------------------------------------------------------------------
# 20-21  THE EXIT-CODE PLUMBING, through the real `powershell -File` path.
#
# Every case above returns a value from a function.  verify.bat reads an
# ERRORLEVEL, and the two are only the same thing if the script body maps one to
# the other -- which is a line of code, and therefore a line of code that can be
# wrong.  Spawning costs about 0.6s each, so this covers the boundary values
# only: the green one and the two reds.
# ---------------------------------------------------------------------------
$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

function Test-Spawn {
    param([string] $Name, [string] $Why, [string] $Root, [string] $Doc, [int] $Expect)
    & $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $lint `
        -RepoRoot $Root -DocPath $Doc -Quiet | Out-Null
    $rc = $LASTEXITCODE
    if ($rc -eq $Expect) {
        $script:Pass++
        if (-not $script:BeQuiet) { Write-Host ('  [ ok ] ' + $Name) }
        return
    }
    $script:Fail++
    Write-Host ('  [FAIL] ' + $Name)
    Write-Host ('         why this case exists: ' + $Why)
    Write-Host ('         exit {0}, expected {1}' -f $rc, $Expect)
}

Test-Spawn -Name 'spawned_green_returns_0' `
    -Why 'the script body must turn a returned 0 into ERRORLEVEL 0' `
    -Root $basic -Doc (Join-Path $basic 'doc-full.md') -Expect 0

Test-Spawn -Name 'spawned_unarchived_returns_1' `
    -Why 'verify.bat only reddens on a non-zero ERRORLEVEL; a verdict that never leaves the process is decorative' `
    -Root $basic -Doc $rmv -Expect 1

Test-Spawn -Name 'spawned_unparseable_doc_returns_3' `
    -Why 'the fail-closed path has to survive the process boundary too' `
    -Root $basic -Doc (Join-Path $basic 'doc-no-section.md') -Expect 3

# ---------------------------------------------------------------------------
Write-Host ''
if ($script:Fail -eq 0) {
    Write-Host ('  [ ok ] door-coverage lint self-test: {0} cases, 0 failures' -f $script:Pass)
    exit 0
}
Write-Host ('  [FAIL] door-coverage lint self-test: {0} cases, {1} failure(s)' -f ($script:Pass + $script:Fail), $script:Fail)
exit 1
