# Checked storage lookup

Statistics recovery needs to distinguish an absent file from an unreadable
folder. `HalStorage::lookupPath` returns Present, Missing, or Failed. It opens
and checks one path component at a time under the existing storage lock. It
uses a temporary allocation proportional to the path length and refuses invalid
components, malformed UTF-8, ambiguous trailing spaces or dots, overlong FAT
names, or allocation failure.

The SdFat 2.3.1 patch propagates long-filename lookup errors from copied
folder handles and stops create-only opens after a failed comparison. The build
script verifies dependency hashes before applying it. Updating SdFat requires
reviewing that patch and its pinned hashes.

Checked lookups opt in on their own directory handles. FAT checks long-name
entry ordering and checksums; exFAT checks entry-set structure and checksums.
Regular opens retain their existing behavior. Host tests exercise both paths
against damaged directories. The exFAT scan rejects unsupported active primary
or critical secondary entries, sets with more than 32 secondary entries, and
directories with more than 4,096 examined entries. Checksum-valid benign
secondary extensions are accepted. Latency and watchdog behavior need device
measurement.

This adds a storage primitive; it does not yet connect statistics saves to a
transaction or change recovery behavior. Host tests exercise fragmented FAT
directories, multi-sector exFAT directories, injected read
failures, malformed records, and the actual SdFat wrapper on both formats.
Physical SD-card behavior and resource headroom still require X4 Pro testing.
These checks cannot prove that every form of directory damage is detectable;
this primitive must not yet authorize recovery writes.
