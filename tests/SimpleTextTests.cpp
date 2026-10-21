#include "support/LayoutY.hpp"
#include "SimpleTextTests.hpp"

// Null/generic-file integration only: desktop runners link their native core
// libraries, so they must not instantiate this POSIX fixture or file rail.
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(LOKA_RETRO68)
#include "../example/SimpleText/src/MyAppConfig.hpp"
#include "app/MenuComposition.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "app/layout/BoxLayout.hpp"
#include <sys/stat.h>
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullApp.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace loka::core;
using namespace loka::app;
using loka::file::File;

class SimpleTextTestAccess
{
public:
  static void replayClose(simpletext::MainNode &n) { n.onCloseAsked(); }
  static ObservableList<String> &lines(simpletext::MainNode &n)
  {
    return n.lines_;
  }
  static FileChooserResult current(const simpletext::MainNode &n)
  {
    return n.currentFile();
  }
  static void publish(simpletext::MainNode &n, const FileChooserResult &file)
  {
    n.publishFacts(file);
  }
  static simpletext::Operation operation(const simpletext::MainNode &n)
  {
    return n.operation_.get();
  }
  static String error(const simpletext::MainNode &n)
  {
    return n.error_.get();
  }
  static LineCursor caret(const simpletext::MainNode &n)
  {
    return n.caret_.get();
  }
  static void postCaret(simpletext::MainNode &n, LineCursor value)
  {
    n.caret_.set(value);
  }
  static void choose(simpletext::MainNode &n, bool save, const FileChooserResult &value)
  {
    (save ? n.saveResult_ : n.openResult_).set(value, true);
  }
  static loka::app::CommandSet<simpletext::FileCommand, simpletext::FILE_COMMAND_COUNT> &
  commands(simpletext::MainNode &n)
  {
    return n.commands_;
  }
};

namespace
{
  std::string utf8(const String &value)
  {
    std::string out;
    LOKA_VERIFY(loka::platform::CollectUtf8(value, out));
    return out;
  }
  // About, Quit: the Null attachment numbers items across menus,
  // so File's commands follow these.
  const unsigned kAppMenuItems = 2;

  const MenuItemDefinition *itemAt(const MenuDefinition &menu, unsigned index)
  {
    const MenuItemDefinition *item = menu.itemsHead();
    while (item && index--)
      item = item->nextInComposition;
    return item;
  }
  scene::Node *find(scene::Node *node, const char *id)
  {
    if (!node)
      return 0;
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
  struct Harness
  {
    std::string directory;
    NullPlatformContext context;
    SimpleTextAppConfig config;
    NullScenePlatformController platform;
    scene::Scene *scene;
    explicit Harness(bool keyed = true)
        : config(&this->context),
          scene(0)
    {
      char pattern[] = "/tmp/loka-simpletext-XXXXXX";
      char *created = mkdtemp(pattern);
      LOKA_VERIFY(created);
      this->directory = created;
      this->context.setApplicationDirectory(String(this->directory));
      simpletext::MainProps props;
      props.platformContext(&this->context).documents(&this->config.documents());
      if (keyed)
        props.document(this->config.documents().idAt(0));
      else
        LOKA_VERIFY(this->config.documents().close(this->config.documents().idAt(0)) == EDIT_OK);
      this->scene = new scene::Scene(scene::Boundary<simpletext::MainNode>(props).clone());
      this->scene->mount(&this->platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(*this->scene, true);
    }
    ~Harness()
    {
      loka::dsl::testing::SceneTestAccess::unmount(*this->scene);
      delete this->scene;
      std::remove((this->directory + "/first.txt").c_str());
      std::remove((this->directory + "/second.txt").c_str());
      std::remove((this->directory + "/bad.txt").c_str());
      std::remove((this->directory + "/long.txt").c_str());
      LOKA_VERIFY(rmdir(this->directory.c_str()) == 0);
    }
    simpletext::MainNode &main() const
    {
      return *static_cast<simpletext::MainNode *>(loka::dsl::testing::SceneTestAccess::rootNode(*this->scene));
    }
    ObservableList<String> &lines()
    {
      return SimpleTextTestAccess::lines(this->main());
    }
    File file(const char *name)
    {
      return File::Application() << File(name);
    }
    void put(const char *name, const std::string &bytes)
    {
      std::FILE *f = std::fopen((this->directory + "/" + name).c_str(), "wb");
      LOKA_VERIFY(f);
      LOKA_VERIFY(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
      LOKA_VERIFY(std::fclose(f) == 0);
    }
    std::string bytes(const char *name)
    {
      std::FILE *f = std::fopen((this->directory + "/" + name).c_str(), "rb");
      LOKA_VERIFY(f);
      std::string value;
      int ch;
      while ((ch = std::fgetc(f)) != EOF)
        value += static_cast<char>(ch);
      LOKA_VERIFY(!std::ferror(f));
      LOKA_VERIFY(std::fclose(f) == 0);
      return value;
    }
    void flush()
    {
      if (this->scene->hasPendingInvalidation())
        this->scene->flushInvalidation();
    }
    void open(const char *name)
    {
      SimpleTextTestAccess::commands(this->main()).slot<simpletext::OPEN_DOCUMENT>()->emit();
      this->flush();
      this->choose(false, FileChooserResult::File(this->file(name)));
    }
    void choose(bool save, const FileChooserResult &result)
    {
      SimpleTextTestAccess::choose(this->main(), save, result);
      this->flush();
    }
    std::string row(unsigned short i)
    {
      return utf8(this->lines().at(i).value);
    }
    bool hasError()
    {
      return !SimpleTextTestAccess::error(this->main()).equals(String());
    }
    simpletext::Document facts() const
    {
      const simpletext::Document *entry = this->config.documents().find(this->main().props.document());
      LOKA_VERIFY(entry);
      return *entry;
    }
    void currentIs(const char *name)
    {
      const FileChooserResult current = SimpleTextTestAccess::current(this->main());
      LOKA_VERIFY(current.kind == FileChooserResult::RESULT_FILE);
      LOKA_VERIFY(!(current.item != this->file(name)));
    }
  };

  class SimpleTextApp : public NullApp
  {
  public:
    explicit SimpleTextApp(SimpleTextAppConfig &config)
        : NullApp(&config)
    {
    }
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    AppComponentGroup &group()
    {
      return *this->group_;
    }
    void tail()
    {
      RunWindowAdmissionOperation(*this);
    }
    NullWindow &window(unsigned index)
    {
      return *static_cast<NullWindow *>(this->adoptedWindows().at(index));
    }
    simpletext::MainNode &main(unsigned index)
    {
      return *static_cast<simpletext::MainNode *>(
          loka::dsl::testing::SceneTestAccess::rootNode(*this->window(index).scene()));
    }
  };

  /** Test-only instrumentation delegates every composition to the real Main. */
  class CountedMain : public simpletext::MainNode
  {
  public:
    explicit CountedMain(const simpletext::MainProps &props)
        : simpletext::MainNode(props),
          compositions_(0)
    {
    }
    virtual void composeNode(scene::NodeComposition &c)
    {
      ++this->compositions_;
      simpletext::MainNode::composeNode(c);
    }
    unsigned compositions() const
    {
      return this->compositions_;
    }

  private:
    unsigned compositions_;
  };
  class CountingContext : public NullPlatformContext
  {
  public:
    virtual Window *createWindow(const WindowProps &props)
    {
      // Preserve the production factory's Props; only instrument its root type.
      const scene::NodeDefinitionBase *definition = props.peekInitialScene()->getRootDefinition();
      const simpletext::MainProps &mainProps = *static_cast<const simpletext::MainProps *>(definition->propsBase());
      WindowProps counted(props);
      counted.scene(new scene::Scene(scene::Boundary<CountedMain>(mainProps).clone()));
      return NullPlatformContext::createWindow(counted);
    }
  };
}

void testSimpleTextNewOpensASecondWindow()
{
  CountingContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  simpletext::MainNode &first = app.main(0);
  ObservableList<String> &lines = SimpleTextTestAccess::lines(first);
  LOKA_VERIFY(lines.size() == 1 && lines.at(0).value.empty());
  LOKA_VERIFY(static_cast<CountedMain &>(first).compositions() == 1);
  LOKA_VERIFY(utf8(config.documents().find(first.props.document())->title) == "Untitled");
  LOKA_VERIFY(!config.documents().find(first.props.document())->edited);
  LOKA_VERIFY(!app.window(0).editedState().get());
  LOKA_VERIFY(app.window(0).frameState().get().x == 16 && app.window(0).frameState().get().y == 16);
  LOKA_VERIFY(lines.update(lines.at(0).id, String("unsaved first document")) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(config.documents().find(first.props.document())->edited);
  LOKA_VERIFY(app.window(0).editedState().get());
  const ListRevision dirtyFacts = config.documents().revision().get();
  LOKA_VERIFY(lines.update(lines.at(0).id, String("unsaved first document")) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(config.documents().revision().get() == dirtyFacts);
  const ListRevision revision = lines.revision().get();
  SimpleTextTestAccess::commands(first).slot<simpletext::NEW_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 2 && config.documents().count() == 2);
  LOKA_VERIFY(&app.main(0) == &first);
  LOKA_VERIFY(!(lines.revision().get() != revision));
  LOKA_VERIFY(lines.at(0).value.equals(String("unsaved first document")));
  LOKA_VERIFY(static_cast<CountedMain &>(first).compositions() == 1);
  ObservableList<String> &second = SimpleTextTestAccess::lines(app.main(1));
  LOKA_VERIFY(second.size() == 1 && second.at(0).value.empty());
  LOKA_VERIFY(static_cast<CountedMain &>(app.main(1)).compositions() == 1);
  LOKA_VERIFY(app.window(1).frameState().get().x == 40 && app.window(1).frameState().get().y == 40);
  LOKA_VERIFY(!app.window(1).editedState().get());
}

void testSimpleTextNewAtCapacitySetsTheError()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  while (config.documents().count() < simpletext::kMaxDocuments)
    LOKA_VERIFY(!config.documents().open(simpletext::Document()).isNone());
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == simpletext::kMaxDocuments);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::NEW_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(utf8(SimpleTextTestAccess::error(app.main(0))) == "Cannot open another document window.");
  LOKA_VERIFY(app.group().getComponents().size() == simpletext::kMaxDocuments);
  LOKA_VERIFY(config.documents().count() == simpletext::kMaxDocuments);
  LOKA_VERIFY(config.documents().idAt(7).seq == 8);
  LOKA_VERIFY(app.window(7).frameState().get().x == 184 && app.window(7).frameState().get().y == 184);
  LOKA_VERIFY(config.documents().close(config.documents().idAt(7)) == EDIT_OK);
  app.tail();
  const ItemId ninth = config.documents().open(simpletext::Document());
  LOKA_VERIFY(ninth.seq == 9);
  app.tail();
  Window *wrapped = static_cast<Window *>(app.group().find(ninth));
  LOKA_VERIFY(wrapped && wrapped->frameState().get().x == 16 && wrapped->frameState().get().y == 16);
}

void testSimpleTextClosingAWindowDropsItsDocument()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId first = config.documents().idAt(0);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::NEW_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 2 && config.documents().count() == 2);
  app.window(0).simulateNativeClose();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  LOKA_VERIFY(!config.documents().find(first) && !app.quitRequested());
  app.window(1).simulateNativeClose();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 0 && config.documents().count() == 0);
  LOKA_VERIFY(app.quitRequested());
}

