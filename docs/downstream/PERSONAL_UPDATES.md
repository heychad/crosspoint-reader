# Personal X4 Pro update channel

G4 targets stable releases from `heychad/crosspoint-reader`, using the existing
reader update screen and human confirmation before installation. The repository
is public; the reader does not need a GitHub credential. Published personal tags
use `X.Y.Z-chad.N`, and only a strictly newer personal release may be offered.
The existing release workflow builds only the X4 Pro asset.
For example, `2.0.0-chad.4` supplies
`crosspoint-2.0.0-chad.4-x4pro.bin`. The numbered release must increase even when
the installed firmware is a test build. The comparison accepts installed
versions with descriptive test labels such as `2.0.0-chad.3-test65-rc3-fixes-x4pro`.
The offered version must use the stable release format. The local OTA pipeline
test covers this exact installed version.

The base `x4pro` profile keeps online checking and installation disabled.
`CROSSPOINT_EXPERIMENTAL_PERSONAL_UPDATES=1` enables the update-only HTTPS
implementation through `personalupdate::transportAvailable()`. The personal
test and stable profiles use the same feature list. Stable OTA publication
requires cumulative review and device validation.
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
The fixed-origin transport uses this CA path. Its TLS allocations and the
active screen still need separate internal/PSRAM heap measurements.

## Bounded release metadata

The check path now clears its previous offer before fetching. On the X4 Pro it
collects at most 64 KiB into a checked PSRAM allocation, with one terminator byte
and no internal-RAM fallback. A larger response stops the request. The updater
standard-build gate remains closed. Metadata and image requests use a separate
synchronous transport interface. The updater does not use the generic insecure
downloader.

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
session. The image verifier now compares that digest with the streamed firmware. The
64 KiB response cap and 16-asset limit are release-maintenance constraints;
larger release records must be shortened or the bounded policy reviewed.

Sixteen metadata/version checks pass, including every truncated prefix of the
fixture, duplicate keys, malformed/extended JSON, nested shadow fields, Unicode,
memory limits, typed sizes, URL/tag binding, and malformed digests. Both
AddressSanitizer and UndefinedBehaviorSanitizer pass. The standard X4 Pro build
passes in 72.13 seconds (RAM 113,060 bytes; flash 5,682,130 bytes). The closed
gate can remove unused updater code from the final image; a later enabled
transport build and runtime measurements must cover its full working set.

## Streamed image verification

The installer hashes incoming bytes without retaining the full image. It requires
an exact metadata length, SHA-256 match, ESP image magic, the running chip ID,
and an explicit matching X4 Pro board tag. It holds the first 14 header bytes
until the magic and chip checks pass. Legacy SD-card updates keep their existing
untagged-image policy.

One checked verifier allocation owns the hash state and board scanner during
installation. No task, SD cache file, or whole-image buffer is added. Runtime
TLS, flash-writer, and screen memory still need measurement together.

The inactive-slot writer receives the exact image size. Transfer, verification,
and write failures abort an owned pending handle. Only a verified stream reaches
`esp_ota_end()`, and only a successful end reaches boot selection. The
[ESP-IDF OTA contract](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/ota.html)
frees the handle after `esp_ota_end()` on either result; the cleanup guard does
not abort that freed handle.

All 31 focused checks pass in 0.16 seconds and pass both sanitizers in 0.56
seconds. They cover split headers, every fixture truncation, excess bytes,
corruption, board/chip mismatch, allocation/hash/network/write failures, and SDK
handle ownership. The pipeline tests compile the whole production updater with
a fake transport and ESP writer, using real CommonCrypto or OpenSSL SHA-256.
Their synthetic payload is not a bootable ESP image; these checks do not prove
physical flashing, power-loss recovery, or certificate validation. The standard
X4 Pro build passes in 74.59 seconds (RAM 113,068 bytes; flash 5,696,246 bytes).

## Restricted HTTPS transport

