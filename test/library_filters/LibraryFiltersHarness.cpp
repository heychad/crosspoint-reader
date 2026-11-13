#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

bool trackAllocations = false;
size_t allocationCount = 0;
void* operator new(size_t bytes) {
  if (trackAllocations) ++allocationCount;
  if (void* value = std::malloc(bytes == 0 ? 1 : bytes)) return value;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }

#include "LibraryFormat.h"
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define FREEINK_CAP_TOUCH 0
int yields = 0;
void delay(int) { ++yields; }
bool failAllocation = false;
template <class T>
auto makeUniqueNoThrow(size_t count) {
  return failAllocation ? std::unique_ptr<T>{} : std::unique_ptr<T>(new std::remove_extent_t<T>[count] {});
}
struct CrossPointSettings {
  static constexpr int RECENT_BOOKS_GRID = 1;
};
struct Settings {
  int recentBooksView = 0;
  bool libraryUseMetadata = true;
  bool libraryTitleCoverGrid = false;
  bool libraryShowEpub = true, libraryShowXtc = true, libraryShowTxt = true, libraryShowMarkdown = true;
  bool libraryHideFinishedBooks = false;
  bool libraryListExpanded = true;
  bool libraryShowSeries = true, libraryShowGenre = true;
} SETTINGS;
namespace FsHelpers {
bool checkFileExtension(std::string_view path, std::string_view ext) {
  if (path.size() < ext.size()) return false;
  path.remove_prefix(path.size() - ext.size());
  for (size_t i = 0; i < ext.size(); ++i)
    if (std::tolower(path[i]) != ext[i]) return false;
  return true;
}
bool hasEpubExtension(std::string_view path) { return checkFileExtension(path, ".epub"); }
bool hasXtcExtension(std::string_view path) {
  return checkFileExtension(path, ".xtc") || checkFileExtension(path, ".xtch");
}
bool hasTxtExtension(std::string_view path) { return checkFileExtension(path, ".txt"); }
bool hasMarkdownExtension(std::string_view path) { return checkFileExtension(path, ".md"); }
}  // namespace FsHelpers
struct Book {
  std::string path, title, author;
  std::optional<bool> completed = false;
  std::string series, genre;
};
std::vector<Book> books = {{"/alpha.epub", "alpha", "first", false},       {"/beta.xtc", "beta", "second", true},
                           {"/gamma.txt", "gamma", "third", std::nullopt}, {"/delta.md", "delta", "fourth", false},
                           {"/epsilon.md", "epsilon", "fifth", true},      {"/zeta.EPUB", "zeta", "sixth", false}};
