#pragma once

#include "activities/reader/StatsAccessGate.h"

int bookMoveReferenceTests() {
  using namespace bookmovetest;
  for (bool exfat : {false, true}) {
    for (bool legacyCover : {false, true}) {
      Disk disk;
      FsVolume volume;
      seed(volume, exfat, disk);
      // Seed real application stores, including an unrelated recent book.
      JsonDocument empty;
      empty["books"].to<JsonArray>();
      assert(RECENT_BOOKS.fromJson(empty.as<JsonVariantConst>()));
      write("/Other.epub", {7, 6, 5});
      RECENT_BOOKS.addBook("/Other.epub", "Other", "Writer", "/external-cover.bmp");
      const auto cache = legacyCover ? bm::Root::LegacyCache : bm::Root::Cache;
      const auto oldCover = bm::rootPath(from, bm::BookKind::Reflowable, cache) + "/thumb.bmp";
      const auto newCover = bm::rootPath(to, bm::BookKind::Reflowable, cache) + "/thumb.bmp";
      RECENT_BOOKS.addBook(from, "Book", "Author", oldCover);
      APP_STATE.openEpubPath = from;
      assert(APP_STATE.saveToFile());
      assert(readingstats::normalStatsAllowed());
      const auto otherBefore = read("/Other.epub");

      // A live lease blocks moves before journal creation or file mutation.
      StatsSessionLease lease;
      assert(lease.acquire());
      assert(bm::move(from, to) == bm::Result::Busy);
      assert(!bm::pending() && read(from) == contents(0));
      assert(lease.release());
      assert(bm::move(from, to) == bm::Result::Moved);
      assert(!bm::pending() && readingstats::normalStatsAllowed());
      assert(APP_STATE.openEpubPath == to);
      assert(RECENT_BOOKS.getCount() == 2);
      assert(RECENT_BOOKS.getBooks()[0].path == to);
      assert(RECENT_BOOKS.getBooks()[0].coverBmpPath == newCover);
      assert(RECENT_BOOKS.getBooks()[1].path == "/Other.epub");
      assert(RECENT_BOOKS.getBooks()[1].coverBmpPath == "/external-cover.bmp");
      assert(read("/Other.epub") == otherBefore);

      // Verify the persisted paths, not only their RAM projections.
      APP_STATE.openEpubPath.clear();
      assert(APP_STATE.loadFromFile() && RECENT_BOOKS.loadFromFile());
      assert(APP_STATE.openEpubPath == to && RECENT_BOOKS.getBooks()[0].path == to);
      assert(RECENT_BOOKS.getBooks()[0].coverBmpPath == newCover);
      assert(Storage.lookupPath(from.c_str()) == HalStorage::PathStatus::Missing);
      assert(read(to) == contents(0));
      assert(bm::recover() == bm::Result::NothingPending);

      // The pending gate also preserves current/recent paths before recovery.
      bm::Work back;
      back.from = to;
      back.to = from;
      assert(bm::prepare(back) == bm::Result::Moved);
      assert(!readingstats::normalStatsAllowed());
      assert(!lease.acquire());
      assert(!RECENT_BOOKS.pruneMissing());
      assert(RECENT_BOOKS.getBooks()[0].path == to);
      assert(bm::recover() == bm::Result::Moved);
      assert(APP_STATE.openEpubPath == from && RECENT_BOOKS.getBooks()[0].path == from);
      assert(readingstats::normalStatsAllowed());
    }
    std::printf(
        "%s: production move owner, reader lease, RAM/disk references, both cover roots, recovery gate passed\n",
        exfat ? "exFAT" : "FAT");
  }
  return 0;
}
