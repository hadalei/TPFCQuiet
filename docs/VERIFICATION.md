# Verification snapshot: 3.5.5

This is a development snapshot dated 2026-11-14, not a claim of general hardware support.

## Software checks

| Group | Passed |
| --- | ---: |
| Fan policy | 33 |
| Vendor data/parsing | 30 |
| Legacy power restoration | 23 |
| Bounded recovery state machine | 50 |
| Engine and IPC, three editions | 70 per edition |
| Observation accounting | 12 |
| New I/O and packet-freshness regressions | 9 |
| Total | 367 |

The source preparation directory was built independently using its included `tools/build.py`. The compiler and SDK were external prerequisites; source files did not come from the private QA directory.

The new focused suite returns 7/9 against 3.5.4 and 9/9 against 3.5.5. Its two old failures reproduce isolated readback handling and premature loss of a still-fresh temperature packet. It also checks persistent mismatch, firmware/high-bit preservation, original critical temperatures, packet expiry, and clock reversal.

## Local runtime checks

All three editions passed native window smoke checks. The installed C edition was checked in Smart/Quiet Office, minimized to the tray, closed to the tray, and reopened. The canonical install retains one executable and the existing tray identity. Local profile and original TPFanControl hashes were checked across the upgrade.

The test machine is an X1 Carbon Gen 14 with an Intel Core Ultra 7 356H. Software checks use fake port functions and simulated telemetry. No real fan stall or EC fault was deliberately induced.

## What remains unmeasured

The initial 3.5.5 observation is short. There is no quantified long-term fallback-rate reduction, acoustic benchmark, or performance comparison. The 18-event chart in the development notes describes the prior 3.5.4 baseline, not the new version. A prior real automatic recovery verified one complete protection/recovery chain; it did not establish ongoing reliability.

The repository contains aggregate results and reviewed figures. It excludes raw private machine telemetry and machine-specific certification.
