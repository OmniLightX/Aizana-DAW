# Security policy

Aizana is alpha software. Security fixes target the current main branch; older
snapshots do not receive separate maintenance or a guaranteed response time.

## Reporting a vulnerability

Do not put exploit details, access tokens, private recordings or personal data in
public issues, pull requests or logs. On the repository's
[Security advisories page](https://github.com/OmniLightX/Aizana-DAW/security/advisories),
use **Report a vulnerability** when that button is available. GitHub keeps such
reports private to the reporter and the repository's security maintainers.

Private vulnerability reporting must be enabled separately by the maintainer
once the repository is public. This document alone does not enable it. If the
button is absent, open an issue asking only for a private security contact,
without describing the vulnerability, and wait for that contact before sharing
details. No public email address is currently designated for security reports.

A private report should include the affected commit/version, macOS version,
reproduction steps using synthetic data, expected and observed behavior, and
impact. Remove credentials and identifying paths from attachments.

## Trust boundaries

Installed and generated plugins are native code. Scanning a generated plugin
already loads and initializes it; execution does not start only when it is
inserted into a track. The Faust compiler is not a sandbox. Only compile or
load code you trust. See [AI Studio and MCP](docs/MCP.md) for tool permissions,
local connection authentication and external AI data processing.

CI uses synthetic fixtures. It does not establish the safety of arbitrary
third-party plugins or replace real-device and listening tests.