// Mutation: close idAt(0) instead of the window's own Props identity.
void testSimpleTextCloseCommandClosesItsOwnWindow()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  simpletext::MainNode *firstMain = &app.main(0);
  Window *firstWindow = &app.window(0);
  const ItemId first = config.documents().idAt(0);
  SimpleTextTestAccess::commands(*firstMain).slot<simpletext::NEW_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 2 && config.documents().count() == 2);
  const ItemId second = config.documents().idAt(1);
  SimpleTextTestAccess::commands(app.main(1)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  LOKA_VERIFY(config.documents().find(first) && !config.documents().find(second));
  LOKA_VERIFY(app.group().find(first) == firstWindow && &app.main(0) == firstMain);
  LOKA_VERIFY(!app.quitRequested());
}

// Mutation: require EDIT_OK from the second Close (debug assert).
void testSimpleTextCloseTwiceBeforeAdmissionClosesOnce()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId first = config.documents().idAt(0);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::NEW_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 2 && config.documents().count() == 2);
  SimpleTextTestAccess::commands(app.main(1)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  SimpleTextTestAccess::commands(app.main(1)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  LOKA_VERIFY(config.documents().count() == 1 && app.group().getComponents().size() == 2);
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  LOKA_VERIFY(config.documents().find(first) && !app.quitRequested());
}

// Characterization: frozen A-prime keeps today's last-window quit rule.
void testSimpleTextCloseOfTheLastWindowQuits()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 0 && config.documents().count() == 0);
  LOKA_VERIFY(app.quitRequested());
}

// Mutation: remove closeDocument's operation guard.
void testSimpleTextCloseDuringAChooserDoesNothing()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId id = config.documents().idAt(0);
  Window *window = &app.window(0);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::OPEN_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(SimpleTextTestAccess::operation(app.main(0)) == simpletext::OPEN);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  LOKA_VERIFY(config.documents().find(id) && app.group().find(id) == window);
  LOKA_VERIFY(SimpleTextTestAccess::operation(app.main(0)) == simpletext::OPEN);
  LOKA_VERIFY(!app.quitRequested());
}

void testSimpleTextLaunchWindowTakesTheSeat()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && config.documents().count() == 1);
  LOKA_VERIFY(app.adoptedWindows().size() == 1);
  const ItemId key = app.group().keyOf(&app.window(0));
  LOKA_VERIFY(!key.isNone() && key == config.documents().idAt(0));
  ObservableList<String> &lines = SimpleTextTestAccess::lines(app.main(0));
  LOKA_VERIFY(lines.size() == 1 && lines.at(0).value.empty());
}

