#include <PersonalRelease.h>
#include <PersonalUpdateVersion.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {
using personalupdate::Release;
using personalupdate::ReleaseResult;
std::string json() {
  return R"({"tag_name":"2.0.0-chad.4","draft":false,"prerelease":false,"assets":[{"name":"crosspoint-2.0.0-chad.4-x4pro.bin","state":"uploaded","size":256,"digest":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","browser_download_url":"https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4pro.bin"}],"body":"Release notes"})";
}
std::string change(const std::string& old, const std::string& value) {
  auto result = json();
  const auto index = result.find(old);
  EXPECT_NE(index, std::string::npos);
  if (index != std::string::npos) result.replace(index, old.size(), value);
  return result;
}
ReleaseResult parse(std::string input, Release& output, size_t maximum = 1024) {
  return personalupdate::parseRelease(input.data(), input.size(), maximum, output);
}
void invalid(std::string input) {
  Release output;
  output.size = 123;
  EXPECT_NE(parse(std::move(input), output), ReleaseResult::Valid);
  EXPECT_EQ(output.size, 0u);
  EXPECT_EQ(output.version[0], '\0');
  EXPECT_EQ(output.url[0], '\0');
  for (auto byte : output.sha256) EXPECT_EQ(byte, 0u);
}
}  // namespace

