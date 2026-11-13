# Test59 X4 Pro handoff

Prepared November 13, 2026. The software checks pass. Fresh reader backup,
installation, runtime measurements, and physical acceptance are pending.

| Item | Verified value |
| --- | --- |
| About version | `2.0.0-chad.3-test59-x4pro` |
| Firmware source | `d8d6005a1d5dfae3be95507babe7f9b271800d70` |
| Source tree | `6c217f59776831c7691f3caa74b5d3f6376cfb66` |
| Build | `pio run -e x4pro-chad-test` |
| SDK pin | `5e94fe7480f019c89fbf691aef87523eb1b46704` |
| Host checks | 2,451 passed, zero failed; 126.91 seconds |
| X4 Pro build | Passed in 108.60 seconds; static RAM 113,100 bytes, flash 5,805,946 bytes |
| Candidate | `TEST59.BIN`, 5,811,216 bytes |
| Candidate SHA-256 | `adda27fc50ffe070301f8d12ee1c4a8b35cd5834e6773ee83c0bbf52cf5ae0f4` |
| Official fallback | `RBACK20.BIN`, 5,377,104 bytes |
| Fallback SHA-256 | `eec8b22b96202da31c91ec73f4af3fa9e3149658c3cf38dab7aa2b810ff34b44` |

The final image has a valid ESP32-S3 header, checksum, validation hash, and
reader version string. The fallback matches the official release asset digest.
The [cumulative record](CUMULATIVE_CANDIDATE.md) describes both review rounds,
the bounded notification follow-up, enabled flags, SDK patches, and limitations.

## Local package

The ignored `.cache/device-test/test59-d8d6005a/` folder contains both images,
`MANIFEST.json`, `BUILD_FLAGS.txt`, `IMAGE_INFO.txt`, `QA.md`, `README.md`, and
`SHA256SUMS`. From that folder, run:

```sh
shasum -a 256 -c SHA256SUMS
```

All seven listed files pass. The manifest records the three patched SDK source
hashes and verification of all 22 SDK library links. Binaries and device state
are not committed to Git.

## Device handoff

1. Reidentify the reader and verify a fresh, coherent backup using
   [the coverage checklist](DEVICE_TEST.md). Preserve existing firmware files.
   Include completion journals/intents/history and any affected books and
   sidecars; progress/settings alone may not describe an active move.
2. Copy the candidate under `/zDev/TEST59.BIN`, read it back, and verify its
   SHA-256. Keep development artifacts under `/zDev/`.
3. Chad selects **Settings → System → SD Card Firmware Update → zDev →
   TEST59.BIN** and keeps the reader powered through restart.
4. Confirm the About version and run the six steps in
   [the five-minute check](FIVE_MINUTE_DEVICE_QA.md).

No reader backup, SD write, or installation has occurred for this candidate.
Clipfix2 remains the last reported device-tested baseline.

## Rollback

Preserve new reading progress before rolling back. A reader that opens its
update menu can install `/zDev/RBACK20.BIN`; a nonbooting reader may need the
official USB flasher. Restore book paths, sidecars, transaction state, and
reader metadata from one coherent snapshot. The original source is the
[official CrossPoint 2.0.0 X4 Pro asset](https://github.com/crosspoint-reader/crosspoint-reader/releases/download/2.0.0/crosspoint-2.0.0-x4pro.bin).

Longer completion-move, backup/restore, TLS-failure, and throughput checks use
disposable books in separate sessions. Compilation, hashes, and host checks do
not establish those physical results.
