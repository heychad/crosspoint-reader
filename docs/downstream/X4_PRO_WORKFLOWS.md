# X4 Pro build and release workflows

GitHub CI builds and checks the X4 Pro target only. Host tests run serially to avoid shared-fixture collisions, and their translation keys are generated in the clean test job. The clean host job skips the SdFat fault-injection tests when the X4 Pro PlatformIO dependency is absent; run those tests separately with the patched dependency present. Existing low-severity cppcheck style reports remain visible; medium and high findings fail CI.

The manual release-candidate workflow builds an X4 Pro artifact. A published stable release triggers the X4 Pro release workflow, which validates the tag against the configured `X.Y.Z-chad.N` version before building. Each numeric part is limited to nine digits and the whole tag to 26 characters, keeping the asset name within the firmware's parser limit. It attaches one `crosspoint-<tag>-x4pro.bin` asset and does not overwrite an existing asset. Prereleases are not automatically published by this workflow. The release is published before the build and upload run, so a failed build can leave a release visible without its firmware asset.

No workflow was dispatched and no release was published as part of this change. Online firmware installation remains disabled until its transport and payload can be authenticated. Build and workflow checks do not establish hardware behavior.