namespace readingstats {
std::optional<bool> bookCompletion(const std::string& path) {
  for (const auto& book : books)
    if (book.path == path) return book.completed;
  return false;
}
}  // namespace readingstats
struct RecentBooksStore {
  static constexpr int MAX_RECENT_BOOKS = 18;
  std::vector<Book> entries = {{"/missing.epub", "missing", "", false}, books[1], books[3]};
  const auto& getBooks() { return entries; }
} RECENT_BOOKS;
struct HalFile {
  int fileSize() { return 0; }
};
struct StorageModel {
  bool openFileForRead(const char*, const char*, HalFile&) { return false; }
} Storage;
namespace library {
bool dirty = false;
bool isLibraryIndexDirty() { return dirty; }
const char* libraryIndexPath() { return "/index"; }
struct LibraryIndexFile {
  struct PathLookup {
    bool ready = false;
    explicit operator bool() const { return ready; }
  };
};
enum class SortOrder {
  RecentAsc,
  RecentDesc,
  TitleAsc,
  TitleDesc,
  AuthorAsc,
  AuthorDesc,
  SeriesAsc,
  SeriesDesc,
  GenreAsc,
  GenreDesc,
  AuthorFirstAsc,
  AuthorFirstDesc,
  DateAddedAsc,
  DateAddedDesc
};
struct BookIdentity {
  uint64_t pathHash;
  uint32_t fileSize;
};
std::string fold(std::string text) {
  for (char& c : text) c = std::tolower(static_cast<unsigned char>(c));
  return text;
}
void foldInto(std::string_view text, std::string& output) {
  output.clear();
  for (char value : text) output.push_back(std::tolower(static_cast<unsigned char>(value)));
}
bool matchesQuery(std::string_view text, std::string_view needle) { return text.find(needle) != text.npos; }
}  // namespace library
bool isDescending(library::SortOrder order) {
  return order == library::SortOrder::RecentDesc || order == library::SortOrder::TitleDesc ||
         order == library::SortOrder::AuthorDesc || order == library::SortOrder::SeriesDesc ||
         order == library::SortOrder::GenreDesc || order == library::SortOrder::AuthorFirstDesc ||
         order == library::SortOrder::DateAddedDesc;
}
bool isSeriesSort(library::SortOrder order) {
  return order == library::SortOrder::SeriesAsc || order == library::SortOrder::SeriesDesc;
}
bool isMetadataSort(library::SortOrder order) {
  return isSeriesSort(order) || order == library::SortOrder::GenreAsc || order == library::SortOrder::GenreDesc;
}
enum class StrId {
  STR_LIBRARY_UNKNOWN_SERIES,
  STR_LIBRARY_UNKNOWN_GENRE,
  STR_LIBRARY_UNKNOWN_AUTHOR,
  STR_LIBRARY_UNKNOWN_TITLE,
  STR_LIBRARY_REBUILDING
};
struct TestI18n {
  const char* get(StrId id) {
    if (id == StrId::STR_LIBRARY_UNKNOWN_TITLE) return "Unknown title";
    if (id == StrId::STR_LIBRARY_UNKNOWN_AUTHOR) return "Unknown author";
    return id == StrId::STR_LIBRARY_UNKNOWN_SERIES ? "No series" : "Uncategorized";
  }
} I18N;
#define tr(id) I18N.get(StrId::id)
constexpr int RECENT_TAB = 0;
constexpr int TITLE_TAB = 1;
constexpr int OPENED_VIEW = 6;
constexpr size_t RECENT_SEARCH_TEXT_BYTES = 512;
struct Gui {
  void drawPopup(int, const char*) {}
} GUI;
struct Index {
  bool broken = false, opened = true, lookupFails = false, failReopen = false;
  int pathReads = 0, nameReads = 0, lookupBuilds = 0, openCalls = 0, recentLookups = 0, failLookupAt = -1;
  struct Header {
    int metadataEnabled = 1;
  } savedHeader;
  const Header& header() const { return savedHeader; }
  bool ranksDegraded() const { return false; }
  bool dedupDegraded() const { return false; }
  int bookCount() const { return opened ? books.size() : 0; }
  bool isOpen() const { return opened; }
  void close() { opened = false; }
  bool open(const char*) {
    ++openCalls;
    opened = !broken && !failReopen;
    return opened;
  }
  uint16_t ordinalForRow(library::SortOrder order, uint16_t row) {
    if (row >= books.size()) return 0xFFFF;
    std::array<uint16_t, 16> positions{};
    assert(books.size() <= positions.size());
    for (uint16_t i = 0; i < books.size(); ++i) positions[i] = i;
    if (isMetadataSort(order)) {
      std::sort(positions.begin(), positions.begin() + books.size(), [&](uint16_t a, uint16_t b) {
        const auto& left = isSeriesSort(order) ? books[a].series : books[a].genre;
        const auto& right = isSeriesSort(order) ? books[b].series : books[b].genre;
        if (left.empty() != right.empty()) return !left.empty();
        return left < right || (left == right && a < b);
      });
    }
    return positions[isDescending(order) ? books.size() - 1 - row : row];
  }
  bool readRecord(uint16_t ordinal, library::ClixRecord& out) {
    if (broken) return false;
    out = {};
    out.nameOff = ordinal;
    out.foldLen = books[ordinal].title.size();
    std::memcpy(out.fold, books[ordinal].title.data(), out.foldLen);
    return true;
  }
  library::LibraryIndexFile::PathLookup preparePathLookup() {
    ++lookupBuilds;
    return {!broken && !lookupFails};
  }
  bool readName(const library::ClixRecord& row, std::string& out) {
    ++nameReads;
    out = books[row.nameOff].path;
    return !broken;
  }
  bool readPath(const library::ClixRecord& row, std::string& out, const library::LibraryIndexFile::PathLookup&) {
    ++pathReads;
    out = books[row.nameOff].path;
    return !broken;
  }
  bool readPath(const library::ClixRecord& row, std::string& out) {
    out = books[row.nameOff].path;
    return !broken;
  }
  bool readTitle(const library::ClixRecord& row, std::string& out) {
    out = books[row.nameOff].title;
    return !broken;
  }
  bool readAuthor(const library::ClixRecord& row, std::string& out) {
    out = books[row.nameOff].author;
    return !broken && !out.empty();
  }
  bool readSeries(const library::ClixRecord& row, std::string& out) {
    out = books[row.nameOff].series;
    return !broken;
  }
  bool readGenre(const library::ClixRecord& row, std::string& out) {
    out = books[row.nameOff].genre;
    return !broken;
  }
  bool ioFailed() const { return broken; }
  bool recentRowsFor(const library::BookIdentity* identities, size_t count, uint16_t* out) {
    assert(count <= 8);
    if (++recentLookups == failLookupAt) return false;
    for (size_t i = 0; i < count; ++i) {
      out[i] = 0xffff;
      for (size_t n = 0; n < books.size(); ++n) {
        const auto& path = books[n].path;
        if (identities[i].pathHash == library::clixPathHash(path.data(), path.size())) out[i] = n;
      }
    }
    return !broken;
  }
};