TEST(PersonalRelease, ExactPersonalAssetOwnsValidatedFields) {
  Release output;
  ASSERT_EQ(parse(json(), output), ReleaseResult::Valid);
  EXPECT_STREQ(output.version, "2.0.0-chad.4");
  EXPECT_EQ(output.size, 256u);
  EXPECT_STREQ(
      output.url,
      "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4pro.bin");
  EXPECT_EQ(output.sha256.front(), 0x01);
  EXPECT_EQ(output.sha256.back(), 0xef);
}
TEST(PersonalRelease, EveryTruncatedPrefixFailsClosed) {
  const auto document = json();
  for (size_t i = 0; i < document.size(); ++i) {
    SCOPED_TRACE(i);
    invalid(document.substr(0, i));
  }
}
TEST(PersonalRelease, RejectsDuplicateCriticalKeysIncludingEscapedNames) {
  for (const auto& field : {"tag_name", "draft", "prerelease", "assets"}) {
    const auto key = std::string("\"") + field + "\":";
    invalid(change(key, key + "null," + key));
  }
  invalid(change("\"tag_name\":", "\"\\u0074ag_name\":null,\"tag_name\":"));
  for (const auto& field : {"name", "state", "size", "digest", "browser_download_url"}) {
    const auto key = std::string("\"") + field + "\":";
    invalid(change(key, key + "null," + key));
  }
}
TEST(PersonalRelease, RejectsMalformedAndExtendedJsonSyntax) {
  for (const auto& replacement : {"draft:", "'draft':"}) invalid(change("\"draft\":", replacement));
  invalid(change("\"uploaded\"", "'uploaded'"));
  invalid(json() + "garbage");
  invalid(json() + "{}");
  invalid(change("\"Release notes\"", "\"line\nfeed\""));
  invalid(change("\"body\":", "body:"));
  invalid(change("\"Release notes\"", "NaN"));
  invalid(change("\"Release notes\"", "01"));
  invalid(change("\"Release notes\"", "+1"));
  invalid(change("\"Release notes\"", "1."));
  invalid(change("\"Release notes\"", "1e+"));
  invalid(change("\"Release notes\"", "\"\\q\""));
  invalid(change("\"Release notes\"", "\"\\u00xz\""));
  invalid(change("\"Release notes\"", "\"" + std::string("\xc0\xaf", 2) + "\""));
  invalid(change("\"Release notes\"", "[1,]"));
}
TEST(PersonalRelease, RequiresStableTypedReleaseAndPersonalVersion) {
  invalid(change("\"draft\":false", "\"draft\":true"));
  invalid(change("\"draft\":false", "\"draft\":0"));
  invalid(change("\"prerelease\":false", "\"prerelease\":true"));
  invalid(change("\"tag_name\":\"2.0.0-chad.4\"", "\"tag_name\":\"2.0.1\""));
  invalid(change("\"tag_name\":\"2.0.0-chad.4\"", "\"tag_name\":\"2.0.0-chad.4-test1\""));
  invalid(change("\"tag_name\":\"2.0.0-chad.4\"", "\"tag_name\":\"2.0.0-chad.1234567890\""));
}
TEST(PersonalRelease, BindsUrlAndFilenameToExactTagAndFork) {
  for (const auto& value : {"http://github.com/", "https://evil.example/", "https://github.com@evil.example/"})
    invalid(change("https://github.com/", value));
  invalid(change("/heychad/", "/other/"));
  invalid(change("/download/2.0.0-chad.4/", "/download/2.0.0-chad.5/"));
  invalid(change(".bin\"}]", ".bin?extra\"}]"));
  Release output;
  EXPECT_EQ(
      parse(change("\"name\":\"crosspoint-2.0.0-chad.4-x4pro.bin\"", "\"name\":\"crosspoint-2.0.0-chad.4-x4.bin\""),
            output),
      ReleaseResult::MissingAsset);
}
TEST(PersonalRelease, RejectsWrongSizeTypeAndPartitionOverflow) {
  for (const auto& value :
       {"0", "23", "1025", "-1", "4294967296", "true", "\"256\"", "256.0", "2.56e2", "+256", "0256"})
    invalid(change("\"size\":256", std::string("\"size\":") + value));
  Release output;
  EXPECT_EQ(parse(json(), output, 255), ReleaseResult::Invalid);
  EXPECT_EQ(parse(change("\"size\":256", "\"size\":24"), output, 24), ReleaseResult::Valid);
}
TEST(PersonalRelease, RequiresExactLowercaseSha256AndUploadedState) {
  invalid(change("sha256:", "sha512:"));
  invalid(change("0123456789abcdef", "0123456789abcdeF"));
  invalid(change("0123456789abcdef", "0123456789abcdeg"));
  invalid(change("\"state\":\"uploaded\"", "\"state\":\"new\""));
  invalid(change("\"digest\":\"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"",
                 "\"digest\":null"));
}
TEST(PersonalRelease, RejectsAmbiguousAssetsAndAcceptsEmptyListAsNoUpdate) {
  auto document = json();
  const auto first = document.find("[{"), last = document.find("}],");
  const auto asset = document.substr(first + 1, last - first);
  document.replace(first, last - first + 2, "[" + asset + "," + asset + "]");
  invalid(document);
  document = json();
  document.replace(first, last - first + 2, "[]");
  Release output;
  EXPECT_EQ(parse(document, output), ReleaseResult::MissingAsset);
  document = json();
  document.replace(first, last - first + 2, "[null]");
  invalid(document);
}
TEST(PersonalRelease, CapsBodyAndParserMemoryAndAllowsIgnoredUnicode) {
  Release output;
  EXPECT_EQ(parse(change("Release notes", "A π 😀 \\\"tag_name\\\""), output), ReleaseResult::Valid);
  auto document = json();
  const auto additional = personalupdate::MAX_RELEASE_BYTES - document.size();
  const auto offset = document.find("Release notes");
  document.insert(offset, additional, 'a');
  ASSERT_EQ(document.size(), personalupdate::MAX_RELEASE_BYTES);
  EXPECT_EQ(parse(document, output), ReleaseResult::Valid);
  document.push_back(' ');
  invalid(document);
  document = json();
  const auto first = document.find("[{"), last = document.find("}],");
  std::string assets = "[";
  for (int i = 0; i < 1000; ++i) {
    if (i) assets += ',';
    assets += R"({"name":"unused"})";
  }
  assets += ']';
  document.replace(first, last - first + 2, assets);
  EXPECT_EQ(parse(document, output), ReleaseResult::NoMemory);
}

TEST(PersonalRelease, AllowsValidUnknownJsonValuesAndNestedShadowFields) {
  for (const auto* value :
       {"null", "true", "false", "0", "-0", "-12.25e+10", "0.1E-3", "[]", "{}",
        R"([1,true,null,{"tag_name":"ignored","assets":[{"size":1}]}])",
        R"({"draft":true,"digest":"ignored","nested":{"size":false}})", R"("\\\"\/\b\f\n\r\t\u0041")"}) {
    SCOPED_TRACE(value);
    Release output;
    EXPECT_EQ(parse(change("\"Release notes\"", value), output), ReleaseResult::Valid);
  }
}

TEST(PersonalRelease, EnforcesNestingLimitForIgnoredFields) {
  Release output;
  auto value = std::string(15, '[') + "null" + std::string(15, ']');
  EXPECT_EQ(parse(change("\"Release notes\"", value), output), ReleaseResult::Valid);
  value = '[' + value + ']';
  invalid(change("\"Release notes\"", value));
}
