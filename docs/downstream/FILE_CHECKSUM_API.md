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

The current client is `apps/book-library/device_sync.py` in Chad's second-brain
repository. Its `Reader.digest(path, size, progress)` can keep its interface:

1. Cache the capability returned by its existing `Reader.status()` call.
2. On older firmware, retain the current bounded `/download` verification.
3. On version 1, start a checksum job, validate the token and echoed path/size,
   then poll. Feed monotonic `processed` values to the existing progress callback.
4. Accept only `complete`, the expected path/size, `processed == size`, and a
   64-character hexadecimal digest. Keep the existing comparison with the source
   SHA-256 and the existing reader-identity check before writing a receipt.
5. Cancel the token on a user stop or polling timeout. Use a bounded polling
   deadline. Do not silently replace a checksum error with a verified receipt.

The client has not been changed by this firmware stage. Its existing fallback
continues to work with installed firmware. Client integration and measured
end-to-end timing remain pending.

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

Cumulative firmware review, client integration, and a backed-up device handoff
remain required before use. This firmware has not been installed or device-tested.
Use `/zDev/` for firmware and development artifacts on the reader's card.

The near-end completion prompt integration was queued behind this reader-side
checksum request from the library workstream. Its previous statistics/move
checkpoint remains unchanged.
