# Personal firmware updates

Online update checks and installation are disabled in this build. The current HTTP client skips certificate verification, so an encrypted connection does not establish that a release or firmware file came from the fork. The update screen directs the reader to use an SD-card firmware file instead; it does not start Wi-Fi. Existing plugins and their network behavior are unchanged.

The prepared channel targets `heychad/crosspoint-reader`. It accepts only X4 Pro firmware from a strictly newer `X.Y.Z-chad.N` release, with an asset such as `crosspoint-2.0.0-chad.4-x4pro.bin`. The number must be greater even when the installed firmware is a test build. The fork has no published releases as of this build. This work has not published a release or installed firmware.

Before enabling online updates, verify server certificates and hostnames across every redirect, authenticate the firmware payload, reject malformed or incomplete release documents, and test the full update and failure paths on the X4 Pro. Certificate-chain validation, firmware authenticity, SD backup and rollback, and release publishing are separate work. Do not enable the guard based on a successful HTTPS connection alone.
