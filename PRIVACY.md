# Privacy policy

Fluent Serial Assistant does not include telemetry, analytics, or advertising.

The application stores its settings and user-created data locally. Serial-port
traffic, logs, exported files, protocol templates, macros, scripts, and session
data are not uploaded by the application.

The application contacts a networked system only in these cases:

- When it starts, and when **Check for updates** is selected, it sends one HTTPS
  request to the public GitHub Releases API for `txp666/FluentSerialAssistant`
  to look for a newer version. GitHub receives the network information normally
  associated with an HTTPS request, including the user's IP address and the
  `FluentSerialAssistant` user-agent string. GitHub's
  [privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement)
  applies.
- A newer version is downloaded from GitHub, with the
  `FluentSerialAssistant-Updater` user-agent string, only after the user
  confirms it in the update dialog.
- Selecting a release or project link opens that URL in the user's default web
  browser. The destination site's privacy policy then applies.

The application does not collect or retain personal data on behalf of the
project maintainer.

Questions or privacy issues can be reported through the private contact method
described in [SECURITY.md](SECURITY.md).
