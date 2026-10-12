# Security Policy

## Reporting a concern

Issues and pull requests are welcome for bugs, security hardening, dependency
updates, suspicious behavior, and proposed fixes.

If a report contains reproducible exploit details, secrets, private data, or
other information that could put users at risk if published, do not open a
public issue or pull request. Use
[GitHub private vulnerability reporting](https://github.com/SimplySmirkySoftware/Loka/security/advisories/new)
instead.

Useful reports include the affected release, commit, or branch; the host and
platform; the observed behavior and security impact; and minimal reproduction
steps or a proof of concept when it is safe to share one. Known mitigations and
disclosure or credit preferences are also helpful. Missing information does not
prevent a report from being considered.

Please keep sensitive details private while the report, any mitigation, and any
possible fix are being evaluated. Confirmed vulnerabilities may be published as
GitHub Security Advisories with the affected revisions and available mitigation
or fix. The project does not currently offer a bug bounty or a fixed response,
release, or backport schedule.

## Project security boundary

Loka is a framework that projects application intent onto operating-system
facilities. The framework does not independently contact a particular remote
service, collect telemetry, update itself, or download or execute remote code.
Applications explicitly choose which operating-system operations they request.

Applications under `example/` are general-purpose examples. They do not create
an undisclosed runtime dependency on an account, credential, telemetry service,
advertising service, or particular online service. External resources, where
used, are obtained only by explicitly invoked development or build scripts, not
by the Loka or example runtime.

Source dependencies must be declared in repository documentation or build
configuration. Compilers, operating-system SDKs, build tools, emulators,
hardware, and host configurations are external environment inputs and are
maintained by their respective owners.

Optional wrappers for third-party components such as SQLite or QuickJS may be
provided in this repository or a separate repository. Their upstream source,
version or revision, acquisition method, license, and integration owner must be
explicit. Maintainers intend to update repository-owned wrappers and declared
third-party versions when practicable, without promising a fixed update or
backport schedule. A separately maintained integration follows the security
policy of its own repository.

## Release integrity

Release archives produced by the repository's
[release assembler](scripts/release/README.md) are built from an explicit
allowlist and accompanied by an adjacent content manifest recording each
archived file's SHA-256 digest and provenance, allowing recipients to detect
content mismatches and inspect the recorded source of each file.

## Legacy environments

Running software on legacy operating systems, hardware, compilers, SDKs, or
emulators may involve substantial security risks, even when the environment is
offline. A `build-verified` or `runtime-verified` result records compatibility
evidence; it is not a security assessment or certification.

## Maintenance

Reports concerning the current development tree and published releases are
welcome. Security fixes are considered on a best-effort basis and normally
begin on the current development tree. Older releases may not receive security
updates or backports.

Use and distribution of Loka remain governed by the
[MIT License](LICENSE.md).
