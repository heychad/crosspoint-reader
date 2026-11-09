# Settings snapshot experiment

Work in progress. Do not install a firmware image from this branch.

The snapshot helper is not connected to the live settings writer or boot recovery. The final exFAT bitmap check was added after the second independent review round and has not been independently reviewed. The existing settings writer can still lose settings after a failed replacement.

The helper checks source single-cluster eligibility before creating a destination, verifies snapshot bytes, and checks that the two files have different single-cluster allocations. The host tests exercise FAT16, FAT32, exFAT, read/write failures, invalid FAT terminal markers, and a source cluster incorrectly marked free. These checks do not prove whole-filesystem ownership: an unrelated file may share or be assigned an allocation cluster. The destination is written before the pairwise identity check, so an unrelated file can still be affected by a damaged allocation map. Hardware timing, low-memory behavior, power-loss behavior, and startup recovery remain unverified.
