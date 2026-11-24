#include <gtest/gtest.h>

#include "network/EpubTimingWeb.h"

namespace {
bool renderBusy = false;
bool renderLockHeld = false;
}  // namespace

RenderLock::RenderLock(Mode mode) {
  EXPECT_EQ(mode, Mode::Try);
  renderLockHeld = isLocked = !renderBusy;
}

RenderLock::~RenderLock() {
  if (isLocked) renderLockHeld = false;
}

void WebServer::send(int statusCode, const char* type, const char* data) {
  EXPECT_FALSE(renderLockHeld);
  status = statusCode;
  contentType = type;
  body = data;
}

class EpubTimingWebTest : public ::testing::Test {
 protected:
  WebServer server;
  JsonDocument reply;

  void SetUp() override {
    epubtiming::recorder = {};
    renderBusy = renderLockHeld = false;
  }

  void request(int expectedStatus) {
    epubtiming::sendMeasurements(server);
    ASSERT_EQ(server.status, expectedStatus);
    ASSERT_EQ(server.contentType, "application/json");
    ASSERT_EQ(server.headers["Cache-Control"], "no-store");
    ASSERT_FALSE(deserializeJson(reply, server.body));
  }
};

TEST_F(EpubTimingWebTest, EmptySamplesHaveNoDurationAndReadingDoesNotStartRecording) {
  request(200);
  EXPECT_FALSE(reply["recording"].as<bool>());
  EXPECT_EQ(reply.size(), 4u);
  for (auto group : {"text", "illustrated", "indexing"}) {
    EXPECT_EQ(reply[group].size(), 1u);
    EXPECT_TRUE(reply[group]["count"].is<unsigned>());
    EXPECT_EQ(reply[group]["count"].as<unsigned>(), 0u);
  }
  EXPECT_FALSE(epubtiming::recorder.isRecording());
}

TEST_F(EpubTimingWebTest, ReportsStoppedRecordingAndPreservesItsValuesAcrossRequests) {
  using epubtiming::PageKind;
  auto& recorder = epubtiming::recorder;
  recorder.toggleRecording();
  recorder.record(PageKind::Text, 0);
  recorder.record(PageKind::Text, 40);
  recorder.record(PageKind::Illustrated, UINT32_MAX);
  recorder.record(PageKind::Indexing, 800);
  request(200);
  EXPECT_TRUE(reply["recording"].as<bool>());
  recorder.toggleRecording();
  for (int i = 0; i < 2; ++i) {
    request(200);
    EXPECT_FALSE(reply["recording"].as<bool>());
    EXPECT_EQ(reply["text"]["count"].as<unsigned>(), 2u);
    EXPECT_EQ(reply["text"]["medianMs"].as<unsigned>(), 20u);
    EXPECT_EQ(reply["text"]["maxMs"].as<unsigned>(), 40u);
    EXPECT_EQ(reply["illustrated"]["medianMs"].as<uint32_t>(), UINT32_MAX);
    EXPECT_EQ(reply["indexing"]["maxMs"].as<unsigned>(), 800u);
  }
}

TEST_F(EpubTimingWebTest, BusyRenderCanBeRetried) {
  renderBusy = true;
  request(503);
  EXPECT_EQ(server.headers["Retry-After"], "1");
  EXPECT_STREQ(reply["error"].as<const char*>(), "render_busy");
  renderBusy = false;
  request(200);
  EXPECT_FALSE(epubtiming::recorder.isRecording());
}

TEST_F(EpubTimingWebTest, LatestSuccessfulClipSurvivesStopAndResetsOnStart) {
  auto& recorder = epubtiming::recorder;
  recorder.recordClipSave({2, 1});
  request(200);
  EXPECT_FALSE(reply["lastClipSave"].is<JsonObject>());
  recorder.toggleRecording();
  recorder.recordClipSave({2, 1});
  recorder.recordClipSave({0, 18});
  recorder.toggleRecording();
  recorder.recordClipSave({99, 99});
  request(200);
  EXPECT_EQ(reply["lastClipSave"].size(), 2u);
  EXPECT_EQ(reply["lastClipSave"]["storeMs"].as<unsigned>(), 0u);
  EXPECT_EQ(reply["lastClipSave"]["exportMs"].as<unsigned>(), 18u);
  recorder.toggleRecording();
  request(200);
  EXPECT_FALSE(reply["lastClipSave"].is<JsonObject>());
}

TEST_F(EpubTimingWebTest, MaximumMeasurementsFitInOneResponse) {
  auto& recorder = epubtiming::recorder;
  recorder.toggleRecording();
  for (auto kind : {epubtiming::PageKind::Text, epubtiming::PageKind::Illustrated, epubtiming::PageKind::Indexing}) {
    for (size_t i = 0; i < recorder.WINDOW_SIZE; ++i) recorder.record(kind, UINT32_MAX);
  }
  recorder.recordClipSave({UINT32_MAX, UINT32_MAX});
  recorder.toggleRecording();
  request(200);
  EXPECT_EQ(reply.size(), 5u);
  EXPECT_EQ(reply["indexing"]["count"].as<unsigned>(), recorder.WINDOW_SIZE);
  EXPECT_EQ(reply["lastClipSave"]["exportMs"].as<uint32_t>(), UINT32_MAX);
}
