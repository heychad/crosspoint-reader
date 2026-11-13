# Personal X4 Pro update channel

G4 targets stable releases from `heychad/crosspoint-reader`, using the existing
reader update screen and human confirmation before installation. The repository
is public; the reader does not need a GitHub credential. Published personal tags
use `X.Y.Z-chad.N`, and only a strictly newer personal release may be offered.
The existing release workflow builds only the X4 Pro asset.
For example, `2.0.0-chad.4` supplies
`crosspoint-2.0.0-chad.4-x4pro.bin`. The numbered release must increase even when
the installed firmware is a test build.

Online checking and installation remain disabled by
`personalupdate::AUTHENTICATED_TRANSPORT`. This document tracks the components
needed before that gate can open. Nothing in this WIP publishes a release or
installs firmware.
The disabled update screen directs the reader to SD-card installation and does
not start Wi-Fi. Reader-state backup and a known-good rollback image remain
part of the device handoff.

## Verified TLS foundation

The pinned SDK's explicit CA mode now:

- Clears a previous `setInsecure()` selection when `setCACert()` is called.
- Selects peer verification and refuses a failed root-CA load.
- Configures hostname verification before the handshake and stops if that
  setup fails. SNI alone does not verify the certificate's hostname.
- Repeats CA and hostname setup on the existing TLS 1.2 retry. A verification
  failure never selects insecure mode.
- Releases the TLS context, session, and socket on setup or handshake failure.

The [wolfSSL verification contract](https://www.wolfssl.com/documentation/manuals/wolfssl/group__Setup.html)
defines peer verification and the separate hostname check. The existing book
and plugin downloader still explicitly selects its previous insecure mode;
this patch does not change its connection policy.

`scripts/patch_freeink_tls.py` applies
`scripts/freeink_patches/0003-verified-tls-hostname.patch` only to X4 Pro builds.
It reuses the existing patch verifier: SDK revision, original/patched source,
and patch bytes are hash-pinned. A repeated application is a checked no-op;
unrecognized source changes, a changed patch, a wrong SDK revision, and escaping
paths stop the build. The SDK stays at
`5e94fe7480f019c89fbf691aef87523eb1b46704`; its existing SDMMC/filesystem patches
stay intact. SDK upgrades require reviewing this small TLS patch as well.

The patch adds no task, persistent state, or permanent buffer. Explicit CA mode
uses wolfSSL's existing trust-store allocations and its hostname copy, released
with the TLS session. Hostname setup allocation failure stops the connection.
The updater's future fixed-origin transport must budget those allocations with
TLS and the active screen, using separate internal/PSRAM heap measurements.

## Bounded release metadata

The check path now clears its previous offer before fetching. On the X4 Pro it
collects at most 64 KiB into a checked PSRAM allocation, with one terminator byte
and no internal-RAM fallback. A larger response stops the request. The updater
gate remains closed; the generic downloader will be replaced before enabling it.

`PersonalRelease` validates JSON syntax, nesting, UTF-8, and a complete single
document before using the existing ArduinoJson parser. A bounded SAX audit
rejects duplicate critical keys, including escaped spellings. The syntax check
is necessary because ArduinoJson also accepts single quotes and unquoted keys.
The existing 16 KiB settings allocator caps both filter and document allocations
together. The temporary SAX owner is released before that parser is created.
Unknown release notes and uploader data do not become stored metadata.

The record must describe a stable, non-draft personal tag, one exact
`crosspoint-<tag>-x4pro.bin` asset, its canonical fork/tag download URL,
`state: uploaded`, a positive image size fitting the inactive partition, and a
lowercase `sha256:` digest. Duplicate matching assets, more than 16 assets,
wrong field types, and incomplete records fail. Tag lengths and digit limits
match the release workflow. Empty/nonmatching asset lists mean no update.
Failed parsing clears every output field.

The updater retains the validated digest in 32 additional bytes per update
session. It does not yet compare that digest with downloaded firmware. The
64 KiB response cap and 16-asset limit are release-maintenance constraints;
larger release records must be shortened or the bounded policy reviewed.

Sixteen metadata/version checks pass, including every truncated prefix of the
fixture, duplicate keys, malformed/extended JSON, nested shadow fields, Unicode,
memory limits, typed sizes, URL/tag binding, and malformed digests. Both
AddressSanitizer and UndefinedBehaviorSanitizer pass. The standard X4 Pro build
passes in 72.13 seconds (RAM 113,060 bytes; flash 5,682,130 bytes). The closed
gate can remove unused updater code from the final image; a later enabled
transport build and runtime measurements must cover its full working set.

## Remaining integration

- A request path that selects trusted roots, checks the clock, restricts every
  redirect to the expected HTTPS origins, and never uses the generic insecure
  downloader for metadata or firmware.
- Streaming payload verification against the stored metadata. GitHub's
  [release-asset API](https://docs.github.com/en/rest/releases/assets) exposes
  the asset's SHA-256 digest; metadata parsing now requires it.
- Exact byte count, SHA-256, image header, and X4 Pro board-tag checks before
  selecting the new boot partition. Failed downloads must abort the inactive
  slot and retain the current boot target.
- Cumulative release review, reproducible release artifacts, and physical
  success/failure checks on the reader. No release is published by WIP pushes.

## Verification boundary

The host harness executes the production setters, connection setup, and retry
methods with controlled wolfSSL/socket adapters. It checks failed CA/hostname
setup, verification-failure propagation through both attempts, successful
fallback with verification retained, allocation-failure cleanup, TCP failure,
and explicit insecure compatibility. It passes AddressSanitizer and
UndefinedBehaviorSanitizer. These adapters do not perform real certificate
cryptography or establish on-device TLS behavior.

Six patch tests cover clean/repeated application, source drift, patch drift,
wrong revision, escaping target, and a missing patch. Live certificate-chain,
hostname/date rejection, resource use, and updater installation remain unproven.

## Build source ownership

A copied default-profile cache contained 22 `.pio-link` files whose `cwd`
still named `crosspoint-reader-upstream-20261111`. PlatformIO resolved its SDK
libraries from that sibling checkout, even though patch scripts targeted this
checkout. The initial default build therefore did not validate this TLS patch
and is excluded from its build evidence. The combined-profile links already
pointed here.
Earlier default-profile records from this copied cache also do not prove this
checkout's SDK inputs. The corrected build below supersedes those records for
the current source. Historical timings remain recorded as such.

The first pre-build hook, `scripts/repair_local_sdk_links.py`, now rebinds cached
relative `freeink-sdk/` library references to the current checkout before library
discovery. It preserves other dependencies and metadata, writes replacements
atomically, and refuses escaping paths. Six host cases cover repair/idempotency,
preserved non-SDK links, invalid targets, symlink metadata, an external SDK,
escaping environment names, and a missing cache. This fixes build provenance;
it does not change the SDK revision or edit the sibling checkout.

The corrected default X4 Pro build passes in 116.03 seconds (RAM 113,060 bytes;
flash 5,682,114 bytes). Its compiled TLS object identifies this checkout's SDK
source. The eleven-flag profile passes in 71.21 seconds (RAM 113,092 bytes;
flash 5,783,834 bytes). The full 2,390-test host run passes in 458.59 seconds,
concurrent with firmware compilation. The final 47 focused checks pass in 1.99
seconds, including the newly registered link repair tests. Firmware builds and
controlled host adapters do not validate a live TLS handshake or device update.
