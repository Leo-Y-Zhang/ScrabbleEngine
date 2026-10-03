# Code signing policy

*Draft for the owner to confirm before applying to SignPath Foundation (see
`docs/windows.md`). It takes effect once the Windows releases are signed.*

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

## What is signed

Only `tilefish.exe` in the Windows release download, built from this repository's source
by `.github/workflows/release.yml` on GitHub-hosted runners. Nothing built elsewhere,
and no third-party program, is signed under this policy.

## Team roles

- **Committers and reviewers:** the members of the `Leo-Y-Zhang` GitHub account with write
  access to this repository.
- **Approvers:** the repository owner. Every signing request is approved by hand in
  SignPath before a release is signed.

## Privacy

Tilefish does not send any information to other networked systems unless the user asks
it to. The only network access is `get-lexicon`, which the user starts to download a
word list from a fixed public address.
