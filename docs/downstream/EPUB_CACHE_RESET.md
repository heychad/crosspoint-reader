# EPUB cache reset

Delete Book Cache asks for confirmation, closes the reader, clears generated
files, and reopens the book at its saved position. The normal reader exit runs
statistics and plugin hooks before destruction closes the metadata handles.
Only then does the cache screen remove files. A removal failure offers Retry
or Back to reopen the book. Cancellation before clearing returns to the reader
menu. From a footnote, the prompt asks the reader to return to the book first.

The operation does not delete or rewrite progress, bookmarks, link history,
statistics, tracking markers, or per-book settings. Unknown root entries are
kept. The generated `sections` and `html` directories are cleared, including
their contents. Metadata, CSS caches, known cover variants, numeric thumbnails,
and numeric chapter-image files are removed. Matching is case-insensitive for
FAT filenames.

`readingstats::clearReaderCache` is separate from the older replacement-cache
operation, whose existing callers are unchanged. Its caller must close all
reader/cache owners first. The existing cache screen has a single-book mode;
the global clearing mode is unchanged. No new storage format or task is added.
The scan uses a 128-byte filename buffer and existing file handles. The X4 Pro
cache activity measures 868 bytes, plus its path strings and existing popup
allocations. It briefly coexists with the reader while replacement is queued;
clearing begins after the reader is destroyed.

Host checks cover preserved file contents, cache removal, failure/retry,
cancellation, allocation failure, footnote protection, and close-before-clear
scheduling. Hardware behavior remains unverified. At the next device test,
clear a book's cache, confirm its saved passage, and check its bookmarks.

## Checkpoint validation

| Check | Result |
| --- | --- |
| Full host suite | 1,809 passed in 67.95 seconds |
| Default X4 Pro build | Passed in 107.17 seconds |
| Completion/statistics-reset X4 Pro build | Passed in 127.93 seconds |
| Updated close-before-clear test | Passed after the final test-only refinement |
| Physical reader | Not yet tested |

The earlier build exposed the open-metadata ownership issue. Its result was
superseded by the corrected builds above. The full suite was not repeated for
the final test-only refinement; that affected test was rerun directly. These
measurements describe this batch, not a model or service-tier speed comparison.