The experimental transport uses the pinned SDK client with CA and hostname
verification for every connection. The metadata URL is fixed, accepts no
redirects, and treats a verified 404 as no published update. An initial image
URL must name an exact stable personal X4 Pro asset. Image redirects accept only
absolute `https://release-assets.githubusercontent.com/` URLs, at most three
hops. Alternate hosts, credentials in the authority, ports, fragments, control
characters, backslashes, and oversized URLs fail. Credentials are never sent.
GitHub lists that CDN in its [release-download communication requirements](https://docs.github.com/en/actions/reference/runners/github-hosted-runners#communication-requirements-for-github-hosted-runners).

The transport checks the existing trusted-time floor against the system clock,
requesting up to ten seconds of SNTP when unset. That existing clock is a
plausibility floor, not authenticated time. Certificate date checks stay enabled;
the experimental build refuses `NO_ASN_TIME`. Each socket operation has a
15-second timeout and the request has a cooperative ten-minute budget across
redirects. DNS/TCP/SDK work can return after that deadline; this is not a hard
cancellation latency bound. No background network task is added. The existing
Wi-Fi activity owns the network, and the transfer restores modem power-save
state on return.

Metadata has a 64 KiB body cap and image transport a 7 MiB cap; the installer
also enforces its exact smaller metadata/partition size. Non-200 bodies are
stopped before delivery. Unsupported compression, conflicting length/transfer
framing, and duplicated nonempty framing headers fail, as do truncated responses. The SDK already caps response headers at 8 KiB/64
headers and each line at 4 KiB. URLs are capped at 2 KiB. One checked HTTP-client
allocation lasts for the request. The compiled X4 Pro HTTP client is 448 bytes
and the image verifier is 216 bytes. Client strings, headers, TLS state, and
the SDK's 2 KiB body-read stack buffer coexist with metadata or the flash verifier.
The client releases its connection after every hop and on all returns. The two
roots occupy 2,889 bytes in the compiled object's `.rodata`. The SDK fixed/close
body readers each have a compiled 2,112-byte frame; that is not the whole call
chain or a measured task high-water mark. Runtime heap/stack headroom remains
pending.

Two public roots are embedded as immutable text, one selected per origin:

| Origin | Root | SHA-256 of DER certificate |
| --- | --- | --- |
| `api.github.com`, `github.com` | USERTrust ECC Certification Authority | `4ff460d54b9c86dabfbcfc5712e0400d2bed3fbc4d4fbdaa86e06adcd2a9ad7a` |
| `release-assets.githubusercontent.com` | ISRG Root X1 | `96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6` |

The copies came from this Mac's trusted `/etc/ssl/cert.pem`; their publishers
are [Sectigo](https://support.sectigo.com/Com_KnowledgeDetailPage?Id=kA0Uj0000004IrB)
and [Let's Encrypt](https://letsencrypt.org/certificates/). On November 13,
these roots verified live API, GitHub release-redirect, and CDN connections from
the Mac. The observed upstream release was 2.0.0, with a 302 to the allowed CDN
and a 200 HEAD response. Wrong-root and hostname-mismatch checks failed as
expected. This used host TLS, not the firmware's wolfSSL implementation. CA
rotation requires reviewing and replacing these public roots; failures never
fall back to insecure mode or arbitrary roots.

All 43 focused checks pass (0.23 seconds), including 11 transport-policy tests
and the whole-updater missing-release regression. All 43 pass ASAN and UBSAN
(0.76 seconds). Host policy tests use a fake SDK HTTP provider; earlier tests
exercise the production TLS setup with controlled wolfSSL/socket adapters.
Neither proves real firmware certificate validation. Initial host-stub type and
name errors were corrected before the passing runs.
The standard X4 Pro build passes in 70.28 seconds (RAM 113,068 bytes; flash
5,696,254 bytes). The twelve-flag profile, including HTTPS updates, passes in
180.25 seconds (RAM 113,100 bytes; flash 5,803,310 bytes), leaving 750,290 bytes
in the image slot. The first enabled-profile build also installed its pinned
dependencies. These are build footprints, not runtime memory measurements.

## Remaining integration

- Measure internal/PSRAM peaks, largest blocks, and stack with the complete
  enabled update path and active screen.
- Confirm certificate-chain, hostname/date rejection, update success/failure,
  and reading-state preservation on the reader.
- Complete cumulative release review and run the stable publication workflow
  after device validation. Both personal build workflows share the OTA slot,
  X4 Pro board tag, and embedded version checks before they package an image.

### Compiled stable image check

The local preview for test67's source, `2f216f60`, built
`x4pro-gh_release` with `2.0.0-chad.4` in a temporary PlatformIO
configuration. The release packaging step rejected a mismatched source SHA
and produced `crosspoint-2.0.0-chad.4-x4pro.bin` from the matching checkout.
The compiled image passed the board, version, partition size, checksum, and
validation hash checks.

The host updater started from test67's version. It accepted all 5,793,872
bytes of the compiled image and selected the simulated boot slot. Flipping
one byte with the original digest in the release metadata stopped the
update before finalization or boot selection. The harness uses real SHA-256
with simulated transport and flash. Live TLS and installation still need a
reader test.

The host pipeline simulates upgrading from test70 by default. Use a temporary
PlatformIO configuration to build the stable test image from the repository
root.

```sh
mkdir -p build/stable-ota-check
sed 's/^version = .*/version = 2.0.0-chad.4/' platformio.ini > build/stable-ota-check/platformio.preview.ini
pio run --project-conf "$PWD/build/stable-ota-check/platformio.preview.ini" -e x4pro-gh_release
python3 scripts/check_x4pro_firmware.py .pio/build/x4pro-gh_release/firmware.bin 2.0.0-chad.4
cmake --build build/recovery-host-tests --target OtaPipelineTest
CROSSPOINT_STABLE_TEST_IMAGE=.pio/build/x4pro-gh_release/firmware.bin \
  build/recovery-host-tests/personal_update/OtaPipelineTest \
  --gtest_also_run_disabled_tests \
  --gtest_filter=OtaPipeline.DISABLED_CompiledStableImageUpgrade
```

The candidate and stable packaging jobs use the same image checker. Before
printing the asset digest, it checks the updater's size limits, chip and
board identity, embedded version, segment lengths, checksum, and appended
SHA-256 when present. This check is local and takes no reader connection.

For a later starting version, configure a separate host build and pass the
firmware's exact About version through `CROSSPOINT_OTA_HOST_START_VERSION`.
Run the same compiled-image check with that build's executable.

```sh
cmake -S test -B build/test71-stable-ota/host \
  -DCROSSPOINT_OTA_HOST_START_VERSION=2.0.0-chad.3-test71-review-fixes-x4pro
cmake --build build/test71-stable-ota/host --target OtaPipelineTest
```

On November 23, the stable profile built from `c97a5ab6` produced a
5,813,456-byte image. It passed the board, version, slot-size, checksum, and
validation-hash checks. The test70 host updater accepted the complete image
and selected the simulated boot slot. Changing one byte stopped the upgrade
before finalization or boot selection. All 44 ordinary updater checks passed.
Real firmware TLS and installation still need a device test.

Image SHA-256
`a295ee664f088073a948719a7ef9667b826dfcc22c98d1b50f172d10296a86fd`.

### Post-test70 integration check

The same preview build from `c06f2d12` produced a 5,815,776-byte image.
The firmware checker verified its board, version, and slot size. Espressif's
image tool verified its checksum and validation hash. The host test starting
from test70 accepted the exact image
and selected the simulated boot slot. A one-byte change made it abort before
finalization or boot selection. All 2,559 enabled cumulative host checks
passed. The compiled-image test ran separately with the preview artifact.

Image SHA-256
`ac62c87b6e88f1636ffe22036aad458ce2f5e8399d56ecef975bd5ebf8d85a5a`.
This preview is for local checks. Device installation and live TLS remain
open. The next physical check is the [test70 handoff](NEXT_DEVICE_QA.md).

### Test71 stable image check

The stable preview built from the same production source as test71
(`f7257e09`) took 113.93 seconds and produced a 5,817,072-byte X4 Pro image.
Its board, version, slot size, checksum, and validation hash checks passed.
Host updaters starting from test70 and test71 both accepted those exact bytes
and selected the simulated boot slot. Flipping one byte aborted before
finalization or boot selection. All ten ordinary updater tests also passed
for each starting version.

Image SHA-256
`6df1f28dad9345672146f9d568de9ade6205f00dcb6c8f3ec8adceb5c0e11e5a`.
The preview is local. Live TLS and a physical upgrade remain device checks.

## Verification boundary

The host harness executes the production setters, connection setup, and retry
methods with controlled wolfSSL/socket adapters. It checks failed CA/hostname
setup, verification-failure propagation through both attempts, successful
fallback with verification retained, allocation-failure cleanup, TCP failure,
and explicit insecure compatibility. It passes AddressSanitizer and
UndefinedBehaviorSanitizer. These adapters do not perform real certificate
cryptography or establish on-device TLS behavior.

Six patch tests cover clean/repeated application, source drift, patch drift,
wrong revision, escaping target, and a missing patch. Firmware certificate-chain,
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