void testSimpleTextOpenSaveAndSaveAs()
{
  Harness h;
  LOKA_VERIFY(utf8(h.facts().title) == "Untitled" && !h.facts().edited);
  LOKA_VERIFY(h.facts().file.kind == FileChooserResult::RESULT_NONE);
  const File named = h.file("notes:2026.txt");
  SimpleTextTestAccess::publish(h.main(), FileChooserResult::File(named));
  LOKA_VERIFY(h.facts().title.equals(named.name()));
  LOKA_VERIFY(utf8(h.facts().title) == "notes:2026.txt");
  SimpleTextTestAccess::publish(h.main(), FileChooserResult::File(File("unresolved")));
  LOKA_VERIFY(utf8(h.facts().title) == "Untitled");
  SimpleTextTestAccess::publish(h.main(), FileChooserResult());
  LOKA_VERIFY(utf8(h.facts().title) == "Untitled");
  h.put("first.txt", "alpha\nbeta\n");
  h.open("first.txt");
  LOKA_VERIFY(h.facts().title.equals(h.file("first.txt").name()) && !h.facts().edited);
  LOKA_VERIFY(h.lines().size() == 3 && h.row(0) == "alpha" && h.row(1) == "beta" && h.row(2).empty());
  h.currentIs("first.txt");
  LOKA_VERIFY(!h.hasError());
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(h.bytes("first.txt") == "alpha\nbeta\n");
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("edited")) == EDIT_OK);
  h.flush();
  LOKA_VERIFY(h.facts().edited);
  LOKA_VERIFY(h.facts().title.equals(h.file("first.txt").name()));
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
  h.flush();
  h.choose(true, FileChooserResult::File(h.file("second.txt")));
  h.currentIs("second.txt");
  LOKA_VERIFY(h.facts().title.equals(h.file("second.txt").name()) && !h.facts().edited);
  LOKA_VERIFY(h.bytes("second.txt") == "edited\nbeta\n");
  LOKA_VERIFY(h.bytes("first.txt") == "alpha\nbeta\n");
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("again")) == EDIT_OK);
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(h.bytes("second.txt") == "again\nbeta\n");
  h.flush();
  LOKA_VERIFY(!h.facts().edited && h.facts().title.equals(h.file("second.txt").name()));
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("dirty before open")) == EDIT_OK);
  h.flush();
  LOKA_VERIFY(h.facts().edited);
  h.open("first.txt");
  LOKA_VERIFY(h.facts().title.equals(h.file("first.txt").name()) && !h.facts().edited);

  // Use the actual admission path to pin both edges of the projected mark.
  SimpleTextAppConfig config(&h.context);
  SimpleTextApp app(config);
  app.run();
  simpletext::MainNode &main = app.main(0);
  ObservableList<String> &lines = SimpleTextTestAccess::lines(main);
  LOKA_VERIFY(lines.update(lines.at(0).id, String("window edit")) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(app.window(0).editedState().get());
  SimpleTextTestAccess::commands(main).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
  SimpleTextTestAccess::choose(main, true, FileChooserResult::File(h.file("second.txt")));
  app.tail();
  LOKA_VERIFY(!app.window(0).editedState().get());
  LOKA_VERIFY(app.window(0).titleState().get().equals(h.file("second.txt").name()));
  LOKA_VERIFY(lines.update(lines.at(0).id, String("window edit again")) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(app.window(0).editedState().get());
  SimpleTextTestAccess::commands(main).slot<simpletext::SAVE_DOCUMENT>()->emit();
  app.tail();
  LOKA_VERIFY(!app.window(0).editedState().get());
}

void testSimpleTextNewAndSaveWithoutDestination()
{
  {
    Harness unkeyed(false);
    const ListRevision revision = unkeyed.config.documents().revision().get();
    LOKA_VERIFY(unkeyed.main().props.document().isNone());
    LOKA_VERIFY(unkeyed.lines().update(unkeyed.lines().at(0).id, String("unkeyed")) == EDIT_OK);
    unkeyed.flush();
    SimpleTextTestAccess::commands(unkeyed.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
    unkeyed.choose(true, FileChooserResult::File(unkeyed.file("first.txt")));
    LOKA_VERIFY(unkeyed.bytes("first.txt") == "unkeyed");
    LOKA_VERIFY(unkeyed.config.documents().count() == 0);
    LOKA_VERIFY(unkeyed.config.documents().revision().get() == revision);
    LOKA_VERIFY(SimpleTextTestAccess::current(unkeyed.main()).kind == FileChooserResult::RESULT_NONE);
    SimpleTextTestAccess::publish(unkeyed.main(), FileChooserResult::File(unkeyed.file("first.txt")));
    LOKA_VERIFY(unkeyed.config.documents().count() == 0);
    LOKA_VERIFY(unkeyed.config.documents().revision().get() == revision);
  }
  {
    Harness keyed;
    SimpleTextTestAccess::publish(keyed.main(), FileChooserResult::File(keyed.file("first.txt")));
    LOKA_VERIFY(keyed.facts().title.equals(keyed.file("first.txt").name()));
    LOKA_VERIFY(keyed.config.documents().close(keyed.main().props.document()) == EDIT_OK);
    LOKA_VERIFY(SimpleTextTestAccess::current(keyed.main()).kind == FileChooserResult::RESULT_NONE);
    SimpleTextTestAccess::commands(keyed.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
    keyed.flush();
    LOKA_VERIFY(SimpleTextTestAccess::operation(keyed.main()) == simpletext::SAVE);
    LOKA_VERIFY(keyed.config.documents().count() == 0);
  }
  Harness h;
  LOKA_VERIFY(h.lines().size() == 1 && h.row(0).empty());
  LOKA_VERIFY(SimpleTextTestAccess::current(h.main()).kind == FileChooserResult::RESULT_NONE);
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  h.flush();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::SAVE);
  LOKA_VERIFY(find(&h.main(), "SimpleText.Save"));
  h.choose(true, FileChooserResult::File(h.file("first.txt")));
  h.currentIs("first.txt");
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("old")) == EDIT_OK);
  h.open("missing.txt");
  LOKA_VERIFY(h.hasError());
  for (unsigned short row = 1; row < TextEditorProps::kMaxLines; ++row)
    LOKA_VERIFY(h.lines().insert(row, String("old")) == EDIT_OK);
  const ItemId old = h.lines().at(0).id;
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>()->emit();
  LOKA_VERIFY(h.lines().size() == TextEditorProps::kMaxLines && h.row(0) == "old" && h.lines().at(0).id == old);
  h.currentIs("first.txt");
  LOKA_VERIFY(h.hasError());
  LOKA_VERIFY(h.config.documents().count() == 2);
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
  LOKA_VERIFY(h.bytes("first.txt").substr(0, 3) == "old");
}

