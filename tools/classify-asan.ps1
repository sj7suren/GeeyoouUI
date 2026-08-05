<#
    classify-asan.ps1 -- decide whether an AddressSanitizer log is OUR defect.

    Called by verify.bat step [6/6].  Exit code IS the verdict:

        0  no AddressSanitizer / LeakSanitizer report in the log at all
        2  reports present, every one of them attributable to third-party code
        1  at least one report attributable to GeeyoouUI  -> the gate must go RED
        3  the log could not be parsed, or this script threw

    verify.bat treats ANYTHING that is not 0 and not 2 as red, so a missing
    PowerShell, a syntax error in here, or an unreadable log all fail the gate
    rather than waving it through.  That is deliberate: a classifier that fails
    open is worse than no classifier, because it looks like one.

    ----------------------------------------------------------------------------
    WHY THIS EXISTS, i.e. what was wrong with the one line it replaces
    ----------------------------------------------------------------------------
    The gate used to ask one question of the whole log:

        does the text "GeeyoouUI\src" (or \include, \tests, \examples) appear
        ANYWHERE in it?

    That is an APPEARED-IN question, not a WHO-IS-RESPONSIBLE question, and on
    this machine the difference is not academic.  A third-party IME (SogouPY.ime)
    injects itself into every process that creates a Win32 window and has a
    use-after-free of its own that fires while the OS tears a window down.  Our
    Window::~Window is on that stack -- it is the thing that called
    DestroyWindow -- so OUR path appears in THEIR report, the old test matched,
    and the gate went red because of software nobody on this team ships.

    A gate that goes red at random is more dangerous than a gate that never goes
    red, because the team learns to click past it, and then it is not a gate for
    the real defects either.

    ----------------------------------------------------------------------------
    THE RULE
    ----------------------------------------------------------------------------
    An ASan use-after-free report has two stacks that assign blame and one that
    does not:

        ERROR: AddressSanitizer: heap-use-after-free on address 0x...
        READ of size 8 at 0x... thread T0
            #0 ...          <- THE USE SITE.  blame.
            #1 ...
        freed by thread T0 here:
            #0 _asan_wrap... <- ASan's allocator interceptor
            #1 gyFree        <- OUR global operator delete replacement
            #2 operator del. <- ditto.  see Get-BlameFrame: this is plumbing,
            #3 ...              every free in the process goes through it
                                and it is NOT the free site
            #4 ...          <- THE FREE SITE.  blame.
        previously allocated by thread T0 here:
            #0 ...          <- allocating is not a defect.  no blame.

    So for the use-after-free FAMILY we walk each blame stack from the innermost
    frame outwards, step over the global-allocator plumbing (Get-BlameFrame) and
    over frames that are structurally incapable of being the culprit
    (Get-FrameClass), and look at the first frame that is left.  If either of
    those two frames is GeeyoouUI code, the report is ours.  If both are
    third-party -- SogouPY, user32, ntdll, some DLL nobody here compiled -- the
    report is third-party, printed as [known], and not counted.

    The allocator-plumbing step is not a detail.  tests\framework\Test.cpp
    replaces global operator new/delete, so without it the innermost "blame"
    frame of every free stack in the binary is one of our own files and the
    classifier calls everything ours -- including the noise it was written to
    suppress, while looking like it works.  That was caught by running the gate
    against a deliberately injected use-after-free, not by reading the code.

    For EVERY OTHER report kind (heap-buffer-overflow, stack-use-after-return,
    stack-use-after-scope, SEGV, LeakSanitizer, anything AddressSanitizer grows
    next year) we keep the OLD, BROAD rule: any GeeyoouUI frame or path anywhere
    in the report makes it ours.  Two reasons.  One, those kinds do not have the
    stack shape the narrow rule is written against, so the narrow rule would be
    guessing.  Two, the noise we actually measured is a use-after-free during
    window teardown and nothing else, so that is exactly how far the relaxation
    should reach.  Behaviour for every kind except the use-after-free family is
    therefore bit-for-bit what the gate did before this script existed.

    ----------------------------------------------------------------------------
    FAIL-CLOSED, AND WHY YOU SHOULD NOT LOOSEN IT
    ----------------------------------------------------------------------------
    Every "I cannot tell" answer resolves to OURS:

      * a frame whose module and source file are both unknown  -> ours
      * a blame stack with no parseable frames                 -> ours
      * a report with no blame stack at all                    -> ours
      * a section header this script does not recognise, if it
        has frames under it                                    -> blame stack
      * anything under the repository root, including
        build*/_deps (blend2d, asmjit)                          -> ours

    That last one is a choice worth stating: vendored dependencies are code we
    compile and ship, so a report inside blend2d is our problem to fix or to pin,
    not environmental noise.  Only code we neither wrote nor build is third-party.

    The asymmetry is the whole point.  A false red costs somebody an hour.  A
    false green costs a use-after-free in a release -- and all five that the R2
    reviews found were green in both non-ASan legs, which is to say this leg is
    the only thing standing between that class of defect and a shipped build.
    If you are here because the gate went red on something you are sure is not
    ours: add the case to tools/test-classify-asan.ps1 FIRST, with the real log
    text, then change the rule.  Do not widen a predicate you have no test for.

    ----------------------------------------------------------------------------
    KNOWN RESIDUAL HOLE (documented, not fixed)
    ----------------------------------------------------------------------------
    Under the narrow rule a use-after-free in which our ONLY involvement is
    being the direct caller of a system API that touches the freed byte -- say
    SetWindowTextW(hwnd, alreadyFreedString) -- has user32 at the use site and,
    if something else freed it, a third-party free site too, and would be filed
    third-party.  We did not add a "look N frames out" window to catch it,
    because the SogouPY stack puts our frame only two or three frames out as
    well, so any such window reopens exactly the hole this script closes.
    Instead the mitigation is visibility: every third-party report is PRINTED in
    full summary form on every run, kind plus both innermost frames, so a change
    in the shape of the noise is in front of a human rather than swallowed.
