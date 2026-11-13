# Changelog

## 3.5.5 (local build, 2026-11-14)

- Confirm an isolated low-bit fan readback mismatch with one bounded read-only check. Preserve original temperatures, persistent mismatches, and firmware takeover indications.
- Reserve time inside the write transaction budget for selector cleanup and record the first timed-out operation and EC status.
- Keep the previous valid named-temperature packet after an unsuccessful query until its original freshness deadline. Do not extend its age.
- Add nine focused regression checks; two reproduce the previous behavior and fail against 3.5.4.

## 3.5.4

- Capture failure evidence before BIOS handoff changes the target/readback state.
- Add bounded automatic Smart recovery for eligible sensor/write faults and an explicit waiting state in the interface.
- Verify one natural BIOS fallback and automatic recovery on the target laptop. This did not establish long-term stability.

## Earlier development

- Introduce explicit temperature provenance, named CPU/GPU protection inputs, Quiet Office control, dual-fan verification, service/tray separation, and a larger native interface.
- Remove the experimental CPU-limiting mode. Production fan profiles leave CPU power management unchanged.