void testSimpleTextPendingCommandsAndTerminalResults()
{
  Harness h;
  h.put("first.txt", "kept");
  h.open("first.txt");
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("unsaved")) == EDIT_OK);
  for (int saving = 0; saving != 2; ++saving)
  {
    SimpleTextTestAccess::commands(h.main())
        .emitter(saving ? simpletext::SAVE_AS_DOCUMENT : simpletext::OPEN_DOCUMENT)
        ->emit();
    h.flush();
    const ListRevision revision = h.lines().revision().get();
    const unsigned short documents = h.config.documents().count();
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>()->emit();
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>()->emit();
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
    LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == (saving ? simpletext::SAVE : simpletext::OPEN));
    LOKA_VERIFY(h.config.documents().count() == documents);
    LOKA_VERIFY(!(h.lines().revision().get() != revision));
    LOKA_VERIFY(h.row(0) == "unsaved" && h.bytes("first.txt") == "kept");
    h.currentIs("first.txt");
    h.choose(saving != 0, FileChooserResult());
    LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) != simpletext::NONE);
    h.choose(saving != 0, FileChooserResult::Canceled());
    LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
    LOKA_VERIFY(!(h.lines().revision().get() != revision));
    LOKA_VERIFY(!h.hasError());
    LOKA_VERIFY(!find(&h.main(), "SimpleText.Open") && !find(&h.main(), "SimpleText.Save"));
  }
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>()->emit();
  h.choose(false, FileChooserResult::Error(17));
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE && h.hasError());
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
  h.choose(true, FileChooserResult::Error(18));
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE && h.hasError());
  LOKA_VERIFY(h.row(0) == "unsaved");
  h.currentIs("first.txt");
}

void testSimpleTextReadAndWriteFailuresPreserveDestination()
{
  Harness h;
  h.put("first.txt", "old");
  h.put("bad.txt", std::string("bad\x80", 4));
  h.open("first.txt");
  const ItemId id = h.lines().at(0).id;
  const ListRevision revision = h.lines().revision().get();
  h.open("bad.txt");
  LOKA_VERIFY(h.hasError() && h.row(0) == "old" && h.lines().at(0).id == id);
  LOKA_VERIFY(utf8(SimpleTextTestAccess::error(h.main())) == "Cannot open the file: it is not plain ASCII text.");
  LOKA_VERIFY(!(h.lines().revision().get() != revision));
  h.currentIs("first.txt");
  h.put("long.txt", std::string(9000, 'a'));
  h.open("long.txt");
  LOKA_VERIFY(utf8(SimpleTextTestAccess::error(h.main())) == "Cannot open the file: it is too long for the editor.");
  h.open("absent.txt");
  LOKA_VERIFY(utf8(SimpleTextTestAccess::error(h.main())) == "Cannot open the file.");
  LOKA_VERIFY(h.row(0) == "old" && h.lines().at(0).id == id);
  h.currentIs("first.txt");
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
  h.choose(true, FileChooserResult::File(h.file("missing/child.txt")));
  LOKA_VERIFY(h.hasError() && h.row(0) == "old");
  LOKA_VERIFY(utf8(SimpleTextTestAccess::error(h.main())).find("destination may have changed") != std::string::npos);
  h.currentIs("first.txt");
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
  h.choose(true, FileChooserResult::File(File("/dev/full")));
  LOKA_VERIFY(h.hasError() && h.row(0) == "old");
  h.currentIs("first.txt");
  LOKA_VERIFY(h.lines().update(id, String("still here")) == EDIT_OK);
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(h.bytes("first.txt") == "still here" && !h.hasError());
  LOKA_VERIFY(std::remove((h.directory + "/first.txt").c_str()) == 0);
  LOKA_VERIFY(mkdir((h.directory + "/first.txt").c_str(), 0700) == 0);
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(h.hasError() && h.row(0) == "still here");
  h.currentIs("first.txt");
  LOKA_VERIFY(rmdir((h.directory + "/first.txt").c_str()) == 0);
}

void testSimpleTextCommitExhaustionPreservesDestination()
{
  Harness h;
  h.put("first.txt", "kept");
  h.put("second.txt", "replacement");
  h.open("first.txt");
  // Exhaust the real list identity budget; no production test hook needed.
  while (h.lines().at(0).id.seq != 65535)
  {
    LOKA_VERIFY(h.lines().remove(h.lines().at(0).id) == EDIT_OK);
    LOKA_VERIFY(h.lines().insert(0, String("kept")) == EDIT_OK);
  }
  const ItemId id = h.lines().at(0).id;
  const ListRevision revision = h.lines().revision().get();
  h.open("second.txt");
  LOKA_VERIFY(h.hasError() && h.row(0) == "kept" && h.lines().at(0).id == id);
  LOKA_VERIFY(!(h.lines().revision().get() != revision));
  h.currentIs("first.txt");
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>()->emit();
  LOKA_VERIFY(h.hasError() && h.lines().at(0).id == id);
  LOKA_VERIFY(h.row(0) == "kept" && !(h.lines().revision().get() != revision));
  LOKA_VERIFY(h.config.documents().count() == 2);
  h.currentIs("first.txt");
  SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
  LOKA_VERIFY(h.bytes("first.txt") == "kept" && h.bytes("second.txt") == "replacement");
}

void testSimpleTextRepeatedOpenAndCaretReplacement()
{
  Harness h;
  h.put("first.txt", "abc\ndef");
  h.open("first.txt");
  const ItemId first = h.lines().at(0).id;
  const ItemId second = h.lines().at(1).id;
  {
    scene::BorrowScope borrow(h.platform);
    // Keep the rail from consuming requests while checking the app's post.
    SimpleTextTestAccess::postCaret(h.main(), LineCursor(second, 2));
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>()->emit();
    SimpleTextTestAccess::choose(h.main(), false, FileChooserResult::File(h.file("first.txt")));
    LOKA_VERIFY(h.lines().at(0).id != first);
    LOKA_VERIFY(SimpleTextTestAccess::caret(h.main()) == LineCursor(h.lines().at(0).id, 0));
  }
  h.flush();
  h.currentIs("first.txt");
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
  {
    scene::BorrowScope borrow(h.platform);
    SimpleTextTestAccess::postCaret(h.main(), LineCursor(h.lines().at(1).id, 2));
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>()->emit();
    LOKA_VERIFY(SimpleTextTestAccess::caret(h.main()) == LineCursor(h.lines().at(1).id, 2));
    SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>()->emit();
    LOKA_VERIFY(SimpleTextTestAccess::caret(h.main()) == LineCursor(h.lines().at(1).id, 2));
    LOKA_VERIFY(h.config.documents().count() == 2);
  }
}

