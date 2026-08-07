<#
    classify-asan.ps1 -- decide whether an AddressSanitizer log is OUR defect.

    Called by verify.bat step [6/6].  Exit code IS the verdict:

        0  no AddressSanitizer / LeakSanitizer report in the log at all
        2  reports present, every one of them attributable to third-party code
        1  at least one report attributable to GeeyoouUI  -> the gate must go RED
        3  the log could not be parsed, the RUN DID NOT FINISH, or this script
           threw

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
    An ASan use-after-free report has three stacks.  Two assign RESPONSIBILITY
    and the third assigns OWNERSHIP:

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
            #0 ...          <- WHO OWNS THE BYTES.  see "OWNERSHIP" below.

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

    FROZEN, DO NOT "TIDY": the $uafFamily pattern below starts an alternative
    with ^double-free, and ASan actually prints "attempting double-free on
    address ...".  The alternative therefore never matches and double-free
    reports fall through to the BROAD rule.  That is the redder of the two
    answers, so the direction is safe and it stays as it is until somebody
    lands a fixture with the real "attempting double-free" log text in
    tools\test-classify-asan.ps1.  Making the narrow rule reach a kind it has
    never been tested against is a relaxation, not a cleanup.

    ----------------------------------------------------------------------------
    OWNERSHIP (the third judgment) -- SHADOW MODE, DOES NOT AFFECT THE VERDICT
    ----------------------------------------------------------------------------
    The two blame stacks answer "who did the wrong thing".  They cannot answer
    "whose object was it", and that is the gap that leaves this residual hole:

        SetWindowTextW(hwnd, alreadyFreedString)

    -- use site user32, free site somebody else, and the classifier files it
    third-party.  But the string was OUR new.  Memory we allocated, that
    something dangles into, is our problem to fix whoever pulled the trigger.

    So Get-BlameFrame is now also run over the "previously allocated by" stack
    and the answer printed as an `alloc:` line.  It has discrimination: on
    third-party-sogou-uaf.log the allocation stack crosses the same plumbing and
    lands on SogouPY.ime, so the IME noise still classifies third-party and does
    NOT flow back in.  On ours-alloc-thirdparty-both-ends.log it lands on our
    Label constructor and says so.

    PHASE A (this change, shipped): compute it, print it, change NOTHING about
    the exit code.  Where the ownership answer WOULD have flipped a third-party
    verdict to ours, the line

        ^ SHADOW: phase B would call this report OURS (allocation-site owner).

    is printed under it.  That marker is the forensic record and it is the only
    thing phase B needs.

    PHASE B (not this change) -- HOW TO EARN THE RIGHT TO MAKE IT BINDING:

      1. Run the full gate 10 times on a machine with the IME installed, and
         run the nightly soak 3 times.  Keep every asan-run.log.
      2. Grep the 13 logs for "SHADOW:".  Count the DISTINCT sites, using the
         `alloc:` line as the key, not the number of hits.
      3. A site is admissible evidence only if the same site shows up in a
         report whose use and free sites are both third-party.  Those are the
         reports whose verdict phase B would change; nothing else matters.
      4. Promote to a hard predicate ONLY if the distinct-site count is zero for
         every known-noise site (SogouPY.ime, user32, ntdll, win32u) across all
         13 runs.  One noise site with a SHADOW marker means promoting it
         reopens exactly the flaky red this script was written to kill, and the
         answer is then to narrow the plumbing skip first, not to ship it.
      5. Whatever the outcome, write the counts into
         tools\test-classify-asan.ps1 as fixtures BEFORE changing the predicate.

    Until step 4 passes, this is a print statement.  A judgment that has been
    measured for one afternoon does not get to fail a build.

    ----------------------------------------------------------------------------
    THE RUN MUST HAVE FINISHED (log-integrity sentinel)
    ----------------------------------------------------------------------------
    Before any of the above, this script checks that the log contains the test
    suite's own closing line -- the "N cases, M failures" line that
    tests\framework\Test.cpp prints last.  No line, no verdict: exit 3, gate red.

    Without that check there is a silent fail-open.  A log that was truncated,
    redirected somewhere unexpected, or produced by a run that died before the
    suite ended, contains no "ERROR: AddressSanitizer" text, and "no reports
    found" is exit 0 -- a GREEN gate for a run that never happened.

    This is not hypothetical, it is the top entry of this quarter's pre-mortem.
    ASAN_OPTIONS=continue_on_error=1 is a VENDOR EXTENSION of MSVC's ASan, not
    an upstream option.  The day a toolchain upgrade drops it, ASan reverts to
    aborting at the first report -- and on this machine the first report is the
    IME's, fired during window teardown, i.e. every run dies mid-suite from
    somebody else's bug.  With the sentinel that is a red gate saying exactly
    what broke.  Without it, it is a red gate saying nothing, which gets muted,
    and then the memory-safety leg is decorative.

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
      * a log with no suite-summary line at all                -> exit 3, red

    That last-but-one is a choice worth stating: vendored dependencies are code
    we compile and ship, so a report inside blend2d is our problem to fix or to
    pin, not environmental noise.  Only code we neither wrote nor build is
    third-party.

    The asymmetry is the whole point.  A false red costs somebody an hour.  A
    false green costs a use-after-free in a release -- and all five that the R2
    reviews found were green in both non-ASan legs, which is to say this leg is
    the only thing standing between that class of defect and a shipped build.

    NO PREDICATE IN THIS FILE MAY BE WIDENED WITHOUT A FIXTURE FIRST.  That is
    not a style note, it is a process rule: add the case to
    tools\test-classify-asan.ps1, with the REAL log text, watch it fail, then
    change the rule.  The self-test runs inside verify.bat step [6/6] now
    (see :classify_asan), so a widening with no fixture is a widening the gate
    will not accept.
