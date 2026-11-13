# Reader-side file checksums

This read-only API verifies bytes already saved on the X4 Pro. It avoids the
library client's verification download after an ordinary create-only `/upload`.
It does not change uploads, publish staged files, decrypt books, or enable the
experimental sync endpoints. Firmware and device validation are tracked below.

## Detect support

`GET /api/status` includes `"fileChecksumApiVersion":1`. This capability is
available in the standard X4 Pro profile independently of experimental flags.
Clients that do not see version 1 keep their existing download-and-hash path.
Do not infer support from the firmware version string.

## Start and poll

Start after the upload has returned successfully, or to verify an existing file:

```http
POST /api/files/checksum
Content-Type: application/x-www-form-urlencoded

path=%2FAuthors%2FWriter%2FBook.epub&size=4622394
```

Only `path` and `size` are accepted. The path must be absolute and canonical,
with at most 1,024 UTF-8 bytes and 32 components. FAT-invalid components,
hidden components, reserved folders, and short-name aliases are refused.
The decimal size must be 0–4,294,967,295, without a sign or leading zeroes.
Visible ordinary files of any extension are supported; hashing does not parse
the file's contents.

An accepted job returns HTTP 202:

```json
{"state":"pending","token":"0123456789abcdef0123456789abcdef","path":"/Authors/Writer/Book.epub","size":4622394,"processed":0}
```

Poll `GET /api/files/checksum?token=<token>`, for example every 100 ms.
HTTP 202 reports `pending` and the current `processed` byte count. HTTP 200
with `state:"complete"` adds a lowercase 64-character `sha256` digest.
The other fields retain the same meanings. Responses use `Cache-Control: no-store`.

There is one job slot. A start while a job or upload is active returns HTTP 409
`{"state":"busy"}` without replacing the active job. A new start can replace a
terminal result. Only the returned token can poll or cancel a job. Tokens are
temporary job identifiers, not account authentication; this uses the existing
local File Transfer server's access model.

## Cancel and errors

`POST /api/files/checksum/cancel` takes only the form field `token`. Cancelling
a pending job returns HTTP 200 with `state:"cancelled"`; a repeated cancel is
safe. A completed result remains complete. Unknown, expired, or malformed token
requests return HTTP 404. Jobs and results expire after 60 seconds without a
valid start, poll, or cancel request. Leaving File Transfer discards the job.

| HTTP | State | Meaning |
| --- | --- | --- |
| 400 | `invalid` | Invalid path, size, or request arguments |
| 404 | `not_found` | File, job, or token not found |
| 409 | `busy` | Another checksum job or upload is active |
| 409 | `wrong_size` | Saved size differs from the requested size |
| 409 | `changed` | Storage changed during hashing or before the result was read |
| 503 | `unavailable` | A stable storage snapshot or storage lock was unavailable |
| 503 | `io_error` | Open, read, seek, directory, or close failure |
| 503 | `no_memory` | The job or its working allocation could not be created |
| 503 | `hash_error` | SHA-256 initialization, update, or finalization failed |

Only a complete response carries a digest. Errors never authorize deleting or
overwriting the target. A client must validate state, path, size, processed count,
and digest before recording a verified receipt. A mismatch or interrupted request
leaves the uploaded file for review.

## Library client integration

The client is `apps/book-library/device_sync.py` in Chad's second-brain
repository. Its integration checkpoint is `6c94b59b4fe3d983ad14580e343af1c9918ab17d`
(checksum integration `4f68baf`). Missing serial numbers, including the firmware's
`Not found` sentinel, block sync instead of sharing an unknown reader identity.
Its existing `Reader.digest(path, size, progress)` interface now selects the
checksum API only when `Reader.status()` advertises integer version 1:

1. Older firmware keeps `/download` verification; unknown capability versions
   and non-integer values also use that path.
2. Start and poll validate HTTP status, state, token, echoed path/size, and
   monotonic processed bytes. Only a complete result with `processed == size`
   and a lowercase 64-character digest reaches the source-hash comparison.
3. Poll every 100 ms with a 180-second deadline. Requests use at most 20-second
   socket timeouts within the remaining deadline. Failure or an interrupted
   progress callback attempts token cancellation with a two-second socket timeout.
   The queue's existing Stop action still finishes the current book.
4. A checksum error stops the queue without a download fallback or verified
   receipt. Files stay intact. A retry verifies the existing destination instead
   of uploading it again. Reader identity is checked before recording success.

Eighteen protocol tests use real local HTTP, including capability fallback,
malformed results, mismatched jobs, HTTP errors, redirects, cancellation,
deadlines, missing reader identity, and receipt failure/retry. Four additional
queue regressions cover reruns, a lost upload response, hash failures, and
same-size conflicts. That checkpoint passed all 295
second-brain host tests and its authoring validator; the queue checks below
bring the current total to 303. These tests use a host
server, not reader hardware. Cumulative review and measured end-to-end transfer
timing remain pending; code publication is recorded below. The installed clipfix2 firmware uses
the existing download fallback until a reviewed candidate is installed.

## Selected-book queue and create-only uploads

The supported D1 flow is Drive Desktop's local library, the existing web-library
selection queue, File Transfer, and create-only `/upload` followed by byte
verification. It uses the existing upload and folder routes; experimental staged
publication is a separate unfinished experiment and is not enabled by this flow.
Drive cloud completion and continuous background sync are not implied.