void testSimpleTextMenuAndDialogProps()
{
  Harness h;
  const MenuBarDefinition *bar = h.scene->menuBar();
  LOKA_VERIFY(bar && bar->menusCount() == 2);
  // The preset declares About and Quit; native placement belongs to each rail.
  const MenuDefinition *app = bar->menuAt(0);
  LOKA_VERIFY(app && app->isAppMenu && app->itemsCount() == kAppMenuItems);
  LOKA_VERIFY(itemAt(*app, 0)->action == MENU_ACTION_ABOUT_APP);
  LOKA_VERIFY(itemAt(*app, 1)->action == MENU_ACTION_QUIT_APP);
  const MenuDefinition *file = bar->menuAt(1);
  LOKA_VERIFY(file && !file->isAppMenu && file->itemsCount() == 5);
  LOKA_VERIFY(file->title.equals(String("File")));
  const char *titles[] = {"New", "Open...", "Close", "Save", "Save As..."};
  for (int i = 0; i < 5; ++i)
  {
    const MenuItemDefinition *item = itemAt(*file, i);
    LOKA_VERIFY(item && item->title.equals(String(titles[i])));
    LOKA_VERIFY(item->titleState == 0);
    LOKA_VERIFY(!item->isSeparator);
    LOKA_VERIFY(item->hasShortcut == (i == 1 || i == 2 || i == 3));
  }
  LOKA_VERIFY(itemAt(*file, 1)->shortcutKey == 'o' && itemAt(*file, 3)->shortcutKey == 's');
  LOKA_VERIFY(itemAt(*file, 0)->onClickState
              == SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>());
  LOKA_VERIFY(itemAt(*file, 1)->onClickState
              == SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>());
  LOKA_VERIFY(itemAt(*file, 3)->onClickState
              == SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>());
  LOKA_VERIFY(itemAt(*file, 4)->onClickState
              == SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>());
  LOKA_VERIFY(itemAt(*file, 2)->shortcutKey == 'w');
  LOKA_VERIFY(itemAt(*file, 2)->onClickState
              == SimpleTextTestAccess::commands(h.main()).slot<simpletext::CLOSE_DOCUMENT>());
  MenuBarDefinition defaultBar;
  MenuComposition defaultComposition(&defaultBar);
  h.config.composeDefaultMenu(defaultComposition);
  defaultComposition.finish();
  LOKA_VERIFY(defaultBar.menusCount() == 0);
  NullMenuAttachment &attachment = h.platform.menuAttachment();
  LOKA_VERIFY(attachment.open(*bar));
  attachment.dispatch(kAppMenuItems + 2);
  h.flush();
  OpenFileDialogNode *open = static_cast<OpenFileDialogNode *>(find(&h.main(), "SimpleText.Open"));
  LOKA_VERIFY(open && open->props.options_.purpose() == FILE_DIALOG_OPEN);
  LOKA_VERIFY(open->props.options_.filterPolicy() == FILE_DIALOG_FILTER_ALL_FILES_TEXT);
  h.choose(false, FileChooserResult::Canceled());
  attachment.dispatch(kAppMenuItems + 5);
  h.flush();
  OpenFileDialogNode *save = static_cast<OpenFileDialogNode *>(find(&h.main(), "SimpleText.Save"));
  LOKA_VERIFY(save && save->props.options_.purpose() == FILE_DIALOG_SAVE);
  LOKA_VERIFY(save->props.options_.defaultName().equals(String("untitled.txt")));
  LOKA_VERIFY(save->props.options_.filterPolicy() == FILE_DIALOG_FILTER_ALL_FILES_TEXT);
  LOKA_VERIFY(find(&h.main(), "SimpleText.Editor") && !find(&h.main(), "SimpleText.Error"));
  h.choose(true, FileChooserResult::File(h.file("first.txt")));
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("saved through menu")) == EDIT_OK);
  attachment.dispatch(kAppMenuItems + 4);
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
  LOKA_VERIFY(h.bytes("first.txt") == "saved through menu");
  attachment.dispatch(kAppMenuItems + 1);
  LOKA_VERIFY(h.lines().size() == 1 && h.row(0) == "saved through menu");
  h.currentIs("first.txt");
  LOKA_VERIFY(h.config.documents().count() == 2);
}

void testSimpleTextRibbonFiresTheMenuEmitters()
{
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  Harness h;
  LOKA_VERIFY(find(&h.main(), "SimpleText.Ribbon"));
  const char *titles[] = {"New", "Open...", "Save", "Save As..."};
  EmitterState *events[] = {SimpleTextTestAccess::commands(h.main()).slot<simpletext::NEW_DOCUMENT>(),
                          SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>(),
                          SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_DOCUMENT>(),
                          SimpleTextTestAccess::commands(h.main()).slot<simpletext::SAVE_AS_DOCUMENT>()};
  FlowError error;
  for (int i = 0; i < 4; ++i)
  {
    ButtonNode *button = 0;
    LOKA_VERIFY(ResolveSelector(h.scene, WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(i + 1), button, error)
                == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(button && button->props.text_->get().equals(String(titles[i])));
    LOKA_VERIFY(button->props.onClick_ == events[i]);
  }
  ButtonNode *extra = 0;
  LOKA_VERIFY(ResolveSelector(h.scene, WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(5), extra, error)
              != FLOW_STEP_SUCCEEDED);
  h.put("first.txt", "kept\nsecond");
  h.open("first.txt");
  LOKA_VERIFY(h.lines().update(h.lines().at(0).id, String("unsaved")) == EDIT_OK);
  scene::Scene *out = 0;
  LOKA_VERIFY(ClickButton(WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(2)).run(h.scene, out, error)
              == FLOW_STEP_SUCCEEDED);
  h.flush();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::OPEN);
  LOKA_VERIFY(find(&h.main(), "SimpleText.Open"));
  const ListRevision revision = h.lines().revision().get();
  const int refused[] = {1, 3, 4};
  for (int i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(
        ClickButton(WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(refused[i])).run(h.scene, out, error)
        == FLOW_STEP_SUCCEEDED);
    h.flush();
    LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::OPEN);
    LOKA_VERIFY(!(h.lines().revision().get() != revision));
    LOKA_VERIFY(h.row(0) == "unsaved" && h.bytes("first.txt") == "kept\nsecond");
    h.currentIs("first.txt");
  }
  h.choose(false, FileChooserResult::Canceled());
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
  LOKA_VERIFY(!find(&h.main(), "SimpleText.Open"));
  LOKA_VERIFY(ClickButton(WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(4)).run(h.scene, out, error)
              == FLOW_STEP_SUCCEEDED);
  h.flush();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::SAVE);
  LOKA_VERIFY(find(&h.main(), "SimpleText.Save"));
  h.choose(true, FileChooserResult::Canceled());
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::NONE);
  LOKA_VERIFY(!find(&h.main(), "SimpleText.Save"));
  const ItemId old = h.lines().at(0).id;
  LOKA_VERIFY(ClickButton(WithinAnchor("SimpleText.Ribbon").descendant<ButtonNode>(1)).run(h.scene, out, error)
              == FLOW_STEP_SUCCEEDED);
  h.flush();
  LOKA_VERIFY(h.lines().size() == 2 && h.row(0) == "unsaved" && h.lines().at(0).id == old);
  h.currentIs("first.txt");
  LOKA_VERIFY(h.config.documents().count() == 2);
  LOKA_VERIFY(!h.hasError());
}