#>

[CmdletBinding()]
param(
    # The ASan run log to classify.  Never pass a directory path ending in a
    # backslash from batch: "%FOO%" where FOO ends in \ makes the CRT eat the
    # closing quote and the argument list collapses.  That exact bug already
    # cost this repository a blind findstr once; see verify.bat.
    [Parameter(Mandatory = $true)]
    [string] $LogPath,

    # Defaults to this script's parent directory, which is the repo root.
    # Exists as a parameter only so the self-test can point it somewhere else.
    [string] $RepoRoot = '',

    # Print the verdict lines only, no per-frame detail.
    [switch] $Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'

$EXIT_CLEAN       = 0
$EXIT_OURS        = 1
$EXIT_THIRD_PARTY = 2
$EXIT_INTERNAL    = 3

function Write-Line { param([string] $Text = '') [Console]::Out.WriteLine($Text) }

# [System.IO.Path]::GetFileName throws ArgumentException on .NET Framework when
# the string contains characters that are illegal in a path -- and ASan prints
# the literal "<unknown module>", which contains two of them.  That exception
# would be caught below and fail the gate closed, i.e. a red gate every time a
# stripped frame appears.  Correct direction, wrong reason, so: split by hand.
function Get-Leaf {
    param([string] $Path)
    if (-not $Path) { return '' }
    $i = [Math]::Max($Path.LastIndexOf('\'), $Path.LastIndexOf('/'))
    if ($i -ge 0) { return $Path.Substring($i + 1) }
    return $Path
}

# ---------------------------------------------------------------------------
# Frame parsing.
#
# ASan frame lines come in these shapes, and the function name in the middle
# may itself contain spaces, commas, angle brackets and parentheses
# (`operator()`, template arguments), so nothing here may assume the fields
# are whitespace separated:
#
#     #0 0x7ff6 in Geeyoou::Widget::layout(int, int) E:\...\src\Widget.cpp:120:5
#     #1 0x7ff6 in SogouPY_Hook (C:\Windows\SogouPY.ime+0x1234)
#     #2 0x7ff6 (C:\Windows\System32\ntdll.dll+0x5678)
#     #3 0x7ff6 (<unknown module>)
#
# We therefore read from the RIGHT: a trailing (...) that looks like a module
# is the module, otherwise the last drive-letter run in the line starts the
# source path.  A function name containing "X:\" would fool that, which is not
# a thing that occurs in C++ identifiers.
# ---------------------------------------------------------------------------
function ConvertTo-Frame {
    param([string] $Line)

    $m = [regex]::Match($Line, '^\s*#(?<n>\d+)\s+0x[0-9a-fA-F]+\s*(?<rest>.*)$')
    if (-not $m.Success) { return $null }

    $frame = @{
        Raw    = $Line.Trim()
        Index  = [int] $m.Groups['n'].Value
        Func   = ''
        File   = ''
        Module = ''
    }

    $rest = $m.Groups['rest'].Value.Trim()
    $head = $rest

    # Trailing parenthesised group -- only believe it is a module if it looks
    # like one.  "in Foo::bar(int, char)" also ends in ')'.
    $mm = [regex]::Match($rest, '\(([^()]*)\)\s*$')
    if ($mm.Success) {
        $inner = $mm.Groups[1].Value.Trim()
        $looksLikeModule =
            ($inner -match '\+0x[0-9a-fA-F]+$') -or
            ($inner -match '^<.*>$') -or
            ($inner -match '\.(dll|exe|ime|sys|ocx|node)(\+|$)')
        if ($looksLikeModule) {
            $plus = $inner.LastIndexOf('+0x')
            if ($plus -gt 0) { $frame.Module = $inner.Substring(0, $plus).Trim() }
            else            { $frame.Module = $inner }
            $head = $rest.Substring(0, $mm.Index).Trim()
        }
    }

    if ($head -match '^in\s') { $head = $head.Substring(3).Trim() }

    # Absolute source path: last drive-letter run wins.
    $drives = [regex]::Matches($head, '[A-Za-z]:[\\/]')
    if ($drives.Count -gt 0) {
        $start = $drives[$drives.Count - 1].Index
        $frame.File = [regex]::Replace($head.Substring($start).Trim(), '(:\d+)+$', '')
        $frame.Func = $head.Substring(0, $start).Trim()
    }
    elseif ($head -match '^(?<f>.*?)\s(?<p>[^\s]+\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inc|ipp|inl|asm|s)(:\d+)*)$') {
        # Relative source path.  Treated as ours further down: only our own
        # translation units are compiled with paths relative to this tree.
        $frame.File = [regex]::Replace($matches['p'], '(:\d+)+$', '')
        $frame.Func = $matches['f'].Trim()
    }
    else {
        $frame.Func = $head
    }

    return $frame
}

# ---------------------------------------------------------------------------
# Frame classification.  Order matters and is load bearing.
#
#   Runtime / Toolchain  transparent -- keep walking outwards.  These frames
#                        cannot be the culprit: they are the instrumentation
#                        of, or the inlined header body of, the call the NEXT
#                        frame out made.  Skipping them can only ever expose a
#                        frame that carries real blame, never hide one, so it
#                        is a fail-closed-leaning move.
#   Ours                 stop, blame us.
#   ThirdParty           stop, do not blame us.
#   Unknown              stop, blame us (fail closed).
# ---------------------------------------------------------------------------
function Get-FrameClass {
    param([hashtable] $Frame, [string] $RepoRootNorm)

    $file   = $Frame.File.Replace('/', '\')
    $module = $Frame.Module.Replace('/', '\')
    $func   = $Frame.Func

    # 1. The ASan runtime itself.  MUST come before the repo-root test, because
    #    build-asan.bat copies clang_rt.asan_dynamic-x86_64.dll INTO
    #    build-asan\bin\, i.e. underneath the repository root.
    if ($func -match '^(__asan|__sanitizer|__interception|_asan_wrap)') { return 'Runtime' }
    foreach ($p in @($file, $module)) {
        if (-not $p) { continue }
        $leaf = Get-Leaf $p
        if ($leaf -match '^(asan|lsan|msan|tsan|ubsan|sanitizer|interception)_') { return 'Runtime' }
        if ($leaf -match '^clang_rt\.')                                          { return 'Runtime' }
        if ($p   -imatch '\\compiler-rt\\lib\\')                                 { return 'Runtime' }
    }

    # 2. Ours: anything under this tree (build*/_deps included -- see header),
    #    anything under a .../GeeyoouUI/{src,include,tests,examples}/ path in
    #    case the build ever emits paths rooted somewhere else, and any source
    #    file the compiler emitted as a RELATIVE path, since only our own
    #    translation units are compiled from inside this tree.
    foreach ($p in @($file, $module)) {
        if (-not $p) { continue }
        if ($RepoRootNorm -and $p.StartsWith($RepoRootNorm, [System.StringComparison]::OrdinalIgnoreCase)) { return 'Ours' }
        if ($p -imatch '\\GeeyoouUI\\(src|include|tests|examples)\\') { return 'Ours' }
    }
    if ($file -and ($file -notmatch '^[A-Za-z]:\\') -and ($file -notmatch '^\\\\')) { return 'Ours' }

    # 3. Toolchain: the MSVC STL headers, the CRT sources and the CRT DLLs.
    #    Transparent for the same reason the ASan runtime is -- a use-after-free
    #    surfacing inside xstring is our dangling object, not a defect in
    #    Microsoft's basic_string -- and skipping them reaches our frame, which
    #    makes the gate redder, not greener.
    #    NOTE ntdll / user32 / win32u / kernelbase are deliberately NOT here.
    #    Those are where the injected-IME noise lives, and treating them as
    #    transparent would walk straight back to our Window::~Window and undo
    #    the entire point of this script.
    foreach ($p in @($file, $module)) {
        if (-not $p) { continue }
        if ($p -imatch '\\VC\\Tools\\MSVC\\')   { return 'Toolchain' }
        if ($p -imatch '\\Windows Kits\\')      { return 'Toolchain' }
        if ($p -imatch '\\vctools\\crt\\')      { return 'Toolchain' }
        if ($p -imatch 'minkernel\\crts\\')     { return 'Toolchain' }
        $leaf = Get-Leaf $p
        if ($leaf -imatch '^(ucrtbase|vcruntime\d*|msvcp\d*|msvcr\d*|msvcrt)\.dll$') { return 'Toolchain' }
    }

    # 4. A named module or a named absolute file we neither wrote nor build.
    if ($module -and ($module -notmatch '^<')) { return 'ThirdParty' }
    if ($file)                                 { return 'ThirdParty' }

    # 5. No module, no file.  <unknown module>, a stripped frame, a truncated
    #    log.  Cannot tell -> ours.
    return 'Unknown'
}

# How far into a stack we are willing to look for global-allocator plumbing.
# The plumbing is always at the very bottom of a stack by construction; 8 is
# slack, not a tuning knob.
$script:AllocPlumbingWindow = 8

function Get-BlameFrame {
    # Walk outwards, skip everything that is structurally incapable of being the
    # culprit, and return the first frame that carries a verdict.  $null means
    # the stack was empty or entirely transparent, which the caller must treat
    # as ours.
    param([array] $Frames, [string] $RepoRootNorm)

    # ---- step 1: step over the global allocator ----------------------------
    #
    # MEASURED, NOT ASSUMED, AND THE ENTIRE CLASSIFIER TURNS ON IT.
    #
    # tests/framework/Test.cpp REPLACES global operator new / operator delete
    # (it is the only way to count allocations inside the library under test).
    # A replacement of a global operator is process-wide, so EVERY free in the
    # test binary goes through it -- including frees made by an injected IME
    # that has never heard of us.  A real free stack from this build looks like:
    #
    #     #0 _asan_wrap__CrtIsValidHeapPointer  (clang_rt.asan_dynamic...dll)
    #     #1 `anonymous namespace'::gyFree      tests\framework\Test.cpp:52
    #     #2 operator delete[]                  tests\framework\Test.cpp:81
    #     #3 <whoever actually freed it>
    #
    # Frames #1 and #2 are OUR source files.  Taking "the innermost non-runtime
    # frame" literally therefore answers "tests\framework\Test.cpp" for every
    # single free in the process, third-party ones included -- which would have
    # left this classifier reporting the SogouPY case as ours and the gate just
    # as flaky as before, while LOOKING like it had been fixed.  The first
    # verify.bat run against a deliberately injected use-after-free is what
    # exposed this; the fixtures had assumed the stock MSVC delete thunk.
    #
    # So: find the OUTERMOST operator new / operator delete frame near the
    # bottom of the stack and start blaming after it.  Structural, so it needs
    # no knowledge of the name `gyFree`, and it is correct for the stock thunk
    # too (there the operator frame is #0 and blame starts at #1).
    #
    # Limitation, stated rather than hidden: a defect INSIDE the allocator
    # replacement itself is attributed to its caller.  That caller is our test
    # framework in every stack we have seen, so the gate is still red; it would
    # only matter if third-party code called our operator delete and our
    # operator delete were the buggy one.
    $start = 0
    $limit = [Math]::Min($Frames.Count, $script:AllocPlumbingWindow)
    for ($i = 0; $i -lt $limit; $i++) {
        if ($Frames[$i].Func -match '^operator\s+(new|delete)\b') { $start = $i + 1 }
    }

    # ---- step 2: skip the transparent classes ------------------------------
    for ($i = $start; $i -lt $Frames.Count; $i++) {
        $f = $Frames[$i]
        $c = Get-FrameClass -Frame $f -RepoRootNorm $RepoRootNorm
        if ($c -eq 'Runtime' -or $c -eq 'Toolchain') { continue }
        return @{ Frame = $f; Class = $c }
    }
    return $null
}

function Format-Frame {
    param($Blame)
    if ($null -eq $Blame) { return '<no non-runtime frame>  [cannot tell -> ours]' }
    $f = $Blame.Frame
    $where = $f.File
    if (-not $where) { $where = $f.Module }
    if (-not $where) { $where = '<unknown module>' }
    $what = $f.Func
    if (-not $what) { $what = '?' }
    if ($what.Length -gt 60) { $what = $what.Substring(0, 57) + '...' }
    return ('#{0} {1}  [{2}]  {3}' -f $f.Index, $what, $Blame.Class, $where)
}

# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------
try {
    if (-not $RepoRoot) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
    $repoNorm = $RepoRoot.Replace('/', '\').TrimEnd('\')
    if ($repoNorm) { $repoNorm = $repoNorm + '\' }

    if (-not (Test-Path -LiteralPath $LogPath)) {
        Write-Line ('  [classify-asan] log not found: ' + $LogPath)
        exit $EXIT_INTERNAL
    }

    # Read as latin-1 so no byte sequence can throw and no decoder can mangle
    # the ASCII we match on.  The suite prints Chinese notes; we never match
    # against those, and a mojibake note is better than a decoder exception
    # taking the gate down.
    $bytes = [System.IO.File]::ReadAllBytes($LogPath)
    $text  = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $lines = $text -split "`r`n|`n|`r"

    # -- split the log into reports, and each report into stack sections ------
    $reports    = New-Object System.Collections.ArrayList
    $cur        = $null
    $curSection = $null

    foreach ($line in $lines) {
        $start = [regex]::Match($line, 'ERROR:\s*(?<tool>AddressSanitizer|LeakSanitizer):\s*(?<kind>.*)$')
        if ($start.Success) {
            $kind = $start.Groups['kind'].Value.Trim()
            $kind = [regex]::Replace($kind, '\s+on (unknown )?address.*$', '')
            $kind = [regex]::Replace($kind, '\s+at pc .*$', '')
            $kind = $kind.Trim()
            if (-not $kind) { $kind = 'unspecified' }

            $cur = @{
                Tool     = $start.Groups['tool'].Value
                Kind     = $kind
                Sections = (New-Object System.Collections.ArrayList)
                Text     = (New-Object System.Text.StringBuilder)
            }
            [void] $reports.Add($cur)
            $curSection = @{ Header = $line.Trim(); Frames = (New-Object System.Collections.ArrayList) }
            [void] $cur.Sections.Add($curSection)
            [void] $cur.Text.AppendLine($line)
            continue
        }

        if ($null -eq $cur) { continue }
        [void] $cur.Text.AppendLine($line)

        $frame = ConvertTo-Frame -Line $line
        if ($null -ne $frame) { [void] $curSection.Frames.Add($frame); continue }

        if ($line.Trim() -eq '') { continue }

        if ($line -match '^\s*SUMMARY:') {
            # End of the report proper; the shadow-byte dump follows and must
            # not be mistaken for stack sections.
            $cur = $null; $curSection = $null
            continue
        }

        $curSection = @{ Header = $line.Trim(); Frames = (New-Object System.Collections.ArrayList) }
        [void] $cur.Sections.Add($curSection)
    }

    if ($reports.Count -eq 0) { exit $EXIT_CLEAN }

    # -- classify -------------------------------------------------------------
    # The narrow rule applies only to the family that actually produces the
    # noise: reports that carry a "freed by" stack.
    $uafFamily = '^(heap-use-after-free|double-free|alloc-dealloc-mismatch|attempting free on address which was not malloc)'

    $anyOurs = $false
    $verdicts = New-Object System.Collections.ArrayList
    $n = 0

    foreach ($r in $reports) {
        $n++
        $ours = $false
        $why  = ''
        $useBlame  = $null
        $freeBlame = $null
        $narrow    = $false
        $freeSeen  = $false

        if ($r.Tool -eq 'AddressSanitizer' -and $r.Kind -imatch $uafFamily) {
            # ---- narrow rule: who used it, and who freed it ------------------
            $narrow     = $true
            $useFrames  = $null
            $freeFrames = $null
            $sawFreeSection = $false

            for ($i = 0; $i -lt $r.Sections.Count; $i++) {
                $s = $r.Sections[$i]
                if ($s.Frames.Count -eq 0) { continue }
                if ($null -eq $useFrames -and $s.Header -notmatch '^(freed|previously allocated|allocated|Thread T\d+ created)') {
                    $useFrames = $s.Frames
                    continue
                }
                if ($s.Header -match '^freed by thread') {
                    $sawFreeSection = $true
                    if ($null -eq $freeFrames) { $freeFrames = $s.Frames }
                    continue
                }
                if ($s.Header -match '^(previously allocated|allocated) by thread' -or
                    $s.Header -match '^Thread T\d+ created by') {
                    continue
                }
                # Unrecognised section carrying frames: fail closed, treat it as
                # a blame stack by folding it into the use side.
                if ($null -eq $useFrames) { $useFrames = $s.Frames }
                else {
                    $merged = New-Object System.Collections.ArrayList
                    foreach ($f in $useFrames) { [void] $merged.Add($f) }
                    foreach ($f in $s.Frames)  { [void] $merged.Add($f) }
                    $useFrames = $merged
                }
            }

            if ($null -eq $useFrames) {
                $ours = $true
                $why  = 'no parseable use-site stack -> fail closed'
            }
            else {
                $useBlame = Get-BlameFrame -Frames @($useFrames) -RepoRootNorm $repoNorm
                if ($sawFreeSection) {
                    if ($null -eq $freeFrames) { $freeBlame = $null }
                    else { $freeBlame = Get-BlameFrame -Frames @($freeFrames) -RepoRootNorm $repoNorm }
                }

                $useVerdict  = 'ThirdParty'
                if ($null -eq $useBlame) { $useVerdict = 'Unknown' } else { $useVerdict = $useBlame.Class }

                $freeVerdict = 'n/a'
                if ($sawFreeSection) {
                    if ($null -eq $freeBlame) { $freeVerdict = 'Unknown' } else { $freeVerdict = $freeBlame.Class }
                }

                if ($useVerdict -eq 'Ours' -or $useVerdict -eq 'Unknown') {
                    $ours = $true; $why = 'innermost use-site frame is ours (or unidentifiable)'
                }
                elseif ($freeVerdict -eq 'Ours' -or $freeVerdict -eq 'Unknown') {
                    $ours = $true; $why = 'innermost free-site frame is ours (or unidentifiable)'
                }
                else {
                    $ours = $false
                    $why  = 'use site and free site are both third-party'
                }
            }
            $freeSeen = $sawFreeSection
        }
        else {
            # ---- broad rule, unchanged from the pre-script gate --------------
            # Any GeeyoouUI frame, or any GeeyoouUI path, anywhere in the
            # report.  Deliberately identical in effect to the old findstr so
            # that nothing except the use-after-free family changes meaning.
            $reportText = $r.Text.ToString()
            if ($repoNorm -and $reportText.IndexOf($repoNorm.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $ours = $true; $why = 'report text names this repository (broad rule)'
            }
            elseif ($reportText -imatch '\\GeeyoouUI\\(src|include|tests|examples)\\') {
                $ours = $true; $why = 'report text names a GeeyoouUI source tree (broad rule)'
            }
            else {
                foreach ($s in $r.Sections) {
                    foreach ($f in $s.Frames) {
                        $c = Get-FrameClass -Frame $f -RepoRootNorm $repoNorm
                        if ($c -eq 'Ours' -or $c -eq 'Unknown') {
                            $ours = $true
                            $why  = 'a frame is ours or unidentifiable (broad rule, fail closed)'
                            break
                        }
                    }
                    if ($ours) { break }
                }
                if (-not $ours) { $why = 'no GeeyoouUI frame anywhere in the report (broad rule)' }
            }

            # A report of a kind we do not model, with nothing parseable in it
            # at all, is not a pass.
            $hasFrames = $false
            foreach ($s in $r.Sections) { if ($s.Frames.Count -gt 0) { $hasFrames = $true; break } }
            if (-not $hasFrames -and -not $ours) {
                $ours = $true; $why = 'report has no parseable frames -> fail closed'
            }
        }

        if ($ours) { $anyOurs = $true }

        [void] $verdicts.Add(@{
            N = $n; Kind = $r.Kind; Tool = $r.Tool; Ours = $ours; Why = $why
            Use = $useBlame; Free = $freeBlame; Narrow = $narrow; FreeSeen = $freeSeen
        })
    }

    # -- print ----------------------------------------------------------------
    #
    # DEDUPLICATED AND CAPPED.  One injected use-after-free in a layout function
    # produced 1174 reports in a single run, and 1174 four-line verdicts on top
    # of ASan's own 1174 stack dumps is not a gate output anybody reads; it is a
    # thing people scroll past, which is the same failure mode as a gate that
    # reddens at random.  Reports are folded by (kind, use site, free site,
    # verdict) -- the same defect hit N times is one line with a count -- and
    # the fold is display only.  The verdict was computed from every report
    # above, before any of this.
    $groups = New-Object System.Collections.Specialized.OrderedDictionary
    foreach ($v in $verdicts) {
        $useTxt  = Format-Frame $v.Use
        $freeTxt = '<report carries no "freed by" stack>'
        if (-not $v.Narrow)   { $freeTxt = '' }
        elseif ($v.FreeSeen)  { $freeTxt = Format-Frame $v.Free }
        $key = '{0}|{1}|{2}|{3}' -f $v.Kind, $useTxt, $freeTxt, $v.Ours
        if ($groups.Contains($key)) { $groups[$key].Count++ }
        else {
            $groups.Add($key, @{
                Count = 1; Kind = $v.Kind; Tool = $v.Tool; Ours = $v.Ours
                Why = $v.Why; Narrow = $v.Narrow; UseTxt = $useTxt; FreeTxt = $freeTxt
                First = $v.N
            })
        }
    }

    Write-Line ''
    Write-Line '  --- AddressSanitizer reported at least one error; classifying ---'
    Write-Line ('  {0} report(s), {1} distinct site(s):' -f $reports.Count, $groups.Count)

    $shown = 0
    foreach ($k in $groups.Keys) {
        $g = $groups[$k]
        $shown++
        if ($shown -gt 20) { continue }
        $tag = 'THIRD-PARTY'
        if ($g.Ours) { $tag = 'OURS' }
        $times = ''
        if ($g.Count -gt 1) { $times = ' (x{0})' -f $g.Count }
        Write-Line ('    {0}: {1}{2}  -> {3}' -f $g.Tool, $g.Kind, $times, $tag)
        if (-not $Quiet) {
            if ($g.Narrow) {
                Write-Line ('        use  : ' + $g.UseTxt)
                Write-Line ('        freed: ' + $g.FreeTxt)
            }
            Write-Line ('        why  : ' + $g.Why)
        }
    }
    if ($groups.Count -gt 20) {
        Write-Line ('    ... and {0} more distinct site(s); full detail in the log.' -f ($groups.Count - 20))
    }

    if ($anyOurs) { exit $EXIT_OURS }
    exit $EXIT_THIRD_PARTY
}
catch {
    Write-Line ('  [classify-asan] INTERNAL ERROR, failing closed: ' + $_.Exception.Message)
    Write-Line ('  [classify-asan] at: ' + $_.InvocationInfo.PositionMessage)
    exit $EXIT_INTERNAL
}