namespace fui {
constexpr int InputTouch = 1, InputLongPress = 2;
struct Text {
  int maxLines = 0;
};
struct ListItem {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  const char* sectionHeading = nullptr;
  int icon = 0;
  int16_t actionValue = 0;
};
struct ListProps {
  uint16_t count = 0, itemsWindowFirst = 0, itemsWindowCount = 0;
  int action = 0, inputMask = 0, rowGap = 0;
  bool headerUnderline = true;
  Text labelText;
  uint16_t topIndex = 0;
  ListItem* items = nullptr;
};
}  // namespace fui
struct UiScreen {
  struct Theme {
    fui::Text bodyText;
    int16_t listRowGap = 1;
  } values;
  fui::ListProps rendered;
  const Theme& theme() const { return values; }
  void list(const fui::ListProps& props) { rendered = props; }
};
struct UITheme {
  static int getFileIcon(const std::string&) { return 1; }
};
struct LibraryCoverGrid {
  static constexpr int MAX_BOOKS = 6;
  std::array<uint64_t, MAX_BOOKS> identities{};
  int first = -1, count = 0, selected = -1, page = MAX_BOOKS;
  bool layout(UiScreen&) { return true; }
  int pageSize() const { return page; }
  int nextCover() const { return 0; }
  bool matches(int slot, uint64_t identity) const { return identities[slot] == identity; }
  int loads = 0;
  bool loadCover(int, const std::string&) {
    ++loads;
    return true;
  }
  void setBook(int slot, uint64_t identity, const std::string&) { identities[slot] = identity; }
  void clearBook(int slot) { identities[slot] = 0; }
  void draw(UiScreen&, int start, int visible, int selection, int) {
    first = start;
    count = visible;
    selected = selection;
  }
};
int listIconFor(int value, int) { return value; }
bool isAuthorSort(library::SortOrder order) {
  return order == library::SortOrder::AuthorAsc || order == library::SortOrder::AuthorDesc ||
         order == library::SortOrder::AuthorFirstAsc || order == library::SortOrder::AuthorFirstDesc;
}
bool isRecentSort(const library::SortOrder order) {
  return order == library::SortOrder::RecentAsc || order == library::SortOrder::RecentDesc;
}

