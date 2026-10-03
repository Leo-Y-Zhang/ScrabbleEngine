# Windows: the word-list download, and code signing

## Why v2.2's `get-lexicon.bat` failed, and what v2.2.1 got wrong about it

**What happened.** On the owner's laptop, v2.2's `get-lexicon.bat` stopped at the checksum:

```
Get-FileHash : The term 'Get-FileHash' is not recognized as the name of a cmdlet, ...
At ...\get-lexicon.ps1:25 char:29
    + FullyQualifiedErrorId : CommandNotFoundException
```

**What v2.2.1 said, which was wrong.** Its commit, its script comment and its release test
described this as "a Windows PowerShell 5.1 setup without Get-FileHash". Windows
PowerShell 5.1 does include `Get-FileHash` (in `Microsoft.PowerShell.Utility` 3.1.0.0).

**The cause, measured on that laptop** (Windows 11, Windows PowerShell 5.1.26100,
PowerShell 7.6.6, 4 October 2026):

- PowerShell 7 adds its own module folders to the front of the `PSModulePath` environment
  variable of its process. Programs started from a PowerShell 7 prompt inherit it.
- When PowerShell 7 itself starts `powershell.exe`, it removes those folders again. When
  something in between starts it, such as `cmd.exe` running a `.bat` file, they stay.
- Windows PowerShell 5.1 then finds PowerShell 7's `Microsoft.PowerShell.Utility`
  (version 7.0.0.0) first and loads it instead of its own. That module does not provide
  `Get-FileHash` to Windows PowerShell, so the command is missing. (The same inherited
  path is why `Microsoft.PowerShell.Security` fails to load there.)
- The user and machine environment variables are clean: `PSModulePath` is unset for
  the user and holds only the two Windows PowerShell folders for the machine. Nothing on
  the laptop is misconfigured; the variable comes from the parent process.

| How `powershell.exe` 5.1 was started | Utility module found first | `Get-FileHash` |
|---|---|---|
| PowerShell 7 → `powershell.exe` | 3.1.0.0 (5.1's own) | present |
| PowerShell 7 → `cmd.exe` (a `.bat`) → `powershell.exe` | 7.0.0.0 (PowerShell 7's) | **missing** |
| as a double-click from Explorer gives it (registry `PSModulePath` only) | 3.1.0.0 | present |
| PowerShell 7 → the fixed `.bat` (clears `PSModulePath`) → `powershell.exe` | 3.1.0.0 | present |

So a double-click from Explorer works. Running the file from a PowerShell 7 window, which
is Windows Terminal's default on many machines, does not.

**The fix.** `get-lexicon.bat` now runs `setlocal` and clears `PSModulePath` before
starting Windows PowerShell, which then builds its own default module path. The `.ps1`
keeps hashing through .NET (`System.Security.Cryptography.SHA256`) as a fallback, so the
script also works however it is started.

**What each test shows** (`selftest.yml`, job `windows-lexicon`, on every push):

1. *The condition.* From PowerShell 7 through `cmd.exe`, Windows PowerShell 5.1 finds no
   `Get-FileHash`; with `PSModulePath` cleared, it does.
2. *The original failure.* v2.2's own `get-lexicon.bat` and `.ps1`, started from
   PowerShell 7, stop with `CommandNotFoundException` for `Get-FileHash`. This reproduces
   the original problem.
3. *The fix on its own.* Today's `.bat` runs v2.2's script, which still calls
   `Get-FileHash`, to completion. The `.bat` change alone resolves the failure.
4. *Today's files.* From PowerShell 7, today's `.bat` downloads and verifies CSW24, and a
   second run recognises the files.
5. *The fallback.* With `Get-FileHash` made to throw, the script still succeeds. This
   tests the fallback. It does not reproduce the original problem, which the v2.2.1
   release test did not do either.

Commands to see it on a Windows machine with PowerShell 7 installed, from a PowerShell 7
prompt:

```powershell
cmd /c "powershell -NoProfile -Command ""[bool](Get-Command Get-FileHash -ErrorAction SilentlyContinue)"""   # False
powershell -NoProfile -Command "[bool](Get-Command Get-FileHash -ErrorAction SilentlyContinue)"            # True
```

## Code signing

Smart App Control (Windows 11) blocks unsigned programs it has no reputation for, with
no override, and it decides file by file. On the owner's laptop it let v2.2's
`tilefish.exe` run and blocked v2.2.1's. The fix is a code signature. The browser
version needs no download in the meantime.

**Chosen service: SignPath Foundation** (free code signing for open-source projects,
signpath.org). The certificate is issued to SignPath Foundation, not to a person, so no
personal name or address appears in the signature. Tilefish meets the published
conditions as far as can be checked from here: an OSI-approved licence (GPL-3.0), a
public repository, and release builds made from source on GitHub-hosted runners.

**Already in place (this repository):**

- `release.yml` uploads the unsigned `tilefish.exe`, submits it with SignPath's GitHub
  action (`signpath/github-action-submit-signing-request@v1`), waits for the signed
  file, checks its Authenticode signature with `Get-AuthenticodeSignature`, runs the
  self-test on the signed program, and packages that. These steps run only when the
  repository variable `SIGNPATH_ORGANIZATION_ID` is set; until then releases are built
  unsigned, as before.
- `docs/code-signing-policy.md`, the policy page SignPath Foundation asks projects to
  publish, ready for the owner to confirm.
- The artifact configuration for SignPath (below).

**The owner's actions, in order** (each needs the owner's own accounts, so none can be
done from here):

1. Apply at signpath.org for SignPath Foundation's open-source programme, for
   `github.com/Leo-Y-Zhang/ScrabbleEngine`, and wait for approval. Both the GitHub
   account and the SignPath account need two-factor authentication.
2. In SignPath, once approved: create the project with slug `ScrabbleEngine`, connect
   GitHub as its trusted build system (SignPath's GitHub app on this repository), add
   the artifact configuration below, and create the signing policy `release-signing`
   with the owner as approver.
3. Create a CI user (submitter) in SignPath and an API token for it. In this repository
   on GitHub, add the secret `SIGNPATH_API_TOKEN` (the token) and the variable
   `SIGNPATH_ORGANIZATION_ID` (from SignPath's organisation settings). If the project or
   policy slug differs, also set `SIGNPATH_PROJECT_SLUG` or `SIGNPATH_POLICY_SLUG`.
4. Run the `release` workflow. It pauses at the signing step until the owner approves
   the request in SignPath. Afterwards, check on the laptop that Smart App Control lets
   the signed `tilefish.exe` run; that has to be checked on the machine itself.

Artifact configuration (the uploaded artifact is a zip holding `tilefish.exe`):

```xml
<?xml version="1.0" encoding="utf-8"?>
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="tilefish.exe">
      <authenticode-sign />
    </pe-file>
  </zip-file>
</artifact-configuration>
```
