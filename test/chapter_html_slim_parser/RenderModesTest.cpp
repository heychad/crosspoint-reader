#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

namespace {
struct Rendered {
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<std::tuple<std::string, uint32_t, uint32_t>> words() const {
    std::vector<std::tuple<std::string, uint32_t, uint32_t>> result;
    for (const auto& page : pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& block = *static_cast<const PageLine&>(*element).getBlock();
        for (uint16_t i = 0; i < block.wordCount(); ++i) {
          const auto range = block.wordSourceRange(i);
          result.emplace_back(block.wordText(i), range.start, range.end);
        }
      }
    }
    return result;
  }
  size_t count(PageElementTag tag) const {
    size_t result = 0;
    for (const auto& page : pages)
      for (const auto& element : page->elements) result += element->getTag() == tag;
    return result;
  }
  const TextBlock* firstText() const {
    for (const auto& page : pages)
      for (const auto& element : page->elements)
        if (element->getTag() == TAG_PageLine) return static_cast<const PageLine&>(*element).getBlock();
    return nullptr;
  }
};

class RenderModes : public testing::Test {
 protected:
  GfxRenderer renderer;
  std::vector<std::filesystem::path> files;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::string temporary() {
    auto file = std::filesystem::temp_directory_path() /
                (std::string("render-modes-") + testing::UnitTest::GetInstance()->current_test_info()->name() + "-" +
                 std::to_string(files.size()));
    files.push_back(file);
    return file.string();
  }
  Rendered parse(const std::string& body, EpubRenderMode mode, const std::string& rules = "", bool embedded = true) {
    const std::string html = temporary();
    const std::string cssFile = temporary();
    std::ofstream(html) << "<html><body>" << body << "</body></html>";
    std::ofstream(cssFile) << rules;
    CssParser css{temporary()};
    HalFile input;
    EXPECT_TRUE(input.open(cssFile.c_str(), "rb"));
    EXPECT_EQ(css.loadFromStream(input), CssParser::ParseResult::Complete);
    input.close();
    Rendered result;
    const std::string empty;
    ChapterHtmlSlimParser parser{
        epub,
        html,
        renderer,
        0,
        1,
        false,
        4,
        480,
        800,
        false,
        false,
        [&](std::unique_ptr<Page> page, auto, auto, auto) { result.pages.push_back(std::move(page)); },
        embedded,
        empty,
        empty,
        0,
        {},
        nullptr,
        embedded ? &css : nullptr};
    parser.setRenderMode(mode);
    EXPECT_TRUE(parser.parseAndBuildPages());
    return result;
  }
  void TearDown() override {
    Epub::enableGifProbeForTest = false;
    for (const auto& file : files) std::filesystem::remove(file);
  }
};
}  // namespace

TEST_F(RenderModes, ParserClassifiesMalformedInputMissingFileAndCancellation) {
  const std::string html = temporary();
  const std::string empty;
  std::ofstream(html) << "<html><body><p>broken</body></html>";
  auto makeParser = [&](const std::string& path) {
    return std::make_unique<ChapterHtmlSlimParser>(
        epub, path, renderer, 0, 1.0f, false, 4, 480, 800, false, false,
        [](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) {}, true, empty, empty, 0, std::vector<std::string>{},
        nullptr, nullptr);
  };
  auto malformed = makeParser(html);
  ASSERT_TRUE(malformed->beginParse());
  EXPECT_EQ(malformed->parseStep(), ChapterHtmlSlimParser::ParseStatus::Error);
  EXPECT_EQ(malformed->lastParseFailure(), EpubBuildFailure::InvalidInput);
  malformed->abortParse();
  EXPECT_EQ(malformed->lastParseFailure(), EpubBuildFailure::InvalidInput);
  const std::string missingPath = temporary();
  auto missing = makeParser(missingPath);
  EXPECT_FALSE(missing->beginParse());
  EXPECT_EQ(missing->lastParseFailure(), EpubBuildFailure::InputOutput);
  auto cancelled = makeParser(html);
  ASSERT_TRUE(cancelled->beginParse());
  cancelled->requestStop();
  EXPECT_EQ(cancelled->parseStep(), ChapterHtmlSlimParser::ParseStatus::Error);
  EXPECT_EQ(cancelled->lastParseFailure(), EpubBuildFailure::Cancelled);
  cancelled->abortParse();
  EXPECT_EQ(cancelled->lastParseFailure(), EpubBuildFailure::Cancelled);
}

