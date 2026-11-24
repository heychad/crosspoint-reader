#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

#include "activities/RenderLock.h"
#include "activities/reader/EpubRenderTiming.h"

namespace epubtiming {
inline void sendMeasurements(WebServer& server) {
  server.sendHeader("Cache-Control", "no-store");
  bool recording;
  Summary text, illustrated, indexing;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      server.sendHeader("Retry-After", "1");
      server.send(503, "application/json", R"({"error":"render_busy"})");
      return;
    }
    recording = recorder.isRecording();
    text = recorder.summarize(PageKind::Text);
    illustrated = recorder.summarize(PageKind::Illustrated);
    indexing = recorder.summarize(PageKind::Indexing);
  }

  JsonDocument response;
  response["recording"] = recording;
  const auto addSummary = [&](const char* name, const Summary& summary) {
    auto group = response[name].to<JsonObject>();
    group["count"] = summary.count;
    if (summary.count) {
      group["medianMs"] = summary.medianMs;
      group["maxMs"] = summary.maxMs;
    }
  };
  addSummary("text", text);
  addSummary("illustrated", illustrated);
  addSummary("indexing", indexing);
  char body[256];
  if (response.overflowed() || serializeJson(response, body, sizeof(body)) >= sizeof(body)) {
    server.send(503, "application/json", R"({"error":"measurements_unavailable"})");
    return;
  }
  server.send(200, "application/json", body);
}
}  // namespace epubtiming
