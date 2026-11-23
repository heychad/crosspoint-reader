# X4 Pro build and release workflows

GitHub CI builds and checks the X4 Pro target only. Host tests run serially to avoid shared-fixture collisions, and their translation keys are generated in the clean test job. The clean host job skips the SdFat fault-injection tests when the X4 Pro PlatformIO dependency is absent; run those tests separately with the patched dependency present. Existing low-severity cppcheck style reports remain visible; medium and high findings fail CI.

The manual release-candidate workflow builds `x4pro-chad-test`, the same
profile used for local device tests. Enter a numbered version such as
`2.0.0-chad.3-test66` or `2.0.0-chad.3-test66-update-check`. The release number
must match `platformio.ini`. The workflow adds the `-x4pro` suffix and checks
the image header, OTA size, board tag, version, and SDK pin. The Actions
artifact includes `firmware-x4pro.bin`, `SHA256SUMS`, and `SOURCE.txt` with the
commit, SDK revision, profile, and embedded version. Hardware handoff still
requires review and a device test.

A published stable release triggers a separate workflow. It validates the
tag against the configured `X.Y.Z-chad.N` version before building. Each numeric
part has at most nine digits and the entire tag at most 26 characters. The
workflow attaches one `crosspoint-<tag>-x4pro.bin` asset and refuses to overwrite
an existing one. It skips prereleases. A failed build can leave a published
release with no firmware asset.

The standard profile keeps online updates disabled. The test profile enables
them for validation. See [the personal update channel](PERSONAL_UPDATES.md).