bool isFlatSort(library::SortOrder order) {
  return order == library::SortOrder::RecentAsc || order == library::SortOrder::RecentDesc ||
         order == library::SortOrder::DateAddedAsc || order == library::SortOrder::DateAddedDesc;
}
struct MappedInputManager {
  enum class SwipeDir { None, Left, Right, Up, Down };
  SwipeDir swipe = SwipeDir::None;
  SwipeDir wasSwipe() const { return swipe; }
};
struct UiAppHost {
  static void routeTouch(MappedInputManager&, bool) {}
};
struct RenderLock {
  template <typename T>
  explicit RenderLock(T&) {}
};
struct LibraryListActivity {
  Index index;
  MappedInputManager mappedInput;
  bool handleGridSwipe();
  static constexpr int ACTION_ROW = 1;
  struct Nav {
    int visibleRows = 12;
    int top = 0, drawnCount = 0;
    std::atomic<int> selected{0};
    std::atomic<bool> followOnBuild{true};
    bool followPending = false;
    int inputPageRows() const { return 12; }
    void onListRendered(uint16_t, int, bool) {}
  } nav;
  Nav& activeNav() { return nav; }
  std::vector<fui::ListItem> winItems;
  std::vector<std::string> winTitles, winAuthors, winHeaders;
  std::string rowFileScratch, seriesScratch, genreScratch, metadataKeyScratch, previousGroupScratch;
  std::unique_ptr<uint16_t[]> groupStarts;
  int listCount() const { return groupsCollapsed ? groupCount : bookRowCount(); }
  void syncTabListViewport(UiScreen&, fui::ListProps&) {}
  void buildRows(UiScreen&);
  bool buildGrid(UiScreen&);
  bool rowPathFor(int, std::string&);
  void loadNextGridCover();
  bool gridRequested() const;
  bool gridEnabled() const;
  int navigationPageSize();
  std::unique_ptr<LibraryCoverGrid> recentGrid;
  int updates = 0;
  void requestUpdate() { ++updates; }
  bool rowMetadataFor(int, std::string&, std::string&);
  bool buildGroupStarts();
  void formatMetadataHeading(const std::string&, std::string&) const;
  bool rowTextFor(int entry, std::string& title, std::string& author, std::string* path = nullptr);
  bool recentTextFor(size_t, std::string&, std::string&) const;
  bool recentTextViews(size_t, std::string_view&, std::string_view&) const;
  bool historyView() const;
  void refreshViewIndex();
  int renderer = 0, rebuilds = 0;
  bool rebuildIndex() {
    ++rebuilds;
    index.broken = false;
    index.failReopen = false;
    library::dirty = false;
    return true;
  }
  uint32_t titleInitialFor(int entry) {
    std::string title, author;
    rowTextFor(entry, title, author);
    return title.empty() ? 0 : title[0];
  }
  static void formatInitialHeading(uint32_t initial, std::string& out) { out = static_cast<char>(initial); }
  void formatAuthorHeading(const std::string& author, std::string& out) const;

  int activeTabIndex = 0;
  library::SortOrder sortOrder = library::SortOrder::RecentDesc;
  std::string query, headerSearchTitle;
  bool filterFailed = false, groupsCollapsed = false, degraded = false;
  uint16_t groupCount = 0, filteredCount = 0, groupCapacity = 0;
  std::unique_ptr<uint16_t[]> filtered;
  uint16_t pinnedAscRows[RecentBooksStore::MAX_RECENT_BOOKS]{}, overlapRows[RecentBooksStore::MAX_RECENT_BOOKS]{};
  uint8_t pinnedStoreIndices[RecentBooksStore::MAX_RECENT_BOOKS]{}, pinnedTotal = 0, overlapCount = 0;
  void closeRouting() {}
  int pinnedCount() const;
  void resolvePinned();
  void refreshOverlap();
  bool filtersActive() const;
  bool matchesFilters(const std::string&) const;
  bool matchesFormat(const std::string&) const;
  void applyFilter();
  int bookRowCount() const;
  int rowFor(int) const;
  std::vector<std::string> visible() {
    std::vector<std::string> result;
    for (int row = 0; row < bookRowCount(); ++row) {
      if (row < pinnedCount())
        result.push_back(RECENT_BOOKS.entries[pinnedStoreIndices[row]].path);
      else
        result.push_back(books[index.ordinalForRow(sortOrder, rowFor(row))].path);
    }
    assert(std::set<std::string>(result.begin(), result.end()).size() == result.size());
    return result;
  }
};

