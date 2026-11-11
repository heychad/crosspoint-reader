# CrossInk attribution

The status-bar layout algorithms and reading-statistics models, binary formats,

calendar utilities, Quick Lock/chord state models, atomic save tests, and the
bounded Library metadata sorter derive from the CrossInk project,

including `src/util/ReaderStatusBarConfig.h` and
`src/activities/reader/BookReadingStats.cpp` and `lib/LibraryIndex/LibrarySort.*` at commit
`9914146eeae7b46b300f475a16c32426fc02ec1f`. Adaptations in this fork are marked
at the affected source. Additional ports must retain their applicable notices.

The daily-duration codec and storage strategy follow
`src/activities/reader/DailyReadingStats.*` at tagged RC
`b8feaec4d10ecb484ed80e326f01f2a3ea949331`. The fork preserves the wire format
and uses its checked HAL storage contracts.

CrossInk's repository license at that revision is:

MIT License

Copyright (c) 2025 Dave Allie
Copyright (c) 2025 CrossPoint Reader organization

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
