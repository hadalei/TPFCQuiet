# Contributing

Start with the build and software checks in the README. Keep port access behind `Backend`; keep UI requests separate from the service's protection checks.

For a bug report, describe the model, BIOS version, application version, control mode, and event time. Share a small reviewed excerpt rather than uploading all local diagnostics. Remove account identifiers and private paths. A single high-RPM observation does not establish whether Smart or BIOS caused it.

For a behavioral change, include a reproducible test that fails before the change. Preserve critical temperature response, BIOS handoff, packet expiry, dual-fan confirmation, and bounded retry behavior. Never silently enable fan-stop operation, copy machine certification, or add CPU limits to a fan profile.

New hardware support needs real sensor-source and fan-level verification on that machine. A passing fake-port test alone is insufficient. Describe observed behavior separately from inferred causes and quantify only effects that were measured.