TEST_F(RenderModes, SimpleProfilesPreserveWordsOffsetsLinksAndHiddenContent) {
  const std::string html =
      "<p>alpha <span class='hidden'>invisible words</span> <b>bold</b> <i>italic</i> "
      "<a href='notes.xhtml#one'>link</a> <span hidden='hidden'>hidden</span> omega</p>";
  const auto normal = parse(html, EpubRenderMode::Default, ".hidden{display:none}");
  for (const auto mode : {EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    const auto simplified = parse(html, mode, ".hidden{display:none}");
    EXPECT_EQ(simplified.words(), normal.words());
    ASSERT_NE(simplified.firstText(), nullptr);
    const auto* text = simplified.firstText();
    bool hasBold = false, hasItalic = false, hasLink = false;
    for (uint16_t i = 0; i < text->wordCount(); ++i) {
      hasBold = hasBold || (text->wordStyle(i) & EpdFontFamily::BOLD);
      hasItalic = hasItalic || (text->wordStyle(i) & EpdFontFamily::ITALIC);
    }
    EXPECT_TRUE(hasBold);
    EXPECT_TRUE(hasItalic);
    for (const auto& page : simplified.pages) hasLink = hasLink || !page->links.empty();
    EXPECT_TRUE(hasLink);
  }
}

TEST_F(RenderModes, BalancedKeepsContextualSelectorsAndPublisherSpacing) {
  const std::string body = "<div class='outer'><p id='text'>alpha beta</p></div>";
  const std::string rules = ".outer p {font-weight:bold} #text {margin-left:32px}";
  const auto normal = parse(body, EpubRenderMode::Default, rules);
  const auto balanced = parse(body, EpubRenderMode::Balanced, rules);
  ASSERT_NE(normal.firstText(), nullptr);
  ASSERT_NE(balanced.firstText(), nullptr);
  EXPECT_NE(normal.firstText()->wordStyle(0) & EpdFontFamily::BOLD, 0);
  EXPECT_NE(balanced.firstText()->wordStyle(0) & EpdFontFamily::BOLD, 0);
  EXPECT_GT(balanced.firstText()->getBlockStyle().marginLeft, 0);
  EXPECT_EQ(balanced.firstText()->getBlockStyle().marginLeft, normal.firstText()->getBlockStyle().marginLeft);
  EXPECT_EQ(normal.words(), balanced.words());
}

TEST_F(RenderModes, ContextualHiddenTextKeepsVisibleSourceOffsetsAcrossProfiles) {
  std::string hidden;
  for (int i = 0; i < 2000; ++i) hidden += "invisible ";
  const std::string body =
      "<div class='outer'><p>before</p><p class='hidden'>" + hidden + "</p><p>after target words</p></div>";
  const auto normal = parse(body, EpubRenderMode::Default, ".outer .hidden {display:none}");
  ASSERT_EQ(normal.words().size(), 4u);
  for (const auto mode : {EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    const auto simplified = parse(body, mode, ".outer .hidden {display:none}");
    EXPECT_EQ(simplified.words(), normal.words());
  }
}

TEST_F(RenderModes, DisablingEmbeddedStylesPreservesOffsetsAcrossHiddenText) {
  const std::string body = "<p>before</p><p class='hidden'>secret words</p><p>after target</p>";
  const auto styled = parse(body, EpubRenderMode::Light, ".hidden {display:none}");
  const auto unstyled = parse(body, EpubRenderMode::Light, ".hidden {display:none}", false);
  ASSERT_EQ(styled.words().size(), 3u);
  ASSERT_EQ(unstyled.words().size(), 5u);
  EXPECT_EQ(std::get<0>(styled.words()[1]), "after");
  EXPECT_EQ(std::get<0>(unstyled.words()[3]), "after");
  EXPECT_EQ(std::get<1>(styled.words()[1]), std::get<1>(unstyled.words()[3]));
}

TEST_F(RenderModes, LightRemovesPublisherLayoutAndKeepsBasicFormatting) {
  const std::string body = "<p id='text'>alpha <b>beta</b></p><hr/>";
  const std::string rules =
      "#text {margin-left:32px;padding-left:12px;text-indent:8px;"
      "border:1px solid;background-color:gray;font-size:200%}";
  const auto normal = parse(body, EpubRenderMode::Default, rules);
  const auto light = parse(body, EpubRenderMode::Light, rules);
  ASSERT_NE(light.firstText(), nullptr);
  EXPECT_EQ(light.firstText()->getBlockStyle().marginLeft, 0);
  EXPECT_EQ(light.firstText()->getBlockStyle().paddingLeft, 0);
  EXPECT_EQ(light.firstText()->getBlockStyle().textIndent, 0);
  EXPECT_EQ(light.count(TAG_PageHorizontalRule), 0u);
  EXPECT_EQ(light.count(TAG_PageBorderBox), 0u);
  EXPECT_GT(normal.count(TAG_PageHorizontalRule), 0u);
  EXPECT_EQ(light.words(), normal.words());
}

TEST_F(RenderModes, SimplifiedTablesUseExistingStackedFlow) {
  const std::string body =
      "<table border='1'><tr><td>alpha</td><td>beta</td></tr>"
      "<tr><td>gamma</td><td>delta</td></tr></table><p>end</p>";
  const auto normal = parse(body, EpubRenderMode::Default);
  const auto balanced = parse(body, EpubRenderMode::Balanced);
  const auto light = parse(body, EpubRenderMode::Light);
  EXPECT_GT(normal.count(TAG_PageBorderBox), 0u);
  EXPECT_EQ(balanced.count(TAG_PageBorderBox), 0u);
  EXPECT_EQ(light.count(TAG_PageBorderBox), 0u);
  EXPECT_GT(balanced.count(TAG_PageHorizontalRule), 0u);
  EXPECT_EQ(light.count(TAG_PageHorizontalRule), 0u);
  EXPECT_EQ(balanced.words(), normal.words());
  EXPECT_EQ(light.words(), normal.words());
}

TEST_F(RenderModes, LightUsesIntrinsicImageSizeWhileBalancedKeepsCssSize) {
  Epub::enableGifProbeForTest = true;
  epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 0x90, 1, 0xC8, 0};
  const std::string body = "<img src='image.gif' style='width:20px;height:10px'/>";
  for (const auto mode : {EpubRenderMode::Default, EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    const auto result = parse(body, mode);
    ASSERT_EQ(result.count(TAG_PageImage), 1u);
    for (const auto& page : result.pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageImage) continue;
        const auto& image = static_cast<const PageImage&>(*element).getImageBlock();
        EXPECT_EQ(image.getWidth(), mode == EpubRenderMode::Light ? 400 : 20);
        EXPECT_EQ(image.getHeight(), mode == EpubRenderMode::Light ? 200 : 10);
      }
    }
  }
  EXPECT_EQ(epub->fullExtractions, 0u);
}

