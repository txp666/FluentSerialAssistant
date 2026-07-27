# SignPath Foundation application and setup

Fluent Serial Assistant uses SignPath Foundation instead of the paid EVSign
service used by the reference project. SignPath provides free Authenticode code
signing to accepted open-source projects.

## Application

Apply at <https://signpath.org/apply.html> after this release configuration is
merged into the public repository.

Suggested application details:

- Project: `Fluent Serial Assistant`
- Repository: `https://github.com/txp666/FluentSerialAssistant`
- Homepage: `https://github.com/txp666/FluentSerialAssistant`
- Download page: `https://github.com/txp666/FluentSerialAssistant/releases`
- Privacy policy:
  `https://github.com/txp666/FluentSerialAssistant/blob/main/PRIVACY.md`
- License: `GPL-3.0-or-later`
- Maintainer: `txp666`
- Tagline: `A modern cross-platform serial terminal and protocol debugging
  application.`
- Description: `Fluent Serial Assistant helps developers communicate with
  serial devices, inspect and export traffic, build protocol packets, plot
  incoming data, automate test sequences, and run local serial scripts through
  a modern desktop interface.`
- Reputation: `The public GPL-3.0 repository has published eight GitHub
  Releases, uses reproducible tag-triggered release automation and
  cross-platform GitHub Actions builds, and currently has four stars and one
  fork. Repository: https://github.com/txp666/FluentSerialAssistant`
- Maintainer type: `Individual maintainer(s)`
- Build system: `GitHub Actions`
- Code signing policy:
  `https://github.com/txp666/FluentSerialAssistant/blob/main/CODE_SIGNING_POLICY.md`
- Files to sign: the project-owned Windows application executable and Inno
  Setup installer

Before submitting:

1. Merge these policy, privacy, packaging, and workflow files into the public
   repository so every application URL resolves.
2. Publish at least one unsigned release containing the new Inno Setup
   installer. SignPath requires the project to be already released in the form
   that will later be signed.
3. Enable multi-factor authentication on the maintainer's GitHub account.

SignPath also requires the project to be public, actively maintained, fully
open source, and documented.

The current application form also requires the maintainer's real first name,
last name, account email, discovery channel, acceptance of the SignPath
Foundation Code of Conduct, and consent for SignPath to store and process the
account data. These identity and consent fields must be completed by the
maintainer. The optional marketing-consent checkbox is not required.

## SignPath project configuration

After acceptance:

1. Install and authorize the SignPath GitHub App for this repository.
2. Create or rename the SignPath project with slug
   `fluent-serial-assistant`.
3. Create a signing policy with slug `release-signing`, trusted-build-system
   verification, GitHub origin verification, and manual approval enabled.
4. Create artifact configuration `windows-application-v1` from
   [`signing/signpath/windows-application-v1.xml`](../../signing/signpath/windows-application-v1.xml).
5. Create artifact configuration `windows-installer-v1` from
   [`signing/signpath/windows-installer-v1.xml`](../../signing/signpath/windows-installer-v1.xml).
6. Create an API token for a SignPath user with submitter permission.
7. Add the token as the GitHub Actions secret `SIGNPATH_API_TOKEN`.
8. Add the SignPath organization ID as the GitHub Actions repository variable
   `SIGNPATH_ORGANIZATION_ID`.

The release workflow detects these two settings. If both are present, it:

1. uploads the staged Windows application as a short-lived GitHub artifact;
2. submits it to SignPath and verifies the returned application signature;
3. builds the Inno Setup installer from the signed application;
4. submits the installer to SignPath and verifies its signature;
5. generates the final SHA-256 checksum and publishes the release.

SignPath Foundation requires a human approver to approve each signing request.
The release job waits up to one hour for each approval.

Until SignPath is configured, the workflow still produces an unsigned Windows
installer and emits a warning. macOS uses an ad-hoc bundle signature for bundle
integrity; it is not Apple notarization. Debian packages are distributed with a
SHA-256 checksum but are not repository-signed.