Create-only upload opens now use `HalStorage::openChecked()` with `O_EXCL` and
without `O_TRUNC`. This closes the gap between the earlier existence check and
open: another HAL writer's new destination causes failure instead of truncation
under the existing storage lock.
Explicit overwrite keeps the existing staged `.part` path and replacement
behavior. The change adds no buffer, task, format, endpoint, or reservation file.
It reuses the checked HAL wrapper, storage lock, and mutation fence.

Nine focused firmware checks pass in 2.52 seconds. The new test executes the
production open policy and HAL methods against patched SdFat on FAT/exFAT disk
images. New files, case-insensitive late collisions, directory collisions,
explicit overwrite staging, and reading-state preservation survive remount.
AddressSanitizer passes. The combined UBSan attempt reports the existing vendor
unaligned accesses and null-pointer arithmetic already tracked in
[completion evidence](FINISHED_BOOKS.md).
It is not a clean UBSan pass; no category was suppressed. A separate leak-check
option was unsupported on this Mac and was removed for the passing ASan run.

Eight real-HTTP integration tests run the production client queue, multipart
upload, folder creation, and checksum or download verification together. They
cover nested Unicode paths, reruns, lost acknowledgements, late conflicts,
truncated files, old firmware, reader identity changes, and Stop after the
current book. Reader state and source organization stay unchanged in the host
fixture. The web app restores the saved address once, without replacing edits
made before or during polling; its UI regression initially exposed and then
verified the first-response ordering correction.

The full client repository passes 303 tests and validates 68 concepts with no
errors or warnings. The final default X4 Pro build passes in 71.77 seconds
(RAM 113,068 bytes; flash 5,696,342 bytes); the twelve-flag profile passes in
76.96 seconds (RAM 113,100 bytes; flash 5,803,398 bytes). These are local adapter
and filesystem-image checks, not simultaneous-task or device timing evidence.
Cumulative security/storage review and backed-up device validation remain.

The app and its tests are published on the private second-brain branch
[`codex/x4pro-library-sync-20261113`](https://github.com/heychad/second-brain/tree/codex/x4pro-library-sync-20261113),
checkpoint `9f4db2f4c0957486809c482318243b0f22e6560d`. Its code and tests match
canonical local checkpoint `6c94b59`; its README records the different test
scope. The code-only branch passes 73 tests and validates five unchanged base
concepts. It excludes the canonical checkout's 244 unrelated unpublished
commits; no knowledge-history push or main-branch merge occurred.

## Execution and resource limits

The File Transfer main loop advances at most 16 KiB per call, using a 4 KiB
buffer and the existing mbedTLS SHA-256 API. Each slice opens, checks, seeks,
reads, and closes under the existing recursive storage lock. No handle stays
open between slices. The server feeds the watchdog and yields between slices;
the existing Back/Home cancellation callback can cancel pending work.

One checked working allocation holds the 4 KiB buffer and SHA-256 context.
It lives only while pending and is released on completion, error, cancellation,
expiry, or server teardown. A checked job allocation and its bounded path string
retain the result until expiry or replacement. There is no worker task, second
file copy, persistent journal, or filesystem write. JSON responses and request
strings add temporary allocations in the existing web transport.

The compiled S3 layout is 144 bytes for the job and 4,212 bytes for its working
allocation, before allocator overhead and the stored path. The slice function's
compiled frame is 112 bytes. These are ELF measurements; they do not establish
runtime heap or stack headroom. The existing experimental `StageDigest` remains
unchanged: its synchronous stage verification has a different lifetime. This API
uses the same mbedTLS implementation with an incremental owner.

The HAL mutation snapshot refuses an existing writer and invalidates the job
after any observed storage mutation, including an unrelated write. This is
conservative: retry after storage becomes idle. Size and I/O checks also reject
truncation. A result describes the observed snapshot, not a path reservation,
card identity guarantee, or protection from writes that bypass the HAL. The
client still checks device identity and matches the expected digest.

Per-slice bytes are bounded; SD read latency, runtime heap headroom, watchdog
behavior, throughput, and battery use still need physical X4 Pro measurements.
No speedup or installed-device result is claimed from host checks.

## Validation status

The 63 focused checks pass, including the new job and HTTP handlers, existing
stage hash/publication checks, protected paths, and the storage mutation fence.
They cover exact SHA-256 vectors, empty and multi-slice files, saved offsets,
no retained file handle, cancellation, expiry and timer wrap, wrong sizes,
truncation, same-size edits, completed-result invalidation, path/argument
restrictions, allocation failure, hash failures, and I/O errors. Both the new
unit executable and HTTP integration pass AddressSanitizer and
UndefinedBehaviorSanitizer. Storage and HTTP remain host adapters; these checks
do not simulate simultaneous tasks or physical SD latency.

The standard ESP32-S3 `x4pro` build passes in 72.74 seconds, using 113,140 bytes
of static RAM and 5,673,138 bytes of flash. No experimental sync flag was enabled.
The preceding build failed on Arduino's `HEX` macro; renamed local constants
and a host macro-compatibility check resolve that compile error. The first
unit run also found a wrong-size result overwritten by a generic I/O error;
the final checks preserve the specific result.

Logs: `/tmp/x4-checksum-final-checks.log`,
`/tmp/x4-checksum-unit-sanitizers.log`,
`/tmp/x4-checksum-http-sanitizers.log`, and
`/tmp/x4-checksum-firmware-final.log`.

Cumulative firmware review and a backed-up device handoff remain required
before installed-device use. This firmware has not been installed or device-tested.
Use `/zDev/` for firmware and development artifacts on the reader's card.

The near-end completion prompt integration was queued behind this reader-side
checksum request from the library workstream. Its previous statistics/move
checkpoint remains unchanged.