#>

[CmdletBinding()]
param(
    # The ASan run log to classify.  Never pass a directory path ending in a
    # backslash from batch: "%FOO%" where FOO ends in \ makes the CRT eat the
    # closing quote and the argument list collapses.  That exact bug already
    # cost this repository a blind findstr once; see verify.bat.
    #
    # NOT [Parameter(Mandatory)] any more -- a mandatory parameter PROMPTS when
    # this file is dot-sourced, and the self-test dot-sources it (see
    # -DefineOnly).  The missing-argument case is checked explicitly at the
    # bottom instead, and it exits 3, which is the same red the prompt-that-
    # nobody-answers would eventually have produced.
    [string] $LogPath = '',

    # Defaults to this script's parent directory, which is the repo root.
    # Exists as a parameter only so the self-test can point it somewhere else.
    [string] $RepoRoot = '',

    # Print the verdict lines only, no per-frame detail.
    [switch] $Quiet,

    # Load the functions and return WITHOUT classifying anything and WITHOUT
    # exiting.  Only tools\test-classify-asan.ps1 uses this: it dot-sources this
    # file once and then calls Invoke-ClassifyAsan in-process for every fixture,
    # because one powershell.exe per fixture costs ~0.6s of cold start and the
    # self-test has a wall-clock budget inside the gate.  The exit-code plumbing
    # that in-process calls skip is covered separately there, by running a few
    # fixtures through the real `powershell -File` path.
    [switch] $DefineOnly
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'

$EXIT_CLEAN       = 0
$EXIT_OURS        = 1
$EXIT_THIRD_PARTY = 2
$EXIT_INTERNAL    = 3

# Captured at load time, not at call time: when this file is dot-sourced,
# $PSScriptRoot inside a function called later resolves against the CALLER's
# script, not this one.
$script:ClassifierDir = $PSScriptRoot

# Everything this script prints goes through here, and everything it prints is
# also recorded, so the in-process self-test can assert on the TEXT (the shadow
# `alloc:` line has no exit code of its own -- if it is not asserted on, it is
# not tested).  [Console]::Out rather than Write-Host so nothing lands in the
# PowerShell output stream and gets confused with a return value.
$script:Emitted = New-Object System.Collections.ArrayList
$script:Silent  = $false
function Write-Line {
    param([string] $Text = '')
    [void] $script:Emitted.Add($Text)
    if (-not $script:Silent) { [Console]::Out.WriteLine($Text) }
}

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

# Where a frame physically lives: its source file if it has one, otherwise its
# module.  '' when it has neither, and '' NEVER compares equal to anything --
# see Get-BlameFrame, where an empty origin must not be allowed to look like a
# match, because matching means skipping and skipping means greener.
function Get-FrameOrigin {
    param([hashtable] $Frame)
    if ($Frame.File)   { return $Frame.File.Replace('/', '\') }
    if ($Frame.Module) { return $Frame.Module.Replace('/', '\') }
    return ''
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
    # as flaky as before, while LOOKING like it had been fixed.
    #
    # THE SKIP IS ANCHORED, NOT WINDOWED.  It used to scan the first 8 frames
    # for the OUTERMOST operator new/delete and start blaming after it, and that
    # had a false-GREEN in it, which is the one direction this file may never
    # fail in.  A nested destructor puts a SECOND operator delete further out:
    #
    #     #0 _asan_wrap...                             <- runtime
    #     #1 gyFree                    Test.cpp        <- plumbing
    #     #2 operator delete           Test.cpp        <- plumbing
    #     #3 geeyoou::Widget::~Widget  Widget.cpp      <- THE FREE SITE. ours.
    #     #4 operator delete           Test.cpp        <- the OUTER delete, of
    #                                                     the parent object
    #     #5 <third-party smart pointer>
    #
    # "Outermost operator frame within 8" answers #5 there and files our own
    # use-after-free as somebody else's.  The 8 was the tell: a magic number
    # over a structure means the structure was never worked out.
    #
    # So instead:
    #
    #   1. skip the contiguous run of ASan-runtime frames at the bottom;
    #   2. take the origin (source file, else module) of the first frame after
    #      it, and the CONTIGUOUS run of frames sharing that exact origin;
    #   3. if an operator new / operator delete frame is INSIDE that run, the
    #      whole run is allocator plumbing -- skip it and blame the first frame
    #      after;  otherwise skip nothing.
    #
    # The run stops at the first frame from a different file or module, so a
    # real frame can never be stepped over to reach a further-out operator: #3
    # above ends the run and gets the blame.  It also still works with no global
    # replacement at all (the stock MSVC thunk is compiler-rt, i.e. Runtime, so
    # step 1 alone lands on the real free site), and it needs no knowledge of
    # the name `gyFree`.
    #
    # An empty origin (no file AND no module) never matches, so a run of
    # unsymbolised frames is never mistaken for plumbing -- unsymbolised frames
    # are Unknown, which is Ours, which is the answer we want to keep reachable.
    #
    # Limitation, stated rather than hidden: a defect INSIDE the allocator
    # replacement itself is attributed to its caller.  That caller is our test
    # framework in every stack we have seen, so the gate is still red; it would
    # only matter if third-party code called our operator delete and our
    # operator delete were the buggy one.
    $p = 0
    while ($p -lt $Frames.Count -and
           (Get-FrameClass -Frame $Frames[$p] -RepoRootNorm $RepoRootNorm) -eq 'Runtime') {
        $p++
    }

    $start = $p
    if ($p -lt $Frames.Count) {
        $anchor = Get-FrameOrigin $Frames[$p]
        if ($anchor) {
            # The contiguous run of frames from $p that share the anchor origin.
            $q = $p
            while ($q -lt $Frames.Count) {
                $o = Get-FrameOrigin $Frames[$q]
                if (-not $o) { break }
                if (-not $o.Equals($anchor, [System.StringComparison]::OrdinalIgnoreCase)) { break }
                $q++
            }
            # Plumbing only if the run actually contains an allocator operator.
            for ($i = $p; $i -lt $q; $i++) {
                if ($Frames[$i].Func -match '^operator\s+(new|delete)\b') { $start = $q; break }
            }
        }
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
# The log-integrity sentinel.  See "THE RUN MUST HAVE FINISHED" in the header.
#
# tests\framework\Test.cpp ends every run with, literally:
#
#     std::printf("\n%zu ...\n", cases.size(), failedCases);
#
# where the ... is Chinese, compiled with /utf-8 (CMakeLists.txt), so the bytes
# that reach the log are UTF-8 and this script reads the log as latin-1 -- see
# the ReadAllBytes call.  Matching the Chinese TEXT would therefore mean pinning
# an encoding, and the day somebody drops /utf-8 the bytes become GBK and the
# gate goes red for the wrong reason.
#
# So match the SHAPE, which survives every single-byte and multi-byte encoding
# of the same words:  <digits> <non-ascii word> <digits> <non-ascii word>
# and nothing else on the line.  A real line is
#
#     208 <12 non-ascii bytes> 0 <9 non-ascii bytes>
#
# The length caps are there so a long Chinese [note] line that happens to start
# with a number cannot satisfy the sentinel -- a false match here is a false
# GREEN, which is the failure this whole check exists to prevent.
#
# If you changed the format of that printf, change this, and add the new text to
# tests\data\asan\ as a fixture at the same time.  The coupling is deliberate
# and it is one line each side; the alternative is a gate that cannot tell a
# finished run from a dead one.
$script:SuiteSummaryPattern =
    '^[ \t]*\d+[ \t]+[^\x00-\x7F]{2,24}[ \t]*\d+[ \t]+[^\x00-\x7F]{2,24}[ \t]*$'

function Test-SuiteFinished {
    param([array] $Lines)
    foreach ($l in $Lines) {
        if ($l -match $script:SuiteSummaryPattern) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# The whole classifier, as a function that RETURNS the exit code rather than
# calling exit.  That is what lets the self-test drive it in-process; the script
# body at the bottom is the only place `exit` appears.
# ---------------------------------------------------------------------------
function Invoke-ClassifyAsan {
    param(
        [string] $LogPath,
        [string] $RepoRoot = '',
        [switch] $Quiet,
        # Record output but do not write it to the console.  Self-test only.
        [switch] $Silent
    )

    $script:Emitted = New-Object System.Collections.ArrayList
    $script:Silent  = [bool] $Silent

    try {
        if (-not $RepoRoot) { $RepoRoot = Split-Path -Parent $script:ClassifierDir }
        $repoNorm = $RepoRoot.Replace('/', '\').TrimEnd('\')
        if ($repoNorm) { $repoNorm = $repoNorm + '\' }

        if (-not (Test-Path -LiteralPath $LogPath)) {
            Write-Line ('  [classify-asan] log not found: ' + $LogPath)
            return $EXIT_INTERNAL
        }

        # Read as latin-1 so no byte sequence can throw and no decoder can mangle
        # the ASCII we match on.  The suite prints Chinese notes; we never match
        # against those, and a mojibake note is better than a decoder exception
        # taking the gate down.
        $bytes = [System.IO.File]::ReadAllBytes($LogPath)
        $text  = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
        $lines = $text -split "`r`n|`n|`r"

        # -- FIRST: did the run finish at all? --------------------------------
        # Before parsing, before counting reports, before anything that could
        # return 0.  See the header.
        if (-not (Test-SuiteFinished -Lines $lines)) {
            Write-Line ''
            Write-Line '  [classify-asan] ASan 运行未跑完或日志不完整：检查 continue_on_error 是否仍被工具链支持。'
            Write-Line ('  [classify-asan] no test-suite summary line in: ' + $LogPath)
            Write-Line '                  The run did not reach the end of the suite, or the log was'
            Write-Line '                  truncated / redirected.  Two things to check, in this order:'
            Write-Line '                    1. ASAN_OPTIONS=continue_on_error=1 (verify.bat) -- an MSVC'
            Write-Line '                       EXTENSION.  If the toolchain stopped supporting it, ASan'
            Write-Line '                       aborts at the first report and every run dies mid-suite.'
            Write-Line '                    2. tests\framework\Test.cpp still prints its summary line,'
            Write-Line '                       and $script:SuiteSummaryPattern here still matches it.'
            Write-Line '                  Failing closed (exit 3): a run that did not happen is not a pass.'
            return $EXIT_INTERNAL
        }

        # -- split the log into reports, and each report into stack sections ---
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

        if ($reports.Count -eq 0) { return $EXIT_CLEAN }

        # -- classify ---------------------------------------------------------
        # The narrow rule applies only to the family that actually produces the
        # noise: reports that carry a "freed by" stack.
        # ^double-free never matches what ASan prints.  FROZEN -- see header.
        $uafFamily = '^(heap-use-after-free|double-free|alloc-dealloc-mismatch|attempting free on address which was not malloc)'

        $anyOurs = $false
        $verdicts = New-Object System.Collections.ArrayList
        $n = 0

        foreach ($r in $reports) {
            $n++
            $ours = $false
            $why  = ''
            $useBlame   = $null
            $freeBlame  = $null
            $allocBlame = $null
            $allocShadowFlips = $false
            $narrow    = $false
            $freeSeen  = $false
            $allocSeen = $false

            if ($r.Tool -eq 'AddressSanitizer' -and $r.Kind -imatch $uafFamily) {
                # ---- narrow rule: who used it, and who freed it ---------------
                $narrow      = $true
                $useFrames   = $null
                $freeFrames  = $null
                $allocFrames = $null
                $sawFreeSection  = $false
                $sawAllocSection = $false

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
                    if ($s.Header -match '^(previously allocated|allocated) by thread') {
                        # OWNERSHIP, shadow mode.  Not blame -- see the header.
                        $sawAllocSection = $true
                        if ($null -eq $allocFrames) { $allocFrames = $s.Frames }
                        continue
                    }
                    if ($s.Header -match '^Thread T\d+ created by') { continue }
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
                    if ($sawAllocSection -and $null -ne $allocFrames) {
                        $allocBlame = Get-BlameFrame -Frames @($allocFrames) -RepoRootNorm $repoNorm
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

                    # SHADOW ONLY.  $ours is already decided above and is NOT
                    # touched here.  This records the reports whose verdict the
                    # ownership rule WOULD change, which is the entire dataset
                    # phase B needs; see the header for how to read it.
                    if (-not $ours -and $sawAllocSection) {
                        $allocVerdict = 'Unknown'
                        if ($null -ne $allocBlame) { $allocVerdict = $allocBlame.Class }
                        if ($allocVerdict -eq 'Ours' -or $allocVerdict -eq 'Unknown') {
                            $allocShadowFlips = $true
                        }
                    }
                }
                $freeSeen  = $sawFreeSection
                $allocSeen = $sawAllocSection
            }
            else {
                # ---- broad rule, unchanged from the pre-script gate ------------
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
                Use = $useBlame; Free = $freeBlame; Alloc = $allocBlame
                Narrow = $narrow; FreeSeen = $freeSeen; AllocSeen = $allocSeen
                AllocShadowFlips = $allocShadowFlips
            })
        }

        # -- print -------------------------------------------------------------
        #
        # DEDUPLICATED AND CAPPED.  One injected use-after-free in a layout function
        # produced 1174 reports in a single run, and 1174 four-line verdicts on top
        # of ASan's own 1174 stack dumps is not a gate output anybody reads; it is a
        # thing people scroll past, which is the same failure mode as a gate that
        # reddens at random.  Reports are folded by (kind, use site, free site,
        # alloc site, verdict) -- the same defect hit N times is one line with a
        # count -- and the fold is display only.  The verdict was computed from
        # every report above, before any of this.
        $groups = New-Object System.Collections.Specialized.OrderedDictionary
        foreach ($v in $verdicts) {
            $useTxt  = Format-Frame $v.Use
            $freeTxt = '<report carries no "freed by" stack>'
            if (-not $v.Narrow)   { $freeTxt = '' }
            elseif ($v.FreeSeen)  { $freeTxt = Format-Frame $v.Free }
            $allocTxt = ''
            if ($v.Narrow -and $v.AllocSeen) { $allocTxt = Format-Frame $v.Alloc }
            $key = '{0}|{1}|{2}|{3}|{4}' -f $v.Kind, $useTxt, $freeTxt, $allocTxt, $v.Ours
            if ($groups.Contains($key)) { $groups[$key].Count++ }
            else {
                $groups.Add($key, @{
                    Count = 1; Kind = $v.Kind; Tool = $v.Tool; Ours = $v.Ours
                    Why = $v.Why; Narrow = $v.Narrow; UseTxt = $useTxt; FreeTxt = $freeTxt
                    AllocTxt = $allocTxt; ShadowFlips = $v.AllocShadowFlips
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
                    if ($g.AllocTxt) {
                        Write-Line ('        alloc: ' + $g.AllocTxt)
                        if ($g.ShadowFlips) {
                            Write-Line ('        ^ SHADOW: phase B would call this report OURS (allocation-site owner).')
                        }
                    }
                }
                Write-Line ('        why  : ' + $g.Why)
            }
        }
        if ($groups.Count -gt 20) {
            Write-Line ('    ... and {0} more distinct site(s); full detail in the log.' -f ($groups.Count - 20))
        }

        if ($anyOurs) { return $EXIT_OURS }
        return $EXIT_THIRD_PARTY
    }
    catch {
        Write-Line ('  [classify-asan] INTERNAL ERROR, failing closed: ' + $_.Exception.Message)
        Write-Line ('  [classify-asan] at: ' + $_.InvocationInfo.PositionMessage)
        return $EXIT_INTERNAL
    }
}

# ---------------------------------------------------------------------------
# Script body.  The only `exit` in the file.
# ---------------------------------------------------------------------------
if ($DefineOnly) { return }

if (-not $LogPath) {
    Write-Line '  [classify-asan] no -LogPath given; refusing to answer.'
    exit $EXIT_INTERNAL
}

exit (Invoke-ClassifyAsan -LogPath $LogPath -RepoRoot $RepoRoot -Quiet:$Quiet)