void testSimpleTextMenuDisconnectsOnUnmount()
{
  Harness h;
  NullMenuAttachment &attachment = h.platform.menuAttachment();
  LOKA_VERIFY(attachment.open(*h.scene->menuBar()));
  attachment.dispatch(kAppMenuItems + 2);
  h.flush();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.main()) == simpletext::OPEN);
  LOKA_VERIFY(find(&h.main(), "SimpleText.Open"));
  loka::dsl::testing::SceneTestAccess::unmount(*h.scene);
  LOKA_VERIFY(!attachment.connected());
  LOKA_VERIFY(attachment.applied() == 0 && attachment.subscriptionCount() == 0);
  // Main and its slots are gone. Dispatch only through the released table.
  // Fixture-owned emitter withdrawal is pinned by testCommandSetWithdrawsOnDetach.
  for (unsigned id = kAppMenuItems + 1; id <= kAppMenuItems + 5; ++id)
    attachment.dispatch(id);
  LOKA_VERIFY(!attachment.connected());
}
namespace
{
  struct LayoutProbe
  {
    explicit LayoutProbe(Harness &h)
        : harness(h),
          editor(),
          band(),
          error()
    {
    }
    Harness &harness;
    scene::LayoutState editor;
    scene::LayoutState band;
    scene::LayoutState error;
    static int columnChild(void *context, scene::Node *node, const scene::LayoutState &state)
    {
      LayoutProbe &probe = *static_cast<LayoutProbe *>(context);
      if (node->testId() == "SimpleText.Editor")
      {
        probe.editor = state;
        return state.y + state.height;
      }
      if (node->asBoxNode())
        return layout::computeBoxLayoutResultY(
            node->asBoxNode(),
            layout::RailSpacing(12, 4, 9),
            state,
            context,
            &LayoutProbe::columnChild,
            layout::resolveLayoutGap(node, state.spacing, layout::RailSpacing(12, 4, 9)),
            &loka::testing::acceptShortLayoutY);
      if (node->asStackNode() && node->testId() != "SimpleText.Ribbon")
        return rowChild(context, node, state);
      if (node->asNestable() && node->testId() != "SimpleText.Ribbon")
        return layout::computeInsetLayoutResultY(node->asNestable()->childrenHead(),
                                                 node->asNestable()->childrenCount(),
                                                 layout::FrameInsets(0),
                                                 false,
                                                 false,
                                                 0,
                                                 0,
                                                 state,
                                                 context,
                                                 &LayoutProbe::columnChild,
                                                 state.spacing,
                                                 &loka::testing::acceptShortLayoutY);
      const int bottom = probe.harness.platform.projectLayoutForTesting(node, state);
      if (node->testId() == "SimpleText.Error")
      {
        probe.error = state;
        probe.error.height = static_cast<short>(bottom - state.y);
      }
      if (node->testId() == "SimpleText.Ribbon")
      {
        probe.band = state;
        probe.band.height = static_cast<short>(bottom - state.y);
      }
      return bottom;
    }
    static int rowChild(void *context, scene::Node *node, const scene::LayoutState &state)
    {
      if (node->asStackNode())
      {
        LOKA_VERIFY(node->asStackNode()->props.effectiveAxis() == STACK_AXIS_COLUMN);
        return layout::computeColumnLayoutResultY(
            node->asStackNode(),
            state,
            context,
            &LayoutProbe::columnChild,
            layout::resolveLayoutGap(node, state.spacing, layout::RailSpacing(12, 4, 9)),
            &loka::testing::acceptShortLayoutY);
      }
      return state.y;
    }
  };
} // namespace

void testSimpleTextEditorReceivesRemainingWindow()
{
  Harness h;
  SimpleTextApp app(h.config);
  app.run();
  LOKA_VERIFY(app.window(0).contentMargin() == layout::LayoutGap::of(0));
  scene::LayoutState viewport;
  viewport.x = 20;
  viewport.y = 30;
  viewport.width = 480;
  viewport.height = 320;
  viewport.lineHeight = 12;
  viewport.spacing = 4;
  for (int phase = 0; phase != 3; ++phase)
  {
    if (phase == 1)
    {
      SimpleTextTestAccess::commands(h.main()).slot<simpletext::OPEN_DOCUMENT>()->emit();
      h.flush();
      h.choose(false, FileChooserResult::Error(5));
    }
    if (phase == 2)
    {
      h.put("first.txt", "loaded");
      h.open("first.txt");
    }
    scene::Node *error = find(&h.main(), "SimpleText.Error");
    LOKA_VERIFY((error != 0) == (phase == 1));
    StackNode *root = h.main().childrenHead()->asStackNode();
    LOKA_VERIFY(root && root->props.effectiveAxis() == STACK_AXIS_ROW);
    StackNode *column = root->childrenHead()->asStackNode();
    LOKA_VERIFY(column && column->props.gap_ == layout::LayoutGap::of(0));
    // Follow Stack and Box nodes with the production allocation routines.
    // Only the editor is intercepted: Null's fake native editor is fixed at 80.
    int consumed = 0;
    for (int size = 0; size != 3; ++size)
    {
      viewport.height = static_cast<short>(size == 0 ? 320 : consumed + (size == 1 ? 3 : 0));
      LayoutProbe probe(h);
      layout::RowLayoutMetrics metrics;
      layout::computeRowLayoutResultY(root,
                                      viewport,
                                      metrics,
                                      layout::RailSpacing(12, viewport.spacing, 9),
                                      0,
                                      &probe,
                                      &LayoutProbe::rowChild,
                                      viewport.spacing,
                                      0);
      LOKA_VERIFY(probe.band.height > 0);
      LOKA_VERIFY(probe.band.x == viewport.x + 9 && probe.band.y == viewport.y + 9);
      LOKA_VERIFY(probe.band.width == viewport.width - 18);
      const int bandBottom = probe.band.y + probe.band.height;
      if (error)
      {
        LOKA_VERIFY(probe.error.y == bandBottom + 9);
        LOKA_VERIFY(probe.error.x == viewport.x + 9 && probe.error.width == viewport.width - 18);
        LOKA_VERIFY(probe.error.height > 0);
        LOKA_VERIFY(probe.editor.y == probe.error.y + probe.error.height + 9);
      }
      else
        LOKA_VERIFY(probe.editor.y == bandBottom + 9);
      LOKA_VERIFY(probe.editor.x == viewport.x && probe.editor.width == viewport.width);
      consumed = probe.editor.y - viewport.y;
      LOKA_VERIFY(probe.editor.height == viewport.height - consumed);
      LOKA_VERIFY(probe.editor.y + probe.editor.height == viewport.y + viewport.height);
      LOKA_VERIFY(size == 0 ? probe.editor.height > 80 : probe.editor.height == (size == 1 ? 3 : 0));
    }
  }
}


void testSimpleTextCloseAskedWhileBusyIsDeclined()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId id = config.documents().idAt(0);
  SimpleTextTestAccess::commands(app.main(0)).slot<simpletext::OPEN_DOCUMENT>()->emit();
  app.tail();
  app.window(0).simulateCloseAsked();
  const CloseAsk ask = config.documents().closeAsked().get();
  app.tail();
  LOKA_VERIFY(config.documents().closeAnswer().get() == CloseAnswer(ask.id, ask.seq));
  LOKA_VERIFY(config.documents().find(id));
  {
    StateTrackerGuard guard(app.main(0).tracker());
    SimpleTextTestAccess::choose(app.main(0), false, FileChooserResult::Canceled());
  }
  app.tail();
  LOKA_VERIFY(SimpleTextTestAccess::operation(app.main(0)) == simpletext::NONE);
  SimpleTextTestAccess::replayClose(app.main(0));
  app.tail();
  LOKA_VERIFY(config.documents().find(id) && !app.quitRequested());
  app.window(0).simulateCloseAsked();
  app.tail();
  LOKA_VERIFY(config.documents().count() == 0);
}

void testSimpleTextQuitAskedBeforeMountIsAnsweredAtAttach()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId first = config.documents().idAt(0);
  // In one turn: a new clean document is opened, the mounted one is closed by
  // hand, and Quit asks the first row, whose window is not admitted yet.
  const ItemId second = config.documents().open(simpletext::Document());
  LOKA_VERIFY(!second.isNone() && config.documents().close(first) == EDIT_OK);
  app.requestQuit();
  LOKA_VERIFY(config.documents().closeAsked().get().id == second);
  app.tail();
  app.tail();
  LOKA_VERIFY(config.documents().count() == 0 && app.quitRequested());
}