// INSERT_METHODS
int main() {
  LibraryListActivity view;
  std::string heading;
  view.sortOrder = library::SortOrder::AuthorAsc;
  view.formatAuthorHeading("Victor Hugo", heading);
  assert(heading == "Hugo, Victor");
  for (const auto order : {library::SortOrder::AuthorFirstAsc, library::SortOrder::AuthorFirstDesc}) {
    view.sortOrder = order;
    view.formatAuthorHeading("Victor Hugo", heading);
    assert(heading == "Victor Hugo");
    view.formatAuthorHeading("", heading);
    assert(heading == "Unknown author");
  }
  for (const auto order : {library::SortOrder::DateAddedAsc, library::SortOrder::DateAddedDesc}) {
    view.activeTabIndex = 7;
    view.sortOrder = order;
    view.applyFilter();
    view.resolvePinned();
    assert(view.pinnedCount() == 0 && view.bookRowCount() == static_cast<int>(books.size()));
    auto dated = view.visible();
    assert(std::find(dated.begin(), dated.end(), "/missing.epub") == dated.end());
    assert(isFlatSort(order) && !isRecentSort(order));
    SETTINGS.libraryShowXtc = false;
    view.applyFilter();
    assert(view.bookRowCount() == static_cast<int>(books.size()) - 1);
    SETTINGS.libraryShowXtc = true;
  }
  view.activeTabIndex = 0;
  view.sortOrder = library::SortOrder::RecentDesc;
  view.applyFilter();
  view.resolvePinned();
  assert(view.bookRowCount() == 7 && view.visible()[0] == "/missing.epub");
  SETTINGS.libraryShowXtc = false;
  view.applyFilter();
  view.resolvePinned();
  assert(view.bookRowCount() == 6 && view.pinnedCount() == 2);
  assert(view.pinnedStoreIndices[1] == 2);
  auto rows = view.visible();
  assert(std::find(rows.begin(), rows.end(), "/beta.xtc") == rows.end());
  assert(view.index.pathReads == 0 && view.index.nameReads > 0);
  SETTINGS.libraryHideFinishedBooks = true;
  view.applyFilter();
  view.resolvePinned();
  assert(view.bookRowCount() == 5 && view.index.lookupBuilds > 0 && view.index.pathReads > 0);
  rows = view.visible();
  assert(std::find(rows.begin(), rows.end(), "/gamma.txt") != rows.end());
  SETTINGS.libraryShowEpub = false;
  view.applyFilter();
  view.resolvePinned();
  assert(view.bookRowCount() == 2 && view.pinnedCount() == 1 && view.pinnedStoreIndices[0] == 2);
  assert((view.visible() == std::vector<std::string>{"/delta.md", "/gamma.txt"}));
  view.query = "third";
  view.applyFilter();
  view.refreshOverlap();
  assert(view.pinnedCount() == 0 && view.bookRowCount() == 1 && view.visible()[0] == "/gamma.txt");
  view.sortOrder = library::SortOrder::RecentAsc;
  view.applyFilter();
  view.refreshOverlap();
  view.query.clear();
  view.applyFilter();
  view.refreshOverlap();
  assert((view.visible() == std::vector<std::string>{"/gamma.txt", "/delta.md"}));
  view.sortOrder = library::SortOrder::RecentDesc;
  view.applyFilter();
  view.refreshOverlap();
  assert((view.visible() == std::vector<std::string>{"/delta.md", "/gamma.txt"}));
  SETTINGS.libraryShowTxt = false;
  SETTINGS.libraryShowMarkdown = false;
  view.applyFilter();
  view.resolvePinned();
  assert(view.bookRowCount() == 0 && !view.filterFailed);
  SETTINGS = {};
  SETTINGS.libraryHideFinishedBooks = true;
  failAllocation = true;
  view.applyFilter();
  view.resolvePinned();
  assert(view.filterFailed && view.bookRowCount() == 0);
  failAllocation = false;
  view.index.broken = true;
  view.applyFilter();
  view.resolvePinned();
  assert(view.filterFailed && view.bookRowCount() == 0);
  view.index.broken = false;
  view.index.lookupFails = true;
  view.applyFilter();
  view.resolvePinned();
  assert(view.filterFailed && view.bookRowCount() == 0);
  view.index.lookupFails = false;
  view.applyFilter();
  view.resolvePinned();
  assert(!view.filterFailed && view.bookRowCount() == 5 && yields > 0);
  SETTINGS = {};
  books[0].author.clear();
  view.query = "third";
  view.applyFilter();
  assert(!view.filterFailed && view.bookRowCount() == 1 && view.visible()[0] == "/gamma.txt");
  books[0].author = "first";
  view.query.clear();
  view.activeTabIndex = 2;
  view.sortOrder = library::SortOrder::AuthorAsc;
  view.applyFilter();
  view.resolvePinned();
  UiScreen expanded;
  view.buildRows(expanded);
  assert(expanded.rendered.rowGap == 6);
  assert(view.winItems.front().sectionHeading != nullptr);
  assert(view.winItems.front().subtitle == nullptr);
  SETTINGS.libraryListExpanded = false;
  UiScreen compact;
  view.buildRows(compact);
  assert(compact.rendered.rowGap == 1);
  for (const auto& item : view.winItems) {
    assert(item.sectionHeading == nullptr);
    assert(item.subtitle != nullptr);
  }
  view.groupsCollapsed = true;
  view.groupCount = 2;
  view.groupStarts = std::make_unique<uint16_t[]>(2);
  view.groupStarts[0] = 0;
  view.groupStarts[1] = 3;
  view.buildRows(compact);
  assert(std::string(view.winItems[0].label) == "first");
  assert(std::string(view.winItems[1].label) == "fourth");

  view.groupsCollapsed = false;
  books[0].series = "Galactic Adventures";
  books[1].genre = "Mystery";
  view.query = "galactic";
  view.applyFilter();
  assert(!view.filterFailed && view.bookRowCount() == 1 && view.visible()[0] == "/alpha.epub");
  view.query = "mystery";
  view.applyFilter();
  assert(!view.filterFailed && view.bookRowCount() == 1 && view.visible()[0] == "/beta.xtc");

  view.query.clear();
  view.activeTabIndex = 3;
  view.sortOrder = library::SortOrder::SeriesAsc;
  view.applyFilter();
  SETTINGS.libraryListExpanded = true;
  assert(view.buildGroupStarts());
  assert(view.groupCount == 2);
  view.groupsCollapsed = true;
  view.buildRows(expanded);
  assert(std::string(view.winItems[0].label) == "Galactic Adventures");
  assert(std::string(view.winItems[1].label) == "No series");
  view.groupsCollapsed = false;
  view.buildRows(expanded);
  assert(std::string(view.winItems[0].sectionHeading) == "Galactic Adventures");
  assert(std::string(view.winItems[0].subtitle).find("Galactic Adventures") != std::string::npos);
  SETTINGS.libraryShowSeries = false;
  view.buildRows(expanded);
  assert(std::string(view.winItems[0].subtitle).find("Galactic Adventures") == std::string::npos);

  view.activeTabIndex = 0;
  view.sortOrder = library::SortOrder::RecentDesc;
  view.resolvePinned();
  view.buildRows(expanded);
  assert(std::string(view.winItems[1].subtitle).find("Mystery") != std::string::npos);

  books[0].genre = "Science Fiction / Adventure";
  books[3].genre = "Science Fiction / Adventure";
  view.activeTabIndex = 4;
  view.sortOrder = library::SortOrder::GenreAsc;
  view.applyFilter();
  for (int warm = 0; warm < 3; ++warm) view.buildRows(expanded);
  allocationCount = 0;
  trackAllocations = true;
  for (int paint = 0; paint < 100; ++paint) view.buildRows(expanded);
  trackAllocations = false;
  assert(allocationCount == 0);
  assert(std::string(view.winItems[1].sectionHeading) == "Science Fiction / Adventure");

  // Selection of each shelf is independent; collapsed groups and OOM use rows.
  view.activeTabIndex = TITLE_TAB;
  SETTINGS.libraryTitleCoverGrid = true;
  assert(!view.gridRequested());
  view.recentGrid = std::make_unique<LibraryCoverGrid>();
  assert(view.gridEnabled() && view.navigationPageSize() == 6);
  view.recentGrid->page = 0;
  assert(!view.gridEnabled() && view.navigationPageSize() == 12);
  view.recentGrid->page = 6;
  view.groupsCollapsed = true;
  assert(!view.gridRequested());
  view.groupsCollapsed = false;
  for (int tab : {2, 3, 4}) {
    view.activeTabIndex = tab;
    assert(!view.gridRequested());
  }
  view.activeTabIndex = RECENT_TAB;
  assert(!view.gridRequested());
  SETTINGS.recentBooksView = CrossPointSettings::RECENT_BOOKS_GRID;
  assert(view.gridRequested());
  SETTINGS.libraryTitleCoverGrid = false;
  view.activeTabIndex = TITLE_TAB;
  assert(!view.gridRequested());
  SETTINGS.libraryTitleCoverGrid = true;

  // Grid pages retain the filtered/pinned row identities used by normal actions.
  view.query.clear();
  view.activeTabIndex = 0;
  view.sortOrder = library::SortOrder::RecentDesc;
  view.applyFilter();
  view.resolvePinned();
  view.recentGrid = std::make_unique<LibraryCoverGrid>();
  const auto expectedGrid = view.visible();
  view.nav.selected = 1;
  view.nav.followOnBuild = true;
  assert(view.buildGrid(expanded));
  assert(view.recentGrid->first == 0 && view.recentGrid->count == 6);
  for (int slot = 0; slot < 6; ++slot) {
    std::string path;
    assert(view.rowPathFor(slot, path) && path == expectedGrid[slot]);
    assert(view.recentGrid->identities[slot] == library::clixPathHash(path.data(), path.size()));
  }
  view.nav.selected = expectedGrid.size();
  view.nav.followOnBuild = true;
  assert(view.buildGrid(expanded));
  assert(view.recentGrid->first == 6 && view.recentGrid->count == 1);
  assert(view.recentGrid->identities[1] == 0);
  // A touch swipe followed by physical Confirm must target a visible book.
  view.nav.top = 0;
  view.nav.selected = 1;
  view.mappedInput.swipe = MappedInputManager::SwipeDir::Left;
  assert(view.handleGridSwipe());
  assert(view.nav.top == 6 && view.nav.selected == 7);
  assert(view.buildGrid(expanded));
  assert(view.recentGrid->first == 6 && view.recentGrid->selected == 6);
  std::string selectedPath;
  assert(view.rowPathFor(view.nav.selected - 1, selectedPath) && selectedPath == expectedGrid[6]);
  view.mappedInput.swipe = MappedInputManager::SwipeDir::Right;
  assert(view.handleGridSwipe());
  assert(view.nav.top == 0 && view.nav.selected == 1);
  view.nav.selected = 0;
  view.mappedInput.swipe = MappedInputManager::SwipeDir::Left;
  assert(view.handleGridSwipe() && view.nav.selected == 0);
  // Switching to the title shelf and filtering must map cover identities to
  // the same paths used by normal book-opening actions, in either sort order.
  view.activeTabIndex = TITLE_TAB;
  for (auto order : {library::SortOrder::TitleAsc, library::SortOrder::TitleDesc}) {
    view.sortOrder = order;
    view.applyFilter();
    const auto expected = view.visible();
    view.nav.selected = 1;
    view.nav.followOnBuild = true;
    assert(view.gridRequested() && view.buildGrid(expanded));
    for (int slot = 0; slot < view.recentGrid->count; ++slot) {
      std::string rowPath;
      assert(view.rowPathFor(slot, rowPath) && rowPath == expected[slot]);
      assert(view.recentGrid->identities[slot] == library::clixPathHash(rowPath.data(), rowPath.size()));
    }
  }
  view.activeTabIndex = RECENT_TAB;
  view.sortOrder = library::SortOrder::RecentDesc;
  view.query = "alpha";
  view.applyFilter();
  view.resolvePinned();
  view.nav.followOnBuild = true;
  view.nav.selected = 77;
  assert(view.buildGrid(expanded));
  assert(view.nav.selected == 1 && view.recentGrid->first == 0 && view.recentGrid->count == 1);
  std::string path;
  assert(view.rowPathFor(0, path) && path == "/alpha.epub");
  assert(!view.rowPathFor(-1, path) && !view.rowPathFor(1, path));
  view.index.failReopen = true;
  view.loadNextGridCover();
  assert(view.filterFailed && view.pinnedTotal == 0 && view.updates > 0);

  // A history-only shelf uses resident rows even without a valid derived index.
  SETTINGS = Settings{};
  RECENT_BOOKS.entries = {{"/incoming/not-indexed.epub", "New arrival", "Ada", false},
                          books[1],
                          books[3],
                          {"/Books/Filename fallback.epub", "", "Named Author", false}};
  LibraryListActivity history;
  history.activeTabIndex = OPENED_VIEW;
  history.sortOrder = library::SortOrder::RecentDesc;
  history.index.opened = false;
  history.index.failReopen = true;
  library::dirty = true;
  history.refreshViewIndex();
  assert(!history.index.isOpen() && history.rebuilds == 0 && !history.degraded);
  const std::vector<std::string> newest{"/incoming/not-indexed.epub", "/beta.xtc", "/delta.md",
                                        "/Books/Filename fallback.epub"};
  failAllocation = true;
  history.applyFilter();
  assert(!history.filterFailed && !history.filtered && history.visible() == newest);
  failAllocation = false;
  history.sortOrder = library::SortOrder::RecentAsc;
  history.applyFilter();
  auto oldest = newest;
  std::reverse(oldest.begin(), oldest.end());
  assert(history.visible() == oldest && history.overlapCount == 0);
  for (int row = 0; row < history.bookRowCount(); ++row) {
    std::string actual;
    assert(history.rowPathFor(row, actual) && actual == oldest[row]);
  }
  history.query = "new arrival ada";
  history.applyFilter();
  assert(history.bookRowCount() == 1 && history.visible()[0] == newest[0]);
  history.query = "filename fallback named author";
  history.applyFilter();
  assert(history.bookRowCount() == 1 && history.visible()[0] == newest[3]);
  SETTINGS.libraryUseMetadata = false;
  history.query = "new arrival";
  history.applyFilter();
  assert(history.bookRowCount() == 0 && !history.filterFailed);
  history.query = "not-indexed";
  history.applyFilter();
  assert(history.bookRowCount() == 1 && history.visible()[0] == newest[0]);
  std::string recentTitle, recentAuthor;
  assert(history.rowTextFor(0, recentTitle, recentAuthor));
  assert(recentTitle == "not-indexed" && recentAuthor.empty());
  SETTINGS.libraryUseMetadata = true;
  history.query.clear();
  SETTINGS.libraryShowXtc = false;
  history.applyFilter();
  assert(history.bookRowCount() == 3);
  SETTINGS.libraryShowXtc = true;
  SETTINGS.libraryHideFinishedBooks = true;
  history.applyFilter();
  assert(history.bookRowCount() == 3);
  SETTINGS.libraryHideFinishedBooks = false;
  // Bound query scratch without truncating or rewriting the resident metadata.
  const auto originalTitle = RECENT_BOOKS.entries[0].title;
  const auto originalAuthor = RECENT_BOOKS.entries[0].author;
  RECENT_BOOKS.entries[0].title = "search" + std::string(505, 'a');
  RECENT_BOOKS.entries[0].author.clear();
  history.query = "search";
  history.applyFilter();
  assert(!history.filterFailed && history.bookRowCount() == 1);
  RECENT_BOOKS.entries[0].title += "b";
  history.applyFilter();
  assert(history.filterFailed && history.bookRowCount() == 0 && RECENT_BOOKS.entries[0].title.size() == 512);
  RECENT_BOOKS.entries[0].title = originalTitle;
  RECENT_BOOKS.entries[0].author = originalAuthor;
  history.query.clear();
  history.applyFilter();
  assert(!history.filterFailed && history.bookRowCount() == 4);
  // Optional annotation failures do not drop reading-history rows.
  history.index.opened = true;
  history.index.broken = true;
  history.applyFilter();
  assert(!history.index.isOpen() && !history.filterFailed && history.bookRowCount() == 4);
  assert(history.visible() == oldest);
  history.recentGrid = std::make_unique<LibraryCoverGrid>();
  SETTINGS.recentBooksView = CrossPointSettings::RECENT_BOOKS_GRID;
  history.nav.selected = 1;
  history.nav.followOnBuild = true;
  assert(history.gridRequested() && history.buildGrid(expanded));
  history.loadNextGridCover();
  assert(history.recentGrid->loads == 1 && !history.filterFailed && history.bookRowCount() == 4);
  history.index.opened = true;
  history.index.broken = false;
  history.index.failReopen = true;
  history.loadNextGridCover();
  assert(!history.filterFailed && history.pinnedTotal == 4 && !history.index.isOpen());
  // Removal changes the same resident mapping used by Open and book actions.
  RECENT_BOOKS.entries.erase(RECENT_BOOKS.entries.begin() + 1);
  history.applyFilter();
  assert(history.bookRowCount() == 3 && history.visible()[0] == newest[3]);
  while (RECENT_BOOKS.entries.size() < 18) {
    const auto number = std::to_string(RECENT_BOOKS.entries.size());
    RECENT_BOOKS.entries.push_back({"/older" + number + ".epub", "Older " + number, "", false});
  }
  history.index.opened = true;
  history.index.failReopen = false;
  history.index.recentLookups = 0;
  history.index.failLookupAt = 2;
  history.applyFilter();
  assert(history.bookRowCount() == 18 && !history.filterFailed && !history.index.isOpen());
  assert(history.index.recentLookups == 2);
  history.index.opened = true;
  history.index.recentLookups = 0;
  history.index.failLookupAt = -1;
  history.applyFilter();
  assert(history.bookRowCount() == 18 && history.index.recentLookups == 3);
  library::dirty = false;
  history.index.close();
  history.index.failReopen = true;
  history.activeTabIndex = TITLE_TAB;
  history.refreshViewIndex();
  assert(history.rebuilds == 1 && history.index.isOpen());
}
