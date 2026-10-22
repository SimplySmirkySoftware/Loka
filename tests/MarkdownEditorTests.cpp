#include "MarkdownEditorTests.hpp"
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(LOKA_RETRO68)
#include "../example/MarkdownEditor/src/MyAppConfig.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/TextEditorAccess.hpp"
#include <cstring>

using namespace loka::core;
using namespace loka::app;
using namespace markdowneditor;

class MarkdownEditorTestAccess
{
public:
  static ObservableList<String> &lines(MainNode &n)
  {
    return n.lines_;
  }
  static String error(MainNode &n)
  {
    return n.error_.get();
  }
  static PreviewSeat &seat(MainNode &n, unsigned i)
  {
    return n.preview_[i];
  }
};

namespace
{
  scene::Node *find(scene::Node *node, const char *id)
  {
    if (node->testId() == id)
      return node;
    scene::INestable *nest = node->asNestable();
    if (nest)
      for (scene::Node *child = nest->childrenHead(); child; child = child->nextInComposition)
      {
        scene::Node *found = find(child, id);
        if (found)
          return found;
      }
    return 0;
  }
  unsigned countBlocks(scene::Node *node)
  {
    unsigned count = node->testId() == "MarkdownEditor.Block" || node->testId() == "MarkdownEditor.Rule" ? 1 : 0;
    scene::INestable *nest = node->asNestable();
    if (nest)
      for (scene::Node *child = nest->childrenHead(); child; child = child->nextInComposition)
        count += countBlocks(child);
    return count;
  }
  String plain(const AttributedString &text)
  {
    String result;
    for (std::size_t i = 0; i < text.segmentCount(); ++i)
      result += text.segment(i).text;
    return result;
  }
  void changed(void *context)
  {
    ++*static_cast<unsigned *>(context);
  }
  struct Harness
  {
    NullPlatformContext context;
    MarkdownEditorAppConfig config;
    NullScenePlatformController platform;
    scene::Scene scene;
    Harness()
        : config(&this->context),
          scene(scene::Boundary<MainNode>(MainProps()
                                              .platformContext(&this->context)
                                              .documents(&this->config.documents())
                                              .document(this->config.documents().idAt(0)))
                    .clone())
    {
      this->scene.mount(&this->platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
    }
    ~Harness()
    {
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
    }
    MainNode &main()
    {
      return *static_cast<MainNode *>(loka::dsl::testing::SceneTestAccess::rootNode(this->scene));
    }
    ObservableList<String> &lines()
    {
      return MarkdownEditorTestAccess::lines(this->main());
    }
    void flush()
    {
      if (this->scene.hasPendingInvalidation())
        this->scene.flushInvalidation();
    }
    void replaceAll(const char *text)
    {
      scene::Node *node = find(&this->main(), "MarkdownEditor.Editor");
      LOKA_VERIFY(node);
      const unsigned short last = this->lines().size() - 1;
      const unsigned length = this->lines().at(last).value.buffer().length();
      TextEditorDocument &document =
          loka::app::testing::TextEditorAccess::document(*static_cast<TextEditorNode *>(node));
      LOKA_VERIFY(document.applyReplace(LineCursor(this->lines().at(0).id, 0),
                                        LineCursor(this->lines().at(last).id, length),
                                        text,
                                        std::strlen(text))
                  == EDITOR_OK);
      this->flush();
    }
    void replace(unsigned row, unsigned length, const char *text)
    {
      scene::Node *node = find(&this->main(), "MarkdownEditor.Editor");
      LOKA_VERIFY(node);
      TextEditorDocument &document =
          loka::app::testing::TextEditorAccess::document(*static_cast<TextEditorNode *>(node));
      const ItemId id = this->lines().at(row).id;
      LOKA_VERIFY(document.applyReplace(LineCursor(id, 0), LineCursor(id, length), text, std::strlen(text))
                  == EDITOR_OK);
      this->flush();
    }
  };
  const char sample[] = "# README\n\nA **bold** and *italic* and `code` paragraph.\nJoined line.\n\n"
                        "## Details\n\nSecond **strong**, *emphasis*, `literal`.\n\n"
                        "- one\n* two\n- three\n1. first\n2. second\n> quoted\n"
                        "```cpp\nint x = 1;\n\nreturn x;\n```\n---";
} // namespace

void testMarkdownEditorPreviewAndLiveEdit()
{
  Harness h;
  h.replace(0, 0, sample);
  const Markdown blocks(h.lines());
  const BlockKind kinds[] = {
      HEADING1, PARAGRAPH, HEADING2, PARAGRAPH, BULLET, BULLET, BULLET, NUMBERED, NUMBERED, QUOTE, CODE, RULE};
  const char *texts[] = {"README",
                         "A bold and italic and code paragraph. Joined line.",
                         "Details",
                         "Second strong, emphasis, literal.",
                         "one",
                         "two",
                         "three",
                         "first",
                         "second",
                         "quoted",
                         "int x = 1;\n\nreturn x;",
                         ""};
  LOKA_VERIFY(blocks.result() == Markdown::READY);
  LOKA_VERIFY(blocks.size() == 12);
  LOKA_VERIFY(countBlocks(&h.main()) == blocks.size());
  AttributedString before[kMaxBlocks];
  State<AttributedString> *identity[kMaxBlocks];
  unsigned changes[kMaxBlocks] = {0};
  for (unsigned i = 0; i < kMaxBlocks; ++i)
  {
    PreviewSeat &seat = MarkdownEditorTestAccess::seat(h.main(), i);
    if (i < blocks.size())
    {
      LOKA_VERIFY(blocks.at(i).kind() == kinds[i]);
      LOKA_VERIFY(plain(blocks.at(i).text()).equals(String(texts[i])));
      LOKA_VERIFY(seat.kind.get() == kinds[i]);
      LOKA_VERIFY(seat.text.get() == blocks.at(i).text());
    }
    LOKA_VERIFY(seat.visible.get() == (i < blocks.size()));
    before[i] = seat.text.get();
    identity[i] = seat.text.state();
    identity[i]->bind(&changed, &changes[i], false);
  }
  const AttributedString &paragraph = blocks.at(1).text();
  LOKA_VERIFY(paragraph.segment(1).style == Bold);
  LOKA_VERIFY(paragraph.segment(3).style == Italic);
  LOKA_VERIFY(paragraph.segment(5).style == Italic + Secondary);
  const ListRevision revision = h.lines().revision().get();
  h.replace(2, 45, "Changed **bold** paragraph.");
  LOKA_VERIFY(h.lines().revision().get().structure == revision.structure);
  LOKA_VERIFY(h.lines().revision().get().content != revision.content);
  for (unsigned i = 0; i < kMaxBlocks; ++i)
  {
    PreviewSeat &seat = MarkdownEditorTestAccess::seat(h.main(), i);
    LOKA_VERIFY(seat.text.state() == identity[i]);
    LOKA_VERIFY(changes[i] == (i == 1 ? 1u : 0u));
    if (i != 1)
    {
      LOKA_VERIFY(seat.text.get() == before[i]);
      if (before[i].segmentCount())
        LOKA_VERIFY(&seat.text.get().segment(0) == &before[i].segment(0));
    }
    identity[i]->unbind(&changed, &changes[i]);
  }
  LOKA_VERIFY(countBlocks(&h.main()) == blocks.size());
  LOKA_VERIFY(plain(MarkdownEditorTestAccess::seat(h.main(), 1).text.get())
                  .equals(String("Changed bold paragraph. Joined line.")));
  scene::Node *rule = find(&h.main(), "MarkdownEditor.Rule");
  LOKA_VERIFY(rule);
  const RectSurfaceNode &surface = *static_cast<RectSurfaceNode *>(rule);
  LOKA_VERIFY(surface.props.height_ == 1);
  LOKA_VERIFY(surface.props.model_->get().rects[0].height == 1);
  scene::LayoutState offer;
  offer.x = 0;
  offer.y = 0;
  offer.width = 400;
  offer.height = 0;
  offer.spacing = 0;
  offer.lineHeight = 12;
  LOKA_VERIFY(h.platform.projectLayoutForTesting(rule, offer) == 1);
  h.replace(5, 10, "### Small");
  LOKA_VERIFY(MarkdownEditorTestAccess::seat(h.main(), 2).kind.get() == HEADING3);
  h.replace(9, 5, "ordinary");
  LOKA_VERIFY(MarkdownEditorTestAccess::seat(h.main(), 4).kind.get() == PARAGRAPH);
  h.replaceAll("Only one paragraph.");
  LOKA_VERIFY(countBlocks(&h.main()) == 1);
  for (unsigned i = 1; i < kMaxBlocks; ++i)
    LOKA_VERIFY(!MarkdownEditorTestAccess::seat(h.main(), i).visible.get());
  h.replaceAll("");
  LOKA_VERIFY(countBlocks(&h.main()) == 0);
}

void testMarkdownEditorKindsCapacityAndFallback()
{
  Harness h;
  h.replace(0, 0, "### Three\n\n[link](url) ![image](file) *unclosed\n\n```\n**literal**");
  const Markdown blocks(h.lines());
  LOKA_VERIFY(blocks.result() == Markdown::READY && blocks.size() == 3);
  LOKA_VERIFY(blocks.at(0).kind() == HEADING3);
  LOKA_VERIFY(plain(blocks.at(1).text()).equals(String("[link](url) ![image](file) *unclosed")));
  LOKA_VERIFY(plain(blocks.at(2).text()).equals(String("**literal**")));
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  const Markdown refused(h.lines());
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 0);
  LOKA_VERIFY(refused.result() == Markdown::REFUSED);
  const AttributedString before = MarkdownEditorTestAccess::seat(h.main(), 0).text.get();
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  h.replace(0, 9, "### Changed");
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 0);
  LOKA_VERIFY(MarkdownEditorTestAccess::seat(h.main(), 0).text.get() == before);
  LOKA_VERIFY(!MarkdownEditorTestAccess::error(h.main()).empty());
  h.replace(0, 11, "### Recovered");
  LOKA_VERIFY(plain(MarkdownEditorTestAccess::seat(h.main(), 0).text.get()).equals(String("Recovered")));
  char overflow[65 * 4 + 1];
  for (unsigned i = 0; i < 65; ++i)
    std::memcpy(overflow + i * 4, "# h\n", 4);
  overflow[65 * 4] = 0;
  h.replaceAll(overflow);
  LOKA_VERIFY(Markdown(h.lines()).result() == Markdown::CAPACITY);
  LOKA_VERIFY(countBlocks(&h.main()) == 3);
  LOKA_VERIFY(plain(MarkdownEditorTestAccess::seat(h.main(), 0).text.get()).equals(String("Recovered")));
  h.replaceAll("Recovered again");
  LOKA_VERIFY(countBlocks(&h.main()) == 1);
  PushStateTracker tracker;
  ObservableList<String> many;
  LOKA_VERIFY(many.attach(&tracker, 65) == ATTACH_OK);
  for (unsigned short i = 0; i < 64; ++i)
    LOKA_VERIFY(many.insert(i, String("# Heading")) == EDIT_OK);
  LOKA_VERIFY(Markdown(many).result() == Markdown::READY);
  LOKA_VERIFY(many.insert(64, String("# Overflow")) == EDIT_OK);
  LOKA_VERIFY(Markdown(many).result() == Markdown::CAPACITY);
}
#endif