TEST_F(RenderModes, NestedTablesAndHiddenCellsKeepVisibleTextAndOffsets) {
  const std::string body =
      "<table><tr><td>outer <table><tr><td>inner</td></tr></table> tail</td>"
      "<td hidden='hidden'>hidden</td><td>last</td></tr></table><p>after</p>";
  const auto normal = parse(body, EpubRenderMode::Default);
  for (const auto mode : {EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    const auto simplified = parse(body, mode);
    EXPECT_EQ(simplified.words(), normal.words());
  }
}

TEST_F(RenderModes, ProfilesKeepMixedDirectionAndCjkSourceRanges) {
  const std::string body = "<p dir='rtl'>مرحبا beta 世界</p><p>alpha 中文 gamma</p>";
  const auto normal = parse(body, EpubRenderMode::Default, "");
  for (const auto mode : {EpubRenderMode::Balanced, EpubRenderMode::Light}) {
    const auto simplified = parse(body, mode, "");
    EXPECT_EQ(simplified.words(), normal.words());
  }
}

TEST_F(RenderModes, OmittedHorizontalRulesPreserveTextBoundaries) {
  for (const std::string body : {"alpha<hr/>beta", "<p>alpha<hr/>beta</p>"}) {
    const auto normal = parse(body, EpubRenderMode::Default);
    const auto light = parse(body, EpubRenderMode::Light);
    EXPECT_EQ(light.words(), normal.words());
    EXPECT_EQ(light.count(TAG_PageHorizontalRule), 0u);
    ASSERT_EQ(light.words().size(), 2u);
    EXPECT_EQ(std::get<0>(light.words()[0]), "alpha");
    EXPECT_EQ(std::get<0>(light.words()[1]), "beta");
  }
}
