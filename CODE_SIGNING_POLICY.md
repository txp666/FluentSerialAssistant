# Code signing policy

Windows release binaries for Fluent Serial Assistant are eligible to be signed
through the open-source program operated by SignPath Foundation.

Free code signing provided by [SignPath.io](https://signpath.io/), certificate
by [SignPath Foundation](https://signpath.org/).

## Signed artifacts

Only release artifacts built from the public
[txp666/FluentSerialAssistant](https://github.com/txp666/FluentSerialAssistant)
repository by the tag-triggered GitHub Actions release workflow may be
submitted for signing.

The following project-owned files are signed:

- `FluentSerialAssistant.exe`
- `FluentSerialAssistant-<version>-windows-x64-setup.exe`

Bundled Qt and other third-party binaries are not re-signed with the project's
certificate.

## Team roles

- Committers and reviewers: [@txp666](https://github.com/txp666)
- Approvers: [@txp666](https://github.com/txp666)

All members in these roles must use multi-factor authentication for GitHub and
SignPath. Signing requests require manual approval under the SignPath release
signing policy.

## Build and release controls

- Release builds run only on GitHub-hosted runners.
- A release tag must match the version declared by CMake.
- The application is signed before Inno Setup creates the installer.
- The completed installer is submitted for a separate signature.
- The workflow verifies both Authenticode signatures before publishing.
- SHA-256 checksum files are generated only after signing is complete.

The SignPath artifact configurations used by the release workflow are stored
under [`signing/signpath`](signing/signpath).

## Privacy

See the project [privacy policy](PRIVACY.md). The application does not transfer
information to networked systems unless the user explicitly requests an action
that requires it, such as checking GitHub Releases for an update.

## Reporting abuse

Please report a suspected signing-policy violation privately using the contact
method described in [SECURITY.md](SECURITY.md). Reports concerning a SignPath
Foundation certificate can also be sent to `support@signpath.io`.