namespace
{
  /** Real app admission plus direct result publication, like Harness::choose. */
  struct CloseHarness
  {
    NullPlatformContext context;
    SimpleTextAppConfig config;
    SimpleTextApp app;
    ItemId id;
    std::string directory;
    CloseHarness() : config(&this->context), app(this->config)
    {
      char pattern[] = "/tmp/loka-simpletext-close-XXXXXX";
      char *created = mkdtemp(pattern);
      LOKA_VERIFY(created);
      this->directory = created;
      this->context.setApplicationDirectory(String(this->directory));
      this->app.run();
      this->id = this->config.documents().idAt(0);
      this->edit();
    }
    ~CloseHarness()
    {
      std::remove((this->directory + "/first.txt").c_str());
      LOKA_VERIFY(rmdir(this->directory.c_str()) == 0);
    }
    void edit()
    {
      ObservableList<String> &lines = SimpleTextTestAccess::lines(this->app.main(0));
      LOKA_VERIFY(lines.update(lines.at(0).id, String("edited")) == EDIT_OK);
      this->app.tail();
    }
    CloseAsk ask()
    {
      this->app.window(0).simulateCloseAsked();
      this->app.tail();
      return this->config.documents().closeAsked().get();
    }
    ConfirmDialogNode &dialog()
    {
      scene::Node *node = find(&this->app.main(0), "SimpleText.ConfirmClose");
      LOKA_VERIFY(node && node->asConfirmDialogNode());
      return *node->asConfirmDialogNode();
    }
    void confirm(const ConfirmResult &result)
    {
      // Null has no native confirm producer: mirror the file-result fixtures.
      {
        StateTrackerGuard guard(this->app.main(0).tracker());
        this->dialog().props.result_.set(result, true);
      }
      this->app.tail();
    }
    void choose(const FileChooserResult &result)
    {
      {
        StateTrackerGuard guard(this->app.main(0).tracker());
        SimpleTextTestAccess::choose(this->app.main(0), true, result);
      }
      this->app.tail();
    }
    File file() { return File::Application() << File("first.txt"); }
    void destination()
    {
      SimpleTextTestAccess::commands(this->app.main(0)).slot<simpletext::SAVE_AS_DOCUMENT>()->emit();
      this->app.tail();
      this->choose(FileChooserResult::File(this->file()));
      ObservableList<String> &lines = SimpleTextTestAccess::lines(this->app.main(0));
      LOKA_VERIFY(lines.update(lines.at(0).id, String("saved for close")) == EDIT_OK);
      this->app.tail();
    }
    void stays(const CloseAsk &ask)
    {
      LOKA_VERIFY(this->config.documents().find(this->id));
      LOKA_VERIFY(this->app.group().find(this->id));
      LOKA_VERIFY(SimpleTextTestAccess::operation(this->app.main(0)) == simpletext::NONE);
      LOKA_VERIFY(this->config.documents().closeAnswer().get() == CloseAnswer(ask.id, ask.seq));
      LOKA_VERIFY(!find(&this->app.main(0), "SimpleText.ConfirmClose"));
      LOKA_VERIFY(!find(&this->app.main(0), "SimpleText.Save"));
    }
    void closed()
    {
      LOKA_VERIFY(!this->config.documents().find(this->id));
      LOKA_VERIFY(!this->app.group().find(this->id));
      LOKA_VERIFY(this->app.quitRequested());
    }
  };
}

void testSimpleTextCloseAskedEditedEntersConfirmClose()
{
  CloseHarness h;
  const CloseAnswer before = h.config.documents().closeAnswer().get();
  h.ask();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
  LOKA_VERIFY(h.config.documents().find(h.id));
  LOKA_VERIFY(h.config.documents().closeAnswer().get() == before);
  LOKA_VERIFY(utf8(h.dialog().props.options_.message()) == "Save changes to Untitled?");
  h.confirm(ConfirmResult::none());
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
}

void testSimpleTextConfirmCancelDeclinesAndStays()
{
  CloseHarness h;
  const CloseAsk ask = h.ask();
  h.confirm(ConfirmResult::Cancel());
  h.stays(ask);
  // A later confirmation must accept the same result again.
  const CloseAsk second = h.ask();
  h.confirm(ConfirmResult::Cancel());
  h.stays(second);
}

void testSimpleTextConfirmDiscardCloses()
{
  CloseHarness h;
  h.ask();
  h.confirm(ConfirmResult::Discard());
  h.closed();
}

void testSimpleTextConfirmErrorDeclinesWithMessage()
{
  CloseHarness h;
  const CloseAsk ask = h.ask();
  h.confirm(ConfirmResult::Error(431));
  h.stays(ask);
  LOKA_VERIFY(!SimpleTextTestAccess::error(h.app.main(0)).empty());
}

void testSimpleTextConfirmSaveWithDestinationCommitsThenCloses()
{
  CloseHarness h;
  h.destination();
  h.ask();
  h.confirm(ConfirmResult::Save());
  h.closed();
  std::FILE *file = std::fopen((h.directory + "/first.txt").c_str(), "rb");
  LOKA_VERIFY(file);
  char bytes[32] = {0};
  LOKA_VERIFY(std::fread(bytes, 1, sizeof(bytes), file) == 15);
  LOKA_VERIFY(std::fclose(file) == 0);
  LOKA_VERIFY(std::string(bytes) == "saved for close");
}

void testSimpleTextConfirmSaveWithoutDestinationOpensChooser()
{
  CloseHarness h;
  const CloseAsk ask = h.ask();
  h.confirm(ConfirmResult::Save());
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::SAVE_FOR_CLOSE);
  LOKA_VERIFY(h.config.documents().find(h.id));
  LOKA_VERIFY(h.config.documents().closeAnswer().get() == CloseAnswer::none());
  scene::Node *node = find(&h.app.main(0), "SimpleText.Save");
  LOKA_VERIFY(node && node->asOpenFileDialogNode());
  LOKA_VERIFY(node->asOpenFileDialogNode()->props.options_.purpose() == FILE_DIALOG_SAVE);
  LOKA_VERIFY(!find(&h.app.main(0), "SimpleText.ConfirmClose"));
  h.choose(FileChooserResult::Canceled());
  h.stays(ask);
}

void testSimpleTextConfirmSaveCommitFailureDeclines()
{
  // Same real write-refusal seam as ReadAndWriteFailuresPreserveDestination:
  // a directory at the selected destination cannot be opened for writing.
  for (int hasDestination = 0; hasDestination != 2; ++hasDestination)
  {
    CloseHarness h;
    if (hasDestination)
    {
      h.destination();
      LOKA_VERIFY(std::remove((h.directory + "/first.txt").c_str()) == 0);
    }
    LOKA_VERIFY(mkdir((h.directory + "/first.txt").c_str(), 0700) == 0);
    const CloseAsk ask = h.ask();
    h.confirm(ConfirmResult::Save());
    if (!hasDestination)
      h.choose(FileChooserResult::File(h.file()));
    h.stays(ask);
    LOKA_VERIFY(!SimpleTextTestAccess::error(h.app.main(0)).empty());
    LOKA_VERIFY(h.config.documents().find(h.id)->edited);
    LOKA_VERIFY(rmdir((h.directory + "/first.txt").c_str()) == 0);
  }
}

void testSimpleTextMenuCloseEditedConfirmsWithoutAnswer()
{
  CloseHarness h;
  SimpleTextTestAccess::commands(h.app.main(0)).slot<simpletext::CLOSE_DOCUMENT>()->emit();
  h.app.tail();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
  h.confirm(ConfirmResult::Cancel());
  h.stays(CloseAsk::none());
}

void testSimpleTextAskWhileConfirmingIsDeclinedAsBusy()
{
  CloseHarness h;
  const CloseAsk first = h.ask();
  const CloseAsk second = h.ask();
  LOKA_VERIFY(first.seq != second.seq);
  LOKA_VERIFY(h.config.documents().closeAnswer().get() == CloseAnswer(second.id, second.seq));
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
  LOKA_VERIFY(h.config.documents().find(h.id));
  h.confirm(ConfirmResult::Cancel());
  h.stays(second);
}

void testSimpleTextConfirmSaveChooserSuccessCloses()
{
  CloseHarness h;
  h.ask();
  h.confirm(ConfirmResult::Save());
  h.choose(FileChooserResult::File(h.file()));
  h.closed();
  std::FILE *file = std::fopen((h.directory + "/first.txt").c_str(), "rb");
  LOKA_VERIFY(file);
  char bytes[16] = {0};
  LOKA_VERIFY(std::fread(bytes, 1, sizeof(bytes), file) == 6);
  LOKA_VERIFY(std::fclose(file) == 0);
  LOKA_VERIFY(std::string(bytes) == "edited");
}

void testSimpleTextConfirmSaveChooserNoneAndErrorDecline()
{
  const FileChooserResult results[] = {FileChooserResult(), FileChooserResult::Error(19)};
  for (unsigned i = 0; i != 2; ++i)
  {
    CloseHarness h;
    const CloseAsk ask = h.ask();
    h.confirm(ConfirmResult::Save());
    h.choose(results[i]);
    h.stays(ask);
    if (i)
      LOKA_VERIFY(!SimpleTextTestAccess::error(h.app.main(0)).empty());
  }
}

void testSimpleTextConfirmDeclinesCapturedAsk()
{
  CloseHarness h;
  const CloseAsk first = h.ask();
  const ItemId second = h.config.documents().open(simpletext::Document());
  LOKA_VERIFY(!second.isNone());
  h.app.tail();
  SimpleTextTestAccess::commands(h.app.main(1)).slot<simpletext::OPEN_DOCUMENT>()->emit();
  h.app.window(1).simulateCloseAsked();
  h.app.tail();
  LOKA_VERIFY(h.config.documents().closeAsked().get().id == second);
  h.confirm(ConfirmResult::Cancel());
  h.stays(first);
}

void testSimpleTextConfirmIgnoresResultsOutsideOperation()
{
  CloseHarness h;
  const CloseAsk ask = h.ask();
  scene::NodeState<ConfirmResult> channel = h.dialog().props.result_;
  h.confirm(ConfirmResult::Cancel());
  {
    StateTrackerGuard guard(h.app.main(0).tracker());
    channel.set(ConfirmResult::Discard(), true);
  }
  h.app.tail();
  h.stays(ask);
  SimpleTextTestAccess::commands(h.app.main(0)).slot<simpletext::OPEN_DOCUMENT>()->emit();
  h.app.tail();
  {
    StateTrackerGuard guard(h.app.main(0).tracker());
    channel.set(ConfirmResult::Save(), true);
  }
  h.app.tail();
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::OPEN);
  LOKA_VERIFY(h.config.documents().find(h.id));
}

void testSimpleTextConfirmUsesTitleAtMount()
{
  CloseHarness h;
  const CloseAsk first = h.ask();
  LOKA_VERIFY(utf8(h.dialog().props.options_.message()) == "Save changes to Untitled?");
  h.confirm(ConfirmResult::Cancel());
  h.stays(first);
  // Save As changes the roster title after MainNode has already composed.
  h.destination();
  const CloseAsk second = h.ask();
  LOKA_VERIFY(utf8(h.app.window(0).titleState().get()) == "first.txt");
  LOKA_VERIFY(utf8(h.dialog().props.options_.message()) == "Save changes to first.txt?");
  h.confirm(ConfirmResult::Cancel());
  h.stays(second);
}

void testSessionQuitAsksEditedDocumentAndCancelEnds()
{
  CloseHarness h;
  h.app.requestQuitAtCompletion();
  LOKA_VERIFY(h.config.documents().closeAsked().get() == CloseAsk::none());
  h.app.tail();
  h.app.tail();
  const CloseAsk ask = h.config.documents().closeAsked().get();
  LOKA_VERIFY(ask.id == h.id);
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
  h.confirm(ConfirmResult::Cancel());
  h.stays(ask);
  LOKA_VERIFY(h.app.quitCalls() == 0);
  h.app.requestQuitAtCompletion();
  h.app.tail();
  h.app.tail();
  LOKA_VERIFY(h.config.documents().closeAsked().get() == CloseAsk(h.id, ask.seq + 1));
  LOKA_VERIFY(SimpleTextTestAccess::operation(h.app.main(0)) == simpletext::CONFIRM_CLOSE);
  h.confirm(ConfirmResult::Cancel());
  // Prove the second canceled attempt ended, even after its row disappears.
  const ItemId other = h.config.documents().open(simpletext::Document());
  LOKA_VERIFY(h.config.documents().close(h.id) == EDIT_OK);
  h.app.tail();
  LOKA_VERIFY(h.config.documents().find(other));
  LOKA_VERIFY(h.app.quitCalls() == 0);
}

void testSessionQuitClosesCleanFirstThenAsksEdited()
{
  NullPlatformContext context;
  SimpleTextAppConfig config(&context);
  SimpleTextApp app(config);
  app.run();
  const ItemId first = config.documents().idAt(0);
  const ItemId second = config.documents().open(simpletext::Document());
  app.tail();
  ObservableList<String> &lines = SimpleTextTestAccess::lines(app.main(1));
  LOKA_VERIFY(lines.update(lines.at(0).id, String("edited")) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(!config.documents().find(first)->edited && config.documents().find(second)->edited);
  app.requestQuitAtCompletion();
  LOKA_VERIFY(config.documents().closeAsked().get() == CloseAsk::none());
  app.tail();
  app.tail();
  LOKA_VERIFY(!config.documents().find(first));
  LOKA_VERIFY(config.documents().closeAsked().get().id == second);
  LOKA_VERIFY(SimpleTextTestAccess::operation(app.main(1)) == simpletext::CONFIRM_CLOSE);
  scene::Node *node = find(&app.main(1), "SimpleText.ConfirmClose");
  LOKA_VERIFY(node && node->asConfirmDialogNode());
  {
    StateTrackerGuard guard(app.main(1).tracker());
    node->asConfirmDialogNode()->props.result_.set(ConfirmResult::Discard(), true);
  }
  app.tail();
  app.tail();
  LOKA_VERIFY(config.documents().count() == 0 && app.quitCalls() == 1);
}

#endif
