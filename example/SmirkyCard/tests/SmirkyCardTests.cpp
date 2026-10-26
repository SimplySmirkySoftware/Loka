#include "support/BlobAllocationProbe.hpp"
#include "platform/file/FileLocatorAccess.hpp"
#include "core/resource/Blob.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "MyAppConfig.hpp"
#include "StandaloneFlowAppConfig.hpp"
#include "platform/null/NullApp.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "JsCardBindingRegistry.hpp"
#include "SmirkyMarkup.hpp"
#include "JsClickNode.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/style/CardStyle.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "core/Operation.hpp"
#include "app/OpenFileDialog.hpp"
#include "core/util/OwnedDef.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/file/FileIO.hpp"
#include "support/TestVerify.hpp"
#include "ScriptAlignedAlloc.h"
#include "ScriptEngine.h"
#include <stdint.h>
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_NAMES

namespace smirkycard
{
  namespace testing
  {
    const char *machineCpuClassName(loka::platform::CpuClass cpuClass);

    /** Test-only setup of the runtime issuer; production never calls into this TU. */
    class ExecutionSerialAccess
    {
    public:
      static void seed(ScriptRuntime &runtime, uint32_t value)
      {
        runtime.executionSerial_.last_ = value;
      }
    };

    /** Test-only view of subscription ownership, without a shipped query API. */
    class CardFlowAccess
    {
    public:
      static loka::app::scene::Scene *scene;
      static JsCardNode *card()
      {
        return static_cast<JsCardNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene));
      }
      static bool active() { return card()->flowAdmission_.hasExecution(); }
      static int seatCount()
      {
        return card()->seats_.count();
      }
      static loka::core::resource::Image image()
      {
        for (JsSeatRecord *s = card()->seats_.head(); s; s=s->next)
          if (s->kind == JsSeatRecord::IMAGE) return s->image.get();
        return loka::core::resource::Image::Empty();
      }
      static loka::core::State<loka::core::resource::Image> *imageState()
      {
        for (JsSeatRecord *s = card()->seats_.head(); s; s = s->next)
          if (s->kind == JsSeatRecord::IMAGE) return s->image.state();
        return 0;
      }
      static loka::core::State<loka::app::FileChooserResult> *fileState()
      {
        for (JsSeatRecord *s = card()->seats_.head(); s; s = s->next)
          if (s->kind == JsSeatRecord::FILE_RESULT) return s->file.state();
        return 0;
      }
      static int notifications;
      static ScriptRuntime *settlementRuntime;
      static bool settledStale;
      static void notified(void *)
      {
        ++notifications;
        if (settlementRuntime)
        {
          loka::core::String result, error;
          const bool ok = settlementRuntime->evaluateToString(loka::core::String::Literal(
            "refuses(()=>im.set(saved))"), result, error);
          settledStale = ok && result.compare(loka::core::String::Literal("true")) == 0;
        }
      }
      static void observeImage()
      {
        notifications = 0;
        for (JsSeatRecord *s = card()->seats_.head(); s; s=s->next)
          if (s->kind == JsSeatRecord::IMAGE) s->image.state()->bind(notified, 0, false);
      }
      static void observeSettlement(ScriptRuntime &runtime)
      {
        settlementRuntime = &runtime;
        settledStale = false;
        loka::core::testing::PushStateTrackerTestAccess::defer(*card()->tracker(), notified, 0);
      }
      static void unobserveImage()
      {
        for (JsSeatRecord *s = card()->seats_.head(); s; s=s->next)
          if (s->kind == JsSeatRecord::IMAGE)
          {
            s->image.state()->unbind(notified, 0);
            s->image.state()->deferUnbind(notified, 0);
          }
        settlementRuntime = 0;
      }
      static JSValue detachCall(JSContext *, JSValueConst, int, JSValueConst *)
      {
        detachEntrance();
        return JS_UNDEFINED;
      }
      static void navigate() { card()->requestGo(SMIRKY_CARD_SECOND, CardCarry()); }
      static void writeFile(const loka::app::FileChooserResult &value)
      {
        loka::core::StateTrackerGuard transaction(card()->tracker());
        for (JsSeatRecord *s = card()->seats_.head(); s; s=s->next)
          if (s->kind == JsSeatRecord::FILE_RESULT) s->file.set(value, true);
      }
      static void detachEntrance()
      {
        JsCardNode *card = static_cast<JsCardNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene));
        card->detachNode(card->composition());
      }
      static void writeString(const loka::core::String &value)
      {
        JsCardNode *card = static_cast<JsCardNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene));
        loka::core::StateTrackerGuard transaction(card->tracker());
        card->seats_.head()->string.set(value);
      }
      static JSValue projection(JSContext *ctx, JSValueConst, int, JSValueConst *)
      {
        JsCardNode *card = static_cast<JsCardNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene));
        const loka::core::String value = card->seats_.head()->formatted.get();
        const loka::core::StringBuffer bytes = value.bufferWithEncoding(loka::core::StringEncodingUtf8);
        return JS_NewStringLen(ctx, static_cast<const char *>(bytes.data()), bytes.length());
      }
      static JSValue watchCount(JSContext *ctx, JSValueConst, int, JSValueConst *)
      {
        JsCardNode *card = static_cast<JsCardNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene));
        int count = 0;
        for (CardFlow *flow = card->flows_.head(); flow; flow = flow->next)
          if (flow->subscription_)
            ++count;
        return JS_NewInt32(ctx, count);
      }
    };
    int CardFlowAccess::notifications = 0;
    ScriptRuntime *CardFlowAccess::settlementRuntime = 0;
    bool CardFlowAccess::settledStale = false;
    loka::app::scene::Scene *CardFlowAccess::scene = 0;
  } // namespace testing
} // namespace smirkycard

namespace
{
  class TestLowering : public smirkycard::IJsNodeLowering
  {
  public:
    explicit TestLowering(const char *value)
    {
      name = value;
      kind = 0;
    }
    virtual JSValue build(JSContext *, int, JSValueConst *)
    {
      return JS_UNDEFINED;
    }
    virtual loka::app::scene::NodeDefinitionBase *lower(smirkycard::JsCardNode &, JSContext *, JSValueConst, int)
    {
      return 0;
    }
  };
  void checkRegistry()
  {
    smirkycard::JsCardBindingRegistry registry;
    TestLowering *first = new TestLowering("first");
    LOKA_VERIFY(registry.registerLowering(first) && first->kind == 1);
    TestLowering *duplicateName = new TestLowering("first");
    LOKA_VERIFY(!registry.registerLowering(duplicateName));
    delete duplicateName;
    TestLowering *duplicateKind = new TestLowering("other");
    duplicateKind->kind = 9;
    LOKA_VERIFY(!registry.registerLowering(duplicateKind));
    delete duplicateKind;
    smirkycard::ScriptRuntime runtime;
    loka::core::String result, error;
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal("[VStack().kind,Text('').kind,EditText({}).kind,Button('',()=>{}).kind,Row().kind,"
                                    "Card(Text('')).kind,ZStack(Text('')).kind].join(',')"),
        result,
        error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("1,2,3,4,5,13,14")) == 0);
  }
  loka::app::scene::Node *find(loka::app::scene::Node *node, const char *id)
  {
    if (!node)
      return 0;
    if (node->testId() == id)
      return node;
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *result = find(child, id);
      if (result)
        return result;
    }
    return 0;
  }

  loka::app::scene::Node *outermostBox(loka::app::scene::Node *node)
  {
    if (!node || node->asBoxNode())
      return node;
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *result = outermostBox(child);
      if (result)
        return result;
    }
    return 0;
  }

  void checkEvaluation(smirkycard::ScriptRuntime &runtime)
  {
    char error[256];
    LOKA_VERIFY(runtime.evaluate("['first', 'second'][1 + 0]", error, sizeof(error)) == SMIRKY_CARD_SECOND);
    LOKA_VERIFY(error[0] == '\0');
    const char *failures[] = {
        "(", "throw new Error('example failure')", "'unknown'", "42", "'first\\u0000extra'", "while (true) {}"};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
      LOKA_VERIFY(runtime.evaluate(failures[i], error, sizeof(error)) == SMIRKY_CARD_ERROR);
      LOKA_VERIFY(error[0] != '\0');
      // A failed/interrupting script must not poison the next evaluation.
      LOKA_VERIFY(runtime.evaluate("'first'", error, sizeof(error)) == SMIRKY_CARD_FIRST);
    }
    char tiny[1] = {'x'};
    LOKA_VERIFY(runtime.evaluate("42", tiny, sizeof(tiny)) == SMIRKY_CARD_ERROR);
    LOKA_VERIFY(tiny[0] == '\0');
    LOKA_VERIFY(SmirkyScriptEvaluate(0, "'first'", error, sizeof(error)) == SMIRKY_CARD_ERROR);
  }

  void checkSceneSwitching(smirkycard::ScriptRuntime &runtime)
  {
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);

    for (int i = 0; i < 20; ++i)
    {
      loka::app::scene::Scene *previous = window.scene();
      loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*previous);
      loka::app::scene::Node *title = find(root, "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asAttributedTextNode());
      LOKA_VERIFY(title->asAttributedTextNode()->props.text_->get()
                  == loka::app::Styled(i % 2 ? "Card Two" : "Card One", loka::app::FontSize<18>() + loka::app::Bold));
      loka::app::scene::Node *button = find(root, "SmirkyCard.Run");
      LOKA_VERIFY(button && button->asButtonNode());
      // Exercise the actual binding: C++ button -> JS -> SceneManager handoff.
      button->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == previous);
      LOKA_VERIFY(window.sceneManager()->hasPendingReplacement());
      admission.flush();
      LOKA_VERIFY(window.scene() != previous);
      LOKA_VERIFY(!previous->getAttachedState()->get());
      LOKA_VERIFY(window.sceneManager()->hasRetiredScenes());

      // The replacement must already be mounted by the production path.
      LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()) != 0);
      admission.flush();
      LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
    }
    // Window teardown exercises cleanup of the final mounted card as well.
  }

  std::string textValue(loka::app::TextNode *text)
  {
    const loka::core::StringBuffer buffer = text->props.text_->get().bufferWithEncoding(loka::core::StringEncodingUtf8);
    return std::string(static_cast<const char *>(buffer.data()), buffer.length());
  }

  bool makeDirectory(const char *path)
  {
#if defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
  }

  bool removeDirectory(const char *path)
  {
#if defined(_WIN32)
    return _rmdir(path) == 0;
#else
    return rmdir(path) == 0;
#endif
  }

  void writeMain(const char *path, const std::string &text)
  {
    std::FILE *file = std::fopen(path, "wb");
    LOKA_VERIFY(file != 0);
    LOKA_VERIFY(std::fwrite(text.data(), 1, text.size(), file) == text.size());
    LOKA_VERIFY(std::fclose(file) == 0);
  }

  std::string reloadCardSource(const char *title)
  {
    return std::string("card('first',class{constructor(c){this.c=c;}compose(){return VStack(Text('") + title
           + "').TEST_ID('SmirkyCard.Title'),Button('r',()=>this.c.reload()).TEST_ID('Reload'),Text(this.c.error).TEST_"
             "ID("
             "'SmirkyCard.Status'))}});";
  }

  std::string constReloadCardSource(const char *title)
  {
    return std::string("const T='") + title
           + "';card('first',class{constructor(c){this.c=c;}compose(){return "
             "VStack(Text(T).TEST_ID('SmirkyCard.Title'),"
             "Button('r',()=>this.c.reload()).TEST_ID('Reload'),Text(this.c.error).TEST_ID('SmirkyCard.Status'))}});";
  }

  void printCurrentEngineMemory(smirkycard::ScriptRuntime &runtime)
  {
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(runtime.currentEngine()->jsRuntime(), &usage);
    std::printf("SmirkyCard QuickJS current after reload: malloc_size=%lld memory_used=%lld\n",
                static_cast<long long>(usage.malloc_size),
                static_cast<long long>(usage.memory_used_size));
  }

  std::string mountedTitle(smirkycard::ScriptRuntime &runtime, NullPlatformContext &context)
  {
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *title =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
    if (title && title->asAttributedTextNode())
    {
      const loka::app::AttributedString &text = title->asAttributedTextNode()->props.text_->get();
      LOKA_VERIFY(text.segmentCount() == 1);
      const loka::core::StringBuffer buffer = text.segment(0).text.bufferWithEncoding(loka::core::StringEncodingUtf8);
      return std::string(static_cast<const char *>(buffer.data()), buffer.length());
    }
    return title && title->asTextNode() ? textValue(title->asTextNode()) : std::string();
  }

  std::string mountedStatus(smirkycard::ScriptRuntime &runtime, NullPlatformContext &context, SmirkyCardId card)
  {
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(card, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *status =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Status");
    return status && status->asTextNode() ? textValue(status->asTextNode()) : std::string();
  }

  void printScratchMemory()
  {
    smirkycard::ScriptRuntime scratch;
    loka::core::String error;
    LOKA_VERIFY(scratch.loadBuiltin(smirkycard::BuiltinMainJs(), error));
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(scratch.jsRuntime(), &usage);
    std::printf("SmirkyCard QuickJS scratch after built-in: malloc_size=%lld memory_used=%lld\n",
                static_cast<long long>(usage.malloc_size),
                static_cast<long long>(usage.memory_used_size));
  }

  void checkMainJsLoad()
  {
    printScratchMemory();
    const char *directory = "_smirkycard_main_fixture";
    const char *path = "_smirkycard_main_fixture/MAIN.JS";
    std::remove(path);
    removeDirectory(directory);
    LOKA_VERIFY(makeDirectory(directory));
    NullPlatformContext context;
    context.setApplicationDirectory(loka::core::String::Literal(directory));

    smirkycard::ScriptRuntime missing;
    missing.loadMain(&context);
    LOKA_VERIFY(missing.mainSource() == smirkycard::ScriptRuntime::MAIN_SOURCE_BUILTIN);
    LOKA_VERIFY(mountedTitle(missing, context) == "Card One");

    writeMain(path, reloadCardSource("Loaded First"));
    smirkycard::ScriptRuntime loaded;
    loaded.loadMain(&context);
    LOKA_VERIFY(loaded.mainSource() == smirkycard::ScriptRuntime::MAIN_SOURCE_FILE);
    LOKA_VERIFY(mountedTitle(loaded, context) == "Loaded First");

    // The native button takes c.reload() through its card context, admits the
    // evaluated candidate engine, and then admits a replacement Scene.
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, loaded));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Scene *before = window.scene();
      smirkycard::JsEngine *beforeEngine = loaded.currentEngine();
      writeMain(path, reloadCardSource("Reloaded First"));
      loka::app::scene::Node *reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*before), "Reload");
      LOKA_VERIFY(reload && reload->asButtonNode());
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == before);
      LOKA_VERIFY(loaded.currentEngine() != beforeEngine);
      LOKA_VERIFY(loaded.retiredEngineCount() == 1);
      admission.flush();
      LOKA_VERIFY(window.scene() != before);
      // The outgoing card may be reclaimed by this admission's unmount path;
      // in either case no generation survives beyond the following flush.
      LOKA_VERIFY(loaded.retiredEngineCount() <= 1);
      loka::app::scene::Node *title =
          find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asTextNode() && textValue(title->asTextNode()) == "Reloaded First");
      printCurrentEngineMemory(loaded);
      admission.flush();
      LOKA_VERIFY(loaded.retiredEngineCount() == 0);

      // Each reload evaluates once in a fresh global scope, so a top-level
      // lexical declaration can be reloaded repeatedly without redeclaration.
      writeMain(path, constReloadCardSource("Const Reloaded"));
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      admission.flush();
      admission.flush();
      title = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asTextNode() && textValue(title->asTextNode()) == "Const Reloaded");
      writeMain(path, constReloadCardSource("Const Reloaded"));
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      admission.flush();
      admission.flush();
      title = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asTextNode() && textValue(title->asTextNode()) == "Const Reloaded");

      // Evaluation alone is not admission: the requested card must exist in
      // the candidate, or both the Scene and current engine stay unchanged.
      writeMain(path, "card('second',class{constructor(c){this.c=c;}compose(){return VStack()}});");
      loka::app::scene::Scene *stable = window.scene();
      smirkycard::JsEngine *stableEngine = loaded.currentEngine();
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == stable);
      LOKA_VERIFY(loaded.currentEngine() == stableEngine);
      loka::app::scene::Node *status =
          find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode()
                  && textValue(status->asTextNode()).find("MAIN.JS: card 'first' is not defined") != std::string::npos);

      // A syntax error leaves the mounted card in place and writes its error
      // seat, rather than admitting an incomplete replacement.
      writeMain(path, "(");
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == stable);
      status = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode()
                  && textValue(status->asTextNode()).find("MAIN.JS:") != std::string::npos);

      // Reload evaluation is nested inside the old engine's button call, but
      // the candidate engine still receives the shared interrupt budget.
      writeMain(path, "for(;;){}");
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == stable);
      status = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode()
                  && textValue(status->asTextNode()).find("interrupted") != std::string::npos);

      // A throwing candidate can mutate its scratch context, but never the
      // live one.
      writeMain(path, "globalThis.touched=1;throw new Error('reload broken');");
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == stable);
      loka::core::String result, error;
      LOKA_VERIFY(loaded.evaluateToString(loka::core::String::Literal("String(globalThis.touched)"), result, error));
      LOKA_VERIFY(result.compare(loka::core::String::Literal("undefined")) == 0);

      LOKA_VERIFY(std::remove(path) == 0);
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      LOKA_VERIFY(window.scene() == stable);
      status = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode()
                  && textValue(status->asTextNode()).find("file is missing") != std::string::npos);

      // A registered card may still refuse while being constructed. Its
      // refusal tree retains a native reload door so editing the file recovers.
      writeMain(path,
                "card('first',class{constructor(c){this.c=c;throw new Error('constructor broken')}compose(){return "
                "VStack()}});");
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*stable), "Reload");
      reload->asButtonNode()->props.getOnClick()->emit();
      admission.flush();
      loka::app::scene::Scene *refusal = window.scene();
      LOKA_VERIFY(refusal != stable);
      status = find(loka::dsl::testing::SceneTestAccess::rootNode(*refusal), "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode()
                  && textValue(status->asTextNode()).find("Card constructor failed") != std::string::npos);
      admission.flush();
      writeMain(path, reloadCardSource("Recovered First"));
      reload = find(loka::dsl::testing::SceneTestAccess::rootNode(*refusal), "SmirkyCard.Reload");
      LOKA_VERIFY(reload && reload->asButtonNode());
      reload->asButtonNode()->props.getOnClick()->emit();
      admission.flush();
      title = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asTextNode() && textValue(title->asTextNode()) == "Recovered First");
      admission.flush();
      LOKA_VERIFY(loaded.retiredEngineCount() == 0);
    }

    // A built-in card can reload a file which appears after startup.
    LOKA_VERIFY(std::remove(path) == 0);
    smirkycard::ScriptRuntime builtin;
    builtin.loadMain(&context);
    LOKA_VERIFY(builtin.mainSource() == smirkycard::ScriptRuntime::MAIN_SOURCE_BUILTIN);
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, builtin));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      writeMain(path, reloadCardSource("Appeared First"));
      loka::app::scene::Node *reload =
          find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Reload");
      LOKA_VERIFY(reload && reload->asButtonNode());
      reload->asButtonNode()->props.getOnClick()->emit();
      admission.flush();
      loka::app::scene::Node *title =
          find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Title");
      LOKA_VERIFY(title && title->asTextNode() && textValue(title->asTextNode()) == "Appeared First");
    }

    writeMain(path, "(");
    smirkycard::ScriptRuntime broken;
    broken.loadMain(&context);
    LOKA_VERIFY(broken.mainSource() == smirkycard::ScriptRuntime::MAIN_SOURCE_BUILTIN);
    LOKA_VERIFY(mountedTitle(broken, context) == "Card One");
    LOKA_VERIFY(mountedStatus(broken, context, SMIRKY_CARD_FIRST).find("MAIN.JS:") != std::string::npos);
    LOKA_VERIFY(mountedStatus(broken, context, SMIRKY_CARD_SECOND).find("MAIN.JS:") != std::string::npos);

    // A script that poisons a global before throwing must not reach the
    // fallback cards: the context is rebuilt before the built-in text runs.
    writeMain(path,
              "globalThis.Text = null; card('first', class{constructor(c){this.c=c;} compose() { return VStack(); } "
              "}); throw new "
              "Error('poison');");
    smirkycard::ScriptRuntime poisoned;
    poisoned.loadMain(&context);
    LOKA_VERIFY(poisoned.mainSource() == smirkycard::ScriptRuntime::MAIN_SOURCE_BUILTIN);
    LOKA_VERIFY(mountedTitle(poisoned, context) == "Card One");
    LOKA_VERIFY(mountedStatus(poisoned, context, SMIRKY_CARD_SECOND).find("poison") != std::string::npos);

    writeMain(path, std::string(64u * 1024u + 1u, 'x'));
    smirkycard::ScriptRuntime tooLarge;
    tooLarge.loadMain(&context);
    LOKA_VERIFY(mountedTitle(tooLarge, context) == "Card One");
    LOKA_VERIFY(mountedStatus(tooLarge, context, SMIRKY_CARD_FIRST).find("64 KiB") != std::string::npos);
    LOKA_VERIFY(mountedStatus(tooLarge, context, SMIRKY_CARD_SECOND).empty());
    LOKA_VERIFY(std::remove(path) == 0);
    LOKA_VERIFY(removeDirectory(directory));
  }

  std::string siblingCardSource(const char *title, const char *target)
  {
    return std::string("card('first',class{constructor(c){this.c=c;}compose(){return VStack(Text('") + title
           + "').TEST_ID('SmirkyCard.Title'),Button('open',()=>this.c.open('" + target
           + "')).TEST_ID('Open'),Button('reload',()=>this.c.reload()).TEST_ID('Reload'),"
             "Text(this.c.error).TEST_ID('SmirkyCard.Status'))}});";
  }

  loka::app::scene::Node *windowNode(NullWindow &window, const char *id)
  {
    return find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), id);
  }

  void clickCardButton(NullWindow &window, const char *id)
  {
    loka::app::scene::Node *button = windowNode(window, id);
    LOKA_VERIFY(button && button->asButtonNode());
    button->asButtonNode()->props.getOnClick()->emit();
  }

  std::string readCardSource(const char *name)
  {
    const std::string path = std::string(SMIRKYCARD_SOURCE_DIR) + "/" + name;
    std::FILE *file = loka::platform::file::OpenRead(loka::core::String::Utf8(path.data(), path.size()));
    LOKA_VERIFY(file != 0);
    std::string source;
    char buffer[4096];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
      source.append(buffer, count);
    LOKA_VERIFY(!std::ferror(file));
    LOKA_VERIFY(std::fclose(file) == 0);
    return source;
  }

  loka::app::CellNode *mineCell(NullWindow &window, int index)
  {
    char id[32];
    std::sprintf(id, "Mines.Cell.%d", index);
    loka::app::scene::Node *node = windowNode(window, id);
    LOKA_VERIFY(node && node->asCellNode());
    return node->asCellNode();
  }

  std::string mineLabel(NullWindow &window, int index)
  {
    const loka::core::StringBuffer buffer =
        mineCell(window, index)->props.text_->get().bufferWithEncoding(loka::core::StringEncodingUtf8);
    return std::string(static_cast<const char *>(buffer.data()), buffer.length());
  }

  void clickMine(NullWindow &window, WindowAdmissionTestApp &admission, int index)
  {
    mineCell(window, index)->props.onClick_->emit();
    admission.flush();
  }

  std::string minesStatus(NullWindow &window)
  {
    loka::app::scene::Node *node = windowNode(window, "Mines.Status");
    LOKA_VERIFY(node && node->asTextNode());
    return textValue(node->asTextNode());
  }

  std::string mineSnapshot(NullWindow &window)
  {
    std::string result = minesStatus(window);
    for (int i = 0; i < 64; ++i)
      result += "|" + mineLabel(window, i);
    return result;
  }

  void checkMines()
  {
    const char *directory = "_smirkycard_mines_fixture";
    const char *mainPath = "_smirkycard_mines_fixture/MAIN.JS";
    const char *minesPath = "_smirkycard_mines_fixture/MINES.JS";
    std::remove(mainPath);
    std::remove(minesPath);
    removeDirectory(directory);
    LOKA_VERIFY(makeDirectory(directory));
    writeMain(mainPath, readCardSource("MAIN.JS"));
    writeMain(minesPath, "globalThis.SMIRKY_SEED = 0x13579BDF;\n" + readCardSource("MINES.JS"));
    NullPlatformContext context;
    context.setApplicationDirectory(loka::core::String::Literal(directory));
    smirkycard::ScriptRuntime runtime;
    runtime.loadMain(&context);
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      clickCardButton(window, "SmirkyCard.OpenMines");
      admission.flush();
      const int mines[3][10] = {{3, 4, 20, 22, 37, 45, 50, 55, 56, 59},
                                {1, 6, 16, 23, 27, 30, 34, 44, 45, 53},
                                {3, 26, 42, 49, 50, 56, 59, 60, 62, 63}};
      for (int board = 0; board < 3; ++board)
      {
        loka::app::scene::Node *content = windowNode(window, "Mines.Content");
        loka::app::scene::Node *grid = windowNode(window, "Mines.Board");
        LOKA_VERIFY(content && content->asStackNode());
        LOKA_VERIFY(content->asStackNode()->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN);
        LOKA_VERIFY(content->asNestable()->childrenCount() == 4);
        loka::app::scene::Node *status = content->asNestable()->childrenHead();
        LOKA_VERIFY(status == windowNode(window, "Mines.Status"));
        loka::app::scene::Node *error = status->nextInComposition;
        LOKA_VERIFY(error && error == windowNode(window, "SmirkyCard.Status") && error->asTextNode());
        LOKA_VERIFY(textValue(error->asTextNode()).empty());
        loka::app::scene::Node *controls = error->nextInComposition;
        LOKA_VERIFY(controls && controls->asStackNode());
        LOKA_VERIFY(controls->asStackNode()->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
        LOKA_VERIFY(controls->asNestable()->childrenCount() == 3);
        LOKA_VERIFY(find(controls, "Mines.Flag") && find(controls, "Mines.NewGame")
                    && find(controls, "SmirkyCard.OpenMain"));
        LOKA_VERIFY(grid && grid->asGridNode() && controls->nextInComposition == grid);
        LOKA_VERIFY(!grid->nextInComposition);
        LOKA_VERIFY(grid->asGridNode()->props.rows == 8 && grid->asGridNode()->props.cols == 8);
        LOKA_VERIFY(grid->asNestable()->childrenCount() == 64);
        LOKA_VERIFY(minesStatus(window) == "Mines left: 10");
        for (int i = 0; i < 64; ++i)
          LOKA_VERIFY(mineLabel(window, i) == ".");
        clickMine(window, admission, mines[board][0]);
        LOKA_VERIFY(minesStatus(window) == "Boom");
        for (int i = 0; i < 64; ++i)
        {
          bool expectedMine = false;
          for (int j = 0; j < 10; ++j)
            expectedMine = expectedMine || mines[board][j] == i;
          LOKA_VERIFY(mineLabel(window, i) == (expectedMine ? "X" : "."));
        }
        const std::string lost = mineSnapshot(window);
        for (int i = 0; i < 64; ++i)
          clickMine(window, admission, i);
        LOKA_VERIFY(mineSnapshot(window) == lost);
        if (board < 2)
        {
          clickCardButton(window, "Mines.NewGame");
          admission.flush();
        }
      }
      // Reopening the script restarts its seeded stream, unlike New Game.
      clickCardButton(window, "SmirkyCard.OpenMain");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "SmirkyCard.Title")->asAttributedTextNode()->props.text_->get()
                  == loka::app::Styled("Card One", loka::app::FontSize<18>() + loka::app::Bold));
      clickCardButton(window, "SmirkyCard.OpenMines");
      admission.flush();
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "Mines.Flag")
                      ->asButtonNode()
                      ->props.getText()
                      ->get()
                      .compare(loka::core::String::Literal("Flag mode: on"))
                  == 0);
      clickMine(window, admission, 0);
      LOKA_VERIFY(mineLabel(window, 0) == "F" && minesStatus(window) == "Mines left: 9");
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      const std::string flagged = mineSnapshot(window);
      clickMine(window, admission, 0);
      LOKA_VERIFY(mineSnapshot(window) == flagged);
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      clickMine(window, admission, 0);
      LOKA_VERIFY(mineLabel(window, 0) == "." && minesStatus(window) == "Mines left: 10");
      // A flag inside the zero region must survive flood reveal.
      clickMine(window, admission, 8);
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      clickMine(window, admission, 2);
      LOKA_VERIFY(mineLabel(window, 2) == "1");
      clickMine(window, admission, 0);
      LOKA_VERIFY(mineLabel(window, 0).empty());
      LOKA_VERIFY(mineLabel(window, 1).empty());
      LOKA_VERIFY(mineLabel(window, 16).empty());
      LOKA_VERIFY(mineLabel(window, 19) == "1");
      LOKA_VERIFY(mineLabel(window, 8) == "F");
      LOKA_VERIFY(mineLabel(window, 7) == "." && mineLabel(window, 63) == ".");
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      const std::string revealed = mineSnapshot(window);
      clickMine(window, admission, 0);
      LOKA_VERIFY(mineSnapshot(window) == revealed);
      clickMine(window, admission, 8);
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "Mines.Flag")
                      ->asButtonNode()
                      ->props.getText()
                      ->get()
                      .compare(loka::core::String::Literal("Flag mode: off"))
                  == 0);
      for (int i = 0; i < 64; ++i)
      {
        bool isMine = false;
        for (int j = 0; j < 10; ++j)
          isMine = isMine || mines[0][j] == i;
        if (!isMine)
          clickMine(window, admission, i);
      }
      LOKA_VERIFY(minesStatus(window) == "You win");
      for (int i = 0; i < 64; ++i)
      {
        bool isMine = false;
        int adjacent = 0;
        for (int j = 0; j < 10; ++j)
        {
          isMine = isMine || mines[0][j] == i;
          const int dy = std::abs(mines[0][j] / 8 - i / 8);
          const int dx = std::abs(mines[0][j] % 8 - i % 8);
          if (dy <= 1 && dx <= 1 && (dx || dy))
            ++adjacent;
        }
        const std::string expected = isMine ? "." : adjacent ? std::string(1, '0' + adjacent) : "";
        LOKA_VERIFY(mineLabel(window, i) == expected);
      }
      const std::string won = mineSnapshot(window);
      for (int i = 0; i < 64; ++i)
        clickMine(window, admission, i);
      LOKA_VERIFY(mineSnapshot(window) == won);
      clickCardButton(window, "Mines.Flag");
      admission.flush();
      clickMine(window, admission, 3);
      LOKA_VERIFY(mineSnapshot(window) == won);
      // A failed open stays on the Mines card and names the file in its status.
      LOKA_VERIFY(std::remove(mainPath) == 0);
      clickCardButton(window, "SmirkyCard.OpenMain");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "Mines.Board") != 0);
      const std::string failure = textValue(windowNode(window, "SmirkyCard.Status")->asTextNode());
      LOKA_VERIFY(failure.find("MAIN.JS") != std::string::npos);
    }
    LOKA_VERIFY(std::remove(minesPath) == 0);
    LOKA_VERIFY(removeDirectory(directory));
  }

  void checkOpenSibling()
  {
    const char *directory = "_smirkycard_sibling_fixture";
    const char *mainPath = "_smirkycard_sibling_fixture/MAIN.JS";
    const char *minesPath = "_smirkycard_sibling_fixture/MINES.JS";
    std::remove(mainPath);
    std::remove(minesPath);
    removeDirectory(directory);
    LOKA_VERIFY(makeDirectory(directory));
    NullPlatformContext context;
    context.setApplicationDirectory(loka::core::String::Literal(directory));
    writeMain(mainPath, siblingCardSource("Main", "./MINES.JS"));
    writeMain(minesPath, siblingCardSource("Mines", "./MAIN.JS"));
    smirkycard::ScriptRuntime runtime;
    runtime.loadMain(&context);
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Scene *before = window.scene();
      smirkycard::JsEngine *engine = runtime.currentEngine();
      clickCardButton(window, "Open");
      LOKA_VERIFY(runtime.currentEngine() != engine);
      LOKA_VERIFY(window.scene() == before);
      LOKA_VERIFY(runtime.retiredEngineCount() == 1);
      admission.flush();
      LOKA_VERIFY(window.scene() != before);
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Title")->asTextNode()) == "Mines");
      admission.flush();
      LOKA_VERIFY(runtime.retiredEngineCount() == 0);

      writeMain(minesPath, siblingCardSource("Edited Mines", "./MAIN.JS"));
      clickCardButton(window, "Reload");
      admission.flush();
      admission.flush();
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Title")->asTextNode()) == "Edited Mines");
      writeMain(mainPath, siblingCardSource("Reopened Main", "MINES.JS"));
      clickCardButton(window, "Open");
      admission.flush();
      admission.flush();
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Title")->asTextNode()) == "Reopened Main");
      writeMain(mainPath, siblingCardSource("Reloaded Main", "MINES.JS"));
      clickCardButton(window, "Reload");
      admission.flush();
      admission.flush();
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Title")->asTextNode()) == "Reloaded Main");
      clickCardButton(window, "Open");
      admission.flush();
      admission.flush();
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Title")->asTextNode()) == "Edited Mines");
      clickCardButton(window, "Open");
      admission.flush();
      admission.flush();

      const std::string failures[] = {
          "(", "card('second',class{constructor(c){this.c=c;}});", "for(;;){}", std::string(64u * 1024u + 1u, 'x')};
      const char *reasons[] = {"SyntaxError", "'first'", "interrupted", "64 KiB"};
      for (size_t i = 0; i <= sizeof(failures) / sizeof(failures[0]); ++i)
      {
        if (i < sizeof(failures) / sizeof(failures[0]))
          writeMain(minesPath, failures[i]);
        else
          LOKA_VERIFY(std::remove(minesPath) == 0);
        before = window.scene();
        engine = runtime.currentEngine();
        clickCardButton(window, "Open");
        LOKA_VERIFY(window.scene() == before && runtime.currentEngine() == engine);
        LOKA_VERIFY(!window.sceneManager()->hasPendingReplacement());
        const std::string status = textValue(windowNode(window, "SmirkyCard.Status")->asTextNode());
        LOKA_VERIFY(status.find("MINES.JS:") != std::string::npos);
        LOKA_VERIFY(status.find(i < sizeof(failures) / sizeof(failures[0]) ? reasons[i] : "missing")
                    != std::string::npos);
        LOKA_VERIFY(runtime.retiredEngineCount() == 0);
      }
    }
    // Each path refusal gets its own live card and exercises the actual JS door.
    const char *names[] = {
        "sub/MINES.JS", "../MINES.JS", "././MINES.JS", "MINES.JS\\u0000suffix", "MINES.JS:bad", "sub\\\\MINES.JS"};
    const char *displayNames[] = {
        "sub/MINES.JS", "../MINES.JS", "./MINES.JS", "MINES.JS", "MINES.JS:bad", "sub\\MINES.JS"};
    writeMain(minesPath, siblingCardSource("Must not open", "MAIN.JS"));
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
      writeMain(mainPath, siblingCardSource("Stable", names[i]));
      smirkycard::ScriptRuntime refused;
      refused.loadMain(&context);
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, refused));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Scene *before = window.scene();
      smirkycard::JsEngine *engine = refused.currentEngine();
      clickCardButton(window, "Open");
      LOKA_VERIFY(window.scene() == before && refused.currentEngine() == engine);
      LOKA_VERIFY(!window.sceneManager()->hasPendingReplacement());
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Status")->asTextNode()).find(displayNames[i])
                  != std::string::npos);
      admission.flush();
      LOKA_VERIFY(window.scene() == before && refused.currentEngine() == engine);
    }
    LOKA_VERIFY(std::remove(mainPath) == 0);
    LOKA_VERIFY(std::remove(minesPath) == 0);
    LOKA_VERIFY(removeDirectory(directory));
  }

  void printMemory(const char *phase, smirkycard::ScriptRuntime &runtime)
  {
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(runtime.jsRuntime(), &usage);
    std::printf("SmirkyCard QuickJS %s: malloc_size=%lld memory_used=%lld\n",
                phase,
                static_cast<long long>(usage.malloc_size),
                static_cast<long long>(usage.memory_used_size));
  }

  void checkCounterAndRefusals()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(
        runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;this.count=this.c.state(0)}compose(){return "
                            "VStack(Button('+1',()=>{this.count.set(this.count.get()+1)}).TEST_ID('Counter."
                            "Increment'),Text(this.count).TEST_ID('Counter.Count'))}});",
                            error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    printMemory("after compose", runtime);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::ButtonNode *increment = find(root, "Counter.Increment")->asButtonNode();
    loka::app::TextNode *count = find(root, "Counter.Count")->asTextNode();
    LOKA_VERIFY(increment && count);
    increment->props.getOnClick()->emit();
    increment->props.getOnClick()->emit();
    LOKA_VERIFY(textValue(count) == "2");

    NullPlatformContext secondContext;
    NullScenePlatformController secondPlatform;
    WindowProps secondProps;
    secondProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow secondWindow(&secondContext, secondProps, &secondPlatform);
    WindowAdmissionTestApp secondAdmission(secondWindow);
    loka::dsl::testing::SceneTestAccess::updateAttached(*secondWindow.scene(), true);
    printMemory("with two cards alive", runtime);
  }
  void checkManyClickables(const char *kind, int count = 70)
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    char length[16];
    std::sprintf(length, "%d", count);
    const std::string source = std::string("card('first',class{constructor(c){this.c=c;this.s=Array.from({length:")
                               + length
                               + "},(_,i)=>this.c.state('seat'+i))}"
                                 "compose(){return VStack(Array.from({length:Math.ceil(this.s.length/10)},(_,r)=>Row("
                                 "this.s.slice(r*10,r*10+10).map((s,j)=>"
                               + kind + "(s,()=>s.set('clicked'+(r*10+j))).TEST_ID('Click'+(r*10+j))))))}});";
    LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::scene::Node *status = find(root, "SmirkyCard.Status");
    if (status)
      std::fprintf(stderr, "large card refusal: %s\n", textValue(status->asTextNode()).c_str());
    const int indices[] = {0, 8, count - 1};
    for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i)
    {
      char id[32];
      std::sprintf(id, "Click%d", indices[i]);
      loka::app::scene::Node *node = find(root, id);
      LOKA_VERIFY(node);
      if (node->asButtonNode())
        node->asButtonNode()->props.getOnClick()->emit();
      else
        node->asCellNode()->props.onClick_->emit();
    }
    for (int i = 0; i < count; ++i)
    {
      char id[32], expected[32];
      std::sprintf(id, "Click%d", i);
      std::sprintf(expected, i == 0 || i == 8 || i == count - 1 ? "clicked%d" : "seat%d", i);
      loka::app::scene::Node *node = find(root, id);
      const loka::core::String value =
          node->asButtonNode() ? node->asButtonNode()->props.getText()->get() : node->asCellNode()->props.text_->get();
      LOKA_VERIFY(value.compare(loka::core::String::Utf8(expected, std::strlen(expected))) == 0);
    }
  }

  void checkClickableLifetime()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error, result;
    LOKA_VERIFY(runtime.loadBuiltin(
        "globalThis.clicks=0;card('first',class{constructor(c){this.c=c;}compose(){return VStack("
        "Button('go',()=>{++clicks;this.c.go('second')}).TEST_ID('Go'),"
        "Cell('stay',()=>{++clicks}).TEST_ID('Stay'))}});"
        "card('second',class{constructor(c){this.c=c;}compose(){return Text('second').TEST_ID('Second')}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::core::EmitterState *go = find(root, "Go")->asButtonNode()->props.getOnClick();
    loka::core::EmitterState *stay = find(root, "Stay")->asCellNode()->props.onClick_;
    stay->emit();
    go->emit();
    // Exercise withdrawal while storage is live: scene replacement's first
    // flush reclaims the root and its component arena, not just native contexts.
    loka::dsl::testing::SceneTestAccess::notifyComposeEvent(*window.scene(), loka::app::scene::COMPOSE_EVENT_DETACH);
    stay->emit();
    go->emit();
    LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Literal("clicks"), result, error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("2")) == 0);
    admission.flush();
    LOKA_VERIFY(window.sceneManager()->hasRetiredScenes());
    LOKA_VERIFY(find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Second"));
    admission.flush();
    LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
  }

  void printRecordCosts()
  {
    const size_t seat = sizeof(smirkycard::JsSeatRecord);
    const size_t handler = sizeof(smirkycard::JsHandlerRecord);
    const size_t child = sizeof(smirkycard::JsClickNode);
    std::printf("SmirkyCard host costs: seat=%lu handler=%lu child=%lu 67/66=%lu bytes\n",
                static_cast<unsigned long>(seat),
                static_cast<unsigned long>(handler),
                static_cast<unsigned long>(child),
                static_cast<unsigned long>(67 * seat + 66 * (handler + child)));
  }

  void checkEnabledSeat()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(
        runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;this.on=this.c.state(true)}compose(){return "
                            "VStack(Button('flip',()=>this.on.set(!this.on.get())).TEST_ID('Flip'),Button('"
                            "target',()=>{}).TEST_ID('Target').enabled(this.on),"
                            "Button('reverse',()=>{}).enabled(this.on).TEST_ID('Reverse'))}});",
                            error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::scene::Node *targetNode = find(root, "Target");
    LOKA_VERIFY(targetNode);
    loka::app::ButtonNode *target = targetNode->asButtonNode();
    LOKA_VERIFY(target && target->props.getEnabled() && target->props.getEnabled()->get());
    loka::app::scene::Node *reverseNode = find(root, "Reverse");
    LOKA_VERIFY(reverseNode && reverseNode->asButtonNode());
    loka::app::ButtonNode *reverse = reverseNode->asButtonNode();
    LOKA_VERIFY(reverse->props.getEnabled() && reverse->props.getEnabled()->get());
    find(root, "Flip")->asButtonNode()->props.getOnClick()->emit();
    LOKA_VERIFY(!target->props.getEnabled()->get());
    LOKA_VERIFY(!reverse->props.getEnabled()->get());
  }
  void checkArrayChildren()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;}compose(){return "
                                    "VStack(['one','two','three'].map(v=>Text(v))).TEST_ID('Mapped')}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::INestable *mapped =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Mapped")->asNestable();
    int count = 0;
    for (loka::app::scene::Node *child = mapped->childrenHead(); child; child = child->nextInComposition)
      ++count;
    LOKA_VERIFY(count == 3);
  }
  void checkTreePropertyCopy()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String result, error;
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal(
            "(()=>{const tag=Text('').TEST_ID;const sym=Symbol('own');"
            "const source=Object.create({inherited:1});source.extra=undefined;source[sym]=7;"
            "Object.defineProperty(source,'hidden',{value:3});"
            "Object.defineProperty(source,'__proto__',{value:9,enumerable:true});"
            "const copy=tag.call(source,'t');"
            "return Object.hasOwn(copy,'extra') && copy.extra===undefined && copy[sym]===7 && "
            "!Object.hasOwn(copy,'inherited') && !Object.hasOwn(copy,'hidden') && "
            "Object.hasOwn(copy,'__proto__') && copy.__proto__===9 && "
            "copy.testId==='t' && Object.isFrozen(copy) && copy.TEST_ID('u').testId==='u';})()"),
        result,
        error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("true")) == 0);
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal(
            "(()=>{try{Text('').TEST_ID.call({get extra(){throw new Error('copy getter')}},'t')}"
            "catch(e){return e.message==='copy getter'}return false})()"),
        result,
        error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("true")) == 0);
    // Invalid style must throw at the helper call, even when the tree is never lowered.
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal(
            "(()=>{try{Text('a',{colour:1})}catch(e){return e instanceof TypeError}return false})()"),
        result,
        error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("true")) == 0);
    LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Literal("typeof Text('a').enabled"), result, error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("undefined")) == 0);
  }

  void checkTextStyle()
  {
    using namespace loka::app;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "card('first',class{constructor(c){this.c=c;this.text=this.c.state('live')}compose(){return VStack("
        "Text('a',{size:24,weight:'bold',italic:true,family:'monospace'}).TEST_ID('Styled'),"
        "Text('a',{size:13}).TEST_ID('Snap13'),Text('a',{size:16}).TEST_ID('Snap16'),"
        "Text('a',{size:21}).TEST_ID('Snap21'),Text('a',{size:18}).TEST_ID('Exact18'),"
        "Text('a',{size:24}).TEST_ID('Chained'),"
        "Text('a',{size:'smallLabel'}).TEST_ID('SmallLabel'),"
        "Text('a',{weight:'normal',italic:false,family:'system'}).TEST_ID('Normal'),"
        "Text('a').TEST_ID('Plain'),Text(this.text,{size:18}).TEST_ID('Live'),"
        "Text(this.c.error,{italic:true}).TEST_ID('Error'))}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    const char *ids[] = {
        "Chained", "Styled", "Snap13", "Snap16", "Snap21", "Exact18", "Normal", "Plain", "Live", "Error", "SmallLabel"};
    const TextStyle styles[] = {FontSize<24>(),
                                FontSize<24>() + Bold + Italic + Monospace,
                                FontSize<12>(),
                                FontSize<14>(),
                                FontSize<18>(),
                                FontSize<18>(),
                                TextStyle().weight(TEXT_WEIGHT_NORMAL).italic(false).family(TEXT_FAMILY_SYSTEM),
                                TextStyle(),
                                FontSize<18>(),
                                Italic,
                                SmallLabel};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
      loka::app::scene::Node *node = find(root, ids[i]);
      LOKA_VERIFY(node && node->asTextNode());
      LOKA_VERIFY(node->asTextNode()->props.textStyle_ == styles[i]);
    }
    // #814: a Text with no style fields must not count as declared, or the
    // rails configure their native label and the goldens drift.
    loka::app::scene::Node *plain = find(root, "Plain");
    LOKA_VERIFY(plain && plain->asTextNode() && !plain->asTextNode()->props.hasDeclaredLayoutStyle());
  }

  void checkTextColors()
  {
    using namespace loka::app;
    for (size_t i = 0; i < sizeof(detail::StyleVocabularyColors) / sizeof(detail::StyleVocabularyColors[0]); ++i)
    {
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      const std::string source = std::string("card('first',class{compose(){return Text('a',{color:'")
                                 + detail::StyleVocabularyColors[i].name + "',weight:'bold'}).TEST_ID('Color')}});";
      LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
      NullPlatformContext context;
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Node *node = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Color");
      LOKA_VERIFY(node && node->asTextNode());
      const TextStyle style = node->asTextNode()->props.resolvedTextStyle();
      LOKA_VERIFY(style.color_ == detail::StyleVocabularyColors[i].value);
      LOKA_VERIFY(style.hasWeight_ && style.weight_ == TEXT_WEIGHT_BOLD);
    }
  }

  void checkLifecycleHooks()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error, result;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;this.value=this.c.state('new')}"
                                    "onAttach(){this.value.set('attached')}onDetach(){"
                                    "this.value.set('detached');globalThis.detached=this.value.get()}compose(){return "
                                    "VStack(Text(this.value).TEST_ID('HookValue'),Button('next',()=>this.c.go('second')"
                                    ").TEST_ID('HookNext'))}});card('"
                                    "second',class{constructor(c){this.c=c;}compose(){return VStack()}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    admission.flush();
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    LOKA_VERIFY(textValue(find(root, "HookValue")->asTextNode()) == "attached");
    find(root, "HookNext")->asButtonNode()->props.getOnClick()->emit();
    admission.flush();
    LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Literal("detached"), result, error));
    LOKA_VERIFY(result.compare(loka::core::String::Literal("detached")) == 0);
    smirkycard::ScriptRuntime throwing;
    LOKA_VERIFY(throwing.loadBuiltin(
        "card('first',class{constructor(c){this.c=c;}onAttach(){throw new Error('attach boom')}compose(){return "
        "VStack(Text(this.c.error).TEST_ID('SmirkyCard.Status'))}});",
        error));
    WindowProps throwingProps;
    throwingProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, throwing));
    NullWindow throwingWindow(&context, throwingProps, &platform);
    WindowAdmissionTestApp throwingAdmission(throwingWindow);
    loka::dsl::testing::SceneTestAccess::updateAttached(*throwingWindow.scene(), true);
    throwingAdmission.flush();
    // A throwing hook lands in the error seat; the card itself stays mounted.
    loka::app::scene::Node *status =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*throwingWindow.scene()), "SmirkyCard.Status");
    LOKA_VERIFY(status);
    LOKA_VERIFY(textValue(status->asTextNode()).find("attach boom") != std::string::npos);
  }

  void checkComposeRefusal(const char *source, const char *expected, bool consumesException = false)
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(source, error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    if (consumesException)
      LOKA_VERIFY(!JS_HasException(runtime.context()));
    loka::app::scene::Node *status =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Status");
    LOKA_VERIFY(!find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Prefix"));
    LOKA_VERIFY(!find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "M"));
    LOKA_VERIFY(status && status->asTextNode());
    LOKA_VERIFY(textValue(status->asTextNode()).find(expected) != std::string::npos);
    loka::app::scene::Node *reload =
        find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "SmirkyCard.Reload");
    LOKA_VERIFY(reload && reload->asButtonNode() && reload->asButtonNode()->props.getOnClick());
    // The card window has no outer margin, so the host's refusal tree pads itself.
    loka::app::scene::Node *inset = outermostBox(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()));
    LOKA_VERIFY(inset && inset->asBoxNode()->props.padding == loka::app::StandardWindowMargin()
                && find(inset, "SmirkyCard.Status") == status && find(inset, "SmirkyCard.Reload") == reload);
  }

  void checkGridShape(int rows, int cols, bool clickable)
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    char prefix[128];
    std::sprintf(
        prefix,
        "card('first',class{constructor(c){this.c=c;}compose(){return Grid(%d,%d,Array.from({length:%d},(_,i)=>",
        rows,
        cols,
        rows * cols);
    const std::string source = std::string(prefix) + (clickable ? "Cell(String(i),()=>{})" : "Text(String(i))")
                               + ".TEST_ID(String(i)))).TEST_ID('Board')}});";
    LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *board = windowNode(window, "Board");
    LOKA_VERIFY(board && board->asGridNode());
    loka::app::GridNode *grid = board->asGridNode();
    LOKA_VERIFY(grid->props.rows == rows && grid->props.cols == cols);
    LOKA_VERIFY(grid->childrenCount() == static_cast<size_t>(rows * cols));
    int count = 0;
    for (loka::app::scene::Node *child = grid->childrenHead(); child; child = child->nextInComposition)
    {
      char expected[16];
      std::sprintf(expected, "%d", count++);
      loka::app::scene::Node *cell = find(child, expected);
      LOKA_VERIFY(cell);
      if (clickable)
      {
        LOKA_VERIFY(cell->asCellNode());
        LOKA_VERIFY(cell->asCellNode()->props.text_->get().compare(loka::core::String::Literal(expected)) == 0);
      }
      else
      {
        LOKA_VERIFY(cell->asTextNode());
        LOKA_VERIFY(textValue(cell->asTextNode()) == expected);
      }
    }
    LOKA_VERIFY(count == rows * cols);
  }

  void checkGrid()
  {
    checkGridShape(8, 8, true);
    // Text nodes cover the capacity without consuming the separate clickable budget.
    checkGridShape(16, 16, false);
    checkGridShape(1, 1, true);
    checkGridShape(2, 3, true);
    smirkycard::ScriptRuntime runtime;
    loka::core::String result, error;
    // Validation belongs to the helper call even if its result is never lowered.
    LOKA_VERIFY(!runtime.evaluateToString(loka::core::String::Literal("Grid(0,8,[]);'unused'"), result, error));
    const char *stacks[] = {"Row", "VStack", "ZStack"};
    for (size_t i = 0; i < sizeof(stacks) / sizeof(stacks[0]); ++i)
    {
      const std::string source = std::string("card('first',class{constructor(c){this.c=c;}compose(){return ")
                                 + stacks[i] + "(Array.from({length:17},()=>Text('x')))}});";
      checkComposeRefusal(source.c_str(), "accepts at most 16 children");
    }
    const char *badCounts[] = {"63", "65"};
    for (size_t i = 0; i < sizeof(badCounts) / sizeof(badCounts[0]); ++i)
    {
      const std::string source =
          std::string("card('first',class{constructor(c){this.c=c;}compose(){return Grid(8,8,Array.from({length:")
          + badCounts[i] + "},()=>Text('x')))}});";
      checkComposeRefusal(source.c_str(), "Grid requires exactly rows * cols children (64)");
    }
    const char *badDimensions[] = {"0", "17", "-1", "1.5", "NaN", "Infinity", "'8'", "null"};
    for (size_t i = 0; i < sizeof(badDimensions) / sizeof(badDimensions[0]); ++i)
      for (int axis = 0; axis < 2; ++axis)
      {
        const std::string source = std::string("card('first',class{constructor(c){this.c=c;}compose(){return Grid(")
                                   + (axis == 0 ? badDimensions[i] : "8") + "," + (axis == 1 ? badDimensions[i] : "8")
                                   + ",[])}});";
        checkComposeRefusal(source.c_str(), "Grid rows and cols must be integers in 1..16");
      }
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Grid(1,1,[[Text('x')]])}});",
                        "nested arrays");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Grid(1,1)}});",
                        "Grid(rows, cols, children)");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){const g=Grid(1,1,Text('x'));"
                        "g.children.pop();return g}});",
                        "Grid requires exactly rows * cols children");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){const g=Grid(1,1,Text('x'));"
                        "g.children.push(Text('y'));return g}});",
                        "Grid requires exactly rows * cols children");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return "
                        "Object.assign({},Grid(1,1,Text('x')),{rows:0})}});",
                        "Grid rows and cols must be integers in 1..16");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return "
                        "Object.assign({},Grid(1,1,Text('x')),{cols:17})}});",
                        "Grid rows and cols must be integers in 1..16");
  }

  void checkTextStyleRefusals()
  {
    const char *styles[] = {"{family:3}",
                            "{family:-1}",
                            "{family:258}",
                            "{family:2147483648}",
                            "{family:'count'}",
                            "{family:'unset'}",
                            "{family:'Monospace'}",
                            "{family:'monospace\\u0000'}",
                            "{family:''}",
                            "{colour:1}",
                            "{size:'big'}",
                            "{size:'SmallLabel'}",
                            "{size:'smallLabel\\u0000'}",
                            "{color:1}",
                            "{color:'purple'}",
                            "{color:'Red'}",
                            "{color:'unset'}",
                            "{color:''}",
                            "{color:'red\\u0000'}",
                            "{weight:1}",
                            "{weight:'heavy'}",
                            "{italic:1}",
                            "null",
                            "[]",
                            "new Date()",
                            "{size:13.5}",
                            "{size:NaN}",
                            "{size:Infinity}",
                            "{size:2147483648}",
                            "{[Symbol('x')]:1}",
                            "Object.defineProperty({},'colour',{value:1})",
                            "{italic:undefined}",
                            "{['size\\u0000']:24}",
                            "{weight:'bold\\u0000'}"};
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); ++i)
    {
      const std::string source =
          std::string("card('first',class{constructor(c){this.c=c;}compose(){return Text('a',") + styles[i] + ")}});";
      checkComposeRefusal(source.c_str(), "TypeError");
      if (!std::strncmp(styles[i], "{family:", 8) || !std::strncmp(styles[i], "{color:", 7)
          || !std::strncmp(styles[i], "{weight:", 8))
      {
        smirkycard::ScriptRuntime runtime;
        loka::core::String result, error;
        const std::string immediate =
            std::string("(()=>{try{Text('a',") + styles[i]
            + ")}catch(e){return e instanceof TypeError && e.message==='Invalid Text style value'}return false})()";
        LOKA_VERIFY(
            runtime.evaluateToString(loka::core::String::Utf8(immediate.c_str(), immediate.size()), result, error));
        LOKA_VERIFY(result.equals(loka::core::String::Literal("true")));
      }
    }
  }

  void checkChangedTextStyleRefusal()
  {
    // The tree owns a reference to the dictionary, so lowering must revalidate it.
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){const s={size:24};const t=Text('a',s);"
                        "s.colour=1;return t}});",
                        "TypeError");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Text('a',{get size(){"
                        "throw new Error('style getter')}})}});",
                        "style getter");
  }

  void checkMarkupParser()
  {
    using namespace loka::app;
    AttributedString result;
    const TextStyle base = FontSize<12>();
    const char *sample = "var <b>x</b> = <i>1</i>;";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result
                == Styled("var ", base) + Styled("x", base + Bold) + Styled(" = ", base) + Styled("1", base + Italic)
                       + Styled(";", base));
    sample = "<code><size=24><b>a</b></size>b</code>c";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result
                == Styled("a", Monospace + FontSize<24>() + Bold) + Styled("b", base + Monospace) + Styled("c", base));
    sample = "<size=24><b>a</b>b</size>";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result == Styled("a", FontSize<24>() + Bold) + Styled("b", FontSize<24>()));
    sample = "<size=24>a<size=12>b</size>c</size>d";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result
                == Styled("a", FontSize<24>()) + Styled("b", base) + Styled("c", FontSize<24>()) + Styled("d", base));
    sample = "\\<b>\\\\";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result == Styled("<b>\\", base));
    sample = "<size=21>x</size>";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result == Styled("x", FontSize<18>()));
    sample = "é<b>日本🙂</b>é";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result == Styled("é", base) + Styled("日本🙂", base + Bold) + Styled("é", base));
    sample = "<b></b><i></i>";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), base, result));
    LOKA_VERIFY(result.valid() && result.segmentCount() == 0);
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(0, 0, base, result));
    LOKA_VERIFY(result.valid() && result.empty());
    const char *bad[] = {"<code>x",
                         "<code>x</b>",
                         "</code>",
                         "<u>x</u>",
                         "<b>x",
                         "<b>x</i>",
                         "</b>",
                         "<size=>x</size>",
                         "<size=1x>x</size>",
                         "<size=2147483648>x</size>",
                         "<size=-1>x</size>",
                         "<size=1.5>x</size>",
                         "<size= 12>x</size>",
                         "<size=12",
                         "<>",
                         "</>",
                         "<b><i>x</b></i>",
                         "<size=12>x</size=12>"};
    const AttributedString sentinel = Styled("unchanged", base);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
      result = sentinel;
      LOKA_VERIFY(!smirkycard::ParseSmirkyMarkup(bad[i], std::strlen(bad[i]), base, result));
      LOKA_VERIFY(result == sentinel);
    }
    const char embedded[] = {'a', '\0', '<', 'b', '>', 'b', '<', '/', 'b', '>'};
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(embedded, sizeof(embedded), base, result));
    LOKA_VERIFY(result == Styled(loka::core::String::Utf8(embedded, 2), base) + Styled("b", base + Bold));
    std::string deep;
    for (int i = 0; i < 1024; ++i)
      deep += "<b>";
    deep += "x";
    for (int i = 0; i < 1024; ++i)
      deep += "</b>";
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(deep.data(), deep.size(), base, result));
    LOKA_VERIFY(result == Styled("x", base + Bold));
  }

  void checkMarkup()
  {
    using namespace loka::app;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error, value;
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal("(()=>{try{Markup('<b>x')}catch(e){return e instanceof TypeError}return false})()"),
        value,
        error));
    LOKA_VERIFY(value.compare(loka::core::String::Literal("true")) == 0);
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;}compose(){return VStack("
                                    "Markup('<b>Hi</b> there',{size:24}).TEST_ID('M'),"
                                    "Text('<b>plain</b>').TEST_ID('P'))}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::scene::Node *node = find(root, "M");
    LOKA_VERIFY(node && node->asAttributedTextNode());
    LOKA_VERIFY(node->asAttributedTextNode()->props.text_->get()
                == Styled("Hi", FontSize<24>() + Bold) + Styled(" there", FontSize<24>()));
    LOKA_VERIFY(textValue(find(root, "P")->asTextNode()) == "<b>plain</b>");
    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;this.s=this.c.state('x')}compose(){return Markup(this.s)}});",
        "state seats are not supported");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Markup('<b>x')}});", "TypeError");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Markup('x',{colour:1})}});",
                        "TypeError");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){const s={size:24};const t=Markup('x',s);"
                        "s.colour=1;return t}});",
                        "Markup",
                        true);
  }

  void checkBlockStyle()
  {
    using namespace loka::app;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error, value;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;this.s=this.c.state('s');this.n=this.c."
                                    "state(1)}compose(){return VStack("
                                    "Text('a',undefined,{align:'center'}).TEST_ID('T').TEST_ID('T'),"
                                    "Markup('<b>x</b>',{size:18},{align:'right',wrap:'word'}).TEST_ID('M'),"
                                    "Text(this.s,undefined,{wrap:'char',truncation:'clip'}).TEST_ID('S'),"
                                    "Text(this.n,undefined,{align:'left',truncation:'ellipsis'}).TEST_ID('N'),"
                                    "Text(this.c.error,undefined,{wrap:'none',truncation:'none'}).TEST_ID('E'),"
                                    "Text('default',undefined,undefined).TEST_ID('D'))}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    const char *ids[] = {"T", "S", "N", "E", "D"};
    const BlockStyle expected[] = {BlockStyle().align(TEXT_ALIGN_CENTER),
                                   BlockStyle().wrap(TEXT_WRAP_CHAR).truncation(TEXT_TRUNCATION_CLIP),
                                   BlockStyle().align(TEXT_ALIGN_LEFT).truncation(TEXT_TRUNCATION_ELLIPSIS),
                                   BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_NONE),
                                   BlockStyle()};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
      loka::app::scene::Node *node = find(root, ids[i]);
      LOKA_VERIFY(node && node->asTextNode());
      LOKA_VERIFY(node->asTextNode()->props.blockStyle_ == expected[i]);
    }
    loka::app::scene::Node *markup = find(root, "M");
    LOKA_VERIFY(markup && markup->asAttributedTextNode());
    LOKA_VERIFY(markup->asAttributedTextNode()->props.blockStyle_
                == BlockStyle().align(TEXT_ALIGN_RIGHT).wrap(TEXT_WRAP_WORD));
    LOKA_VERIFY(markup->asAttributedTextNode()->props.text_->get() == Styled("x", FontSize<18>() + Bold));

    const char *kinds[] = {"Text", "Markup"};
    const char *bad[] = {"{gap:4}",
                         "{align:1}",
                         "{align:'justify'}",
                         "{wrap:1}",
                         "{wrap:'words'}",
                         "{truncation:false}",
                         "{truncation:'cut'}",
                         "null",
                         "[]",
                         "new Date()",
                         "Object.create({align:'center'})",
                         "{[Symbol('x')]:1}",
                         "Object.defineProperty({},'gap',{value:4})",
                         "{align:undefined}",
                         "{align:'center\\u0000'}",
                         "{['align\\u0000']:'center'}"};
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
    {
      for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
      {
        const std::string call = std::string(kinds[k]) + "('a',undefined," + bad[i] + ")";
        const std::string source = "card('first',class{constructor(c){this.c=c;}compose(){return " + call + "}});";
        checkComposeRefusal(source.c_str(), "TypeError");
        const std::string caught = "(()=>{try{" + call + "}catch(e){return e instanceof TypeError}return false})()";
        LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Utf8(caught.data(), caught.size()), value, error));
        LOKA_VERIFY(value.compare(loka::core::String::Literal("true")) == 0);
      }
      const std::string changed =
          std::string("card('first',class{constructor(c){this.c=c;}compose(){const b={align:'center'};const t=")
          + kinds[k] + "('a',undefined,b).TEST_ID('M');b.gap=4;return t}});";
      checkComposeRefusal(changed.c_str(), k == 0 ? "TypeError" : "Markup", true);
      const std::string throwing = std::string("card('first',class{constructor(c){this.c=c;}compose(){return ")
                                   + kinds[k] + "('a',undefined,{get align(){throw new Error('block getter')}})}});";
      checkComposeRefusal(throwing.c_str(), "block getter");
    }
    // Inherited properties are not dictionary entries, even on Object.prototype.
    LOKA_VERIFY(runtime.evaluateToString(
        loka::core::String::Literal(
            "(()=>{Object.prototype.gap=4;try{return Text('a',undefined,Object.create(null)).kind>0 && "
            "Markup('x',undefined,{}).kind>0}finally{delete Object.prototype.gap}})()"),
        value,
        error));
    LOKA_VERIFY(value.compare(loka::core::String::Literal("true")) == 0);
  }

  void checkMarkupAllocationRefusal()
  {
    using namespace loka::core::testing;
    // Two Builder allocations per pass for five segments: fail both initial
    // allocation and growth, first at declaration, then at lowering.
    for (int failure = 1; failure <= 4; ++failure)
    {
      failLokaAllocRaw("AttributedString", "Segments", failure);
      checkComposeRefusal(
          "card('first',class{constructor(c){this.c=c;}compose(){return VStack(Text('prefix').TEST_ID('Prefix'),"
          "Markup('a<b>b</b>c<i>d</i>e').TEST_ID('M'))}});",
          failure <= 2 ? "TypeError" : "Markup",
          true);
      LOKA_VERIFY(lokaAllocRawLive() == 0);
      allowLokaAllocRaw();
    }
    failLokaAllocRaw("AttributedString", "Segments", 1);
    {
      loka::app::AttributedString result;
      LOKA_VERIFY(!smirkycard::ParseSmirkyMarkup(0, 0, loka::app::TextStyle(), result));
      LOKA_VERIFY(result.valid() && result.empty());
    }
    LOKA_VERIFY(lokaAllocRawLive() == 0);
    allowLokaAllocRaw();
    failLokaAllocRaw("SmirkyMarkup", "Frame", 2);
    {
      loka::app::AttributedString result;
      const char *sample = "<b>a<i>b</i></b>";
      LOKA_VERIFY(!smirkycard::ParseSmirkyMarkup(sample, std::strlen(sample), loka::app::TextStyle(), result));
      LOKA_VERIFY(result.empty());
    }
    LOKA_VERIFY(lokaAllocRawLive() == 0);
    allowLokaAllocRaw();
  }

  void checkRecordAllocationRefusal()
  {
    using namespace loka::core::testing;
    const char *types[] = {"Seat", "Handler"};
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
      failLokaAllocRaw("JsCardNode", types[i], 1);
      checkComposeRefusal("card('first',class{constructor(c){this.c=c;this.s=this.c.state('s')}compose(){return "
                          "Button(this.s,()=>{})}});",
                          "Could not allocate");
      LOKA_VERIFY(lokaAllocRawLive() == 0);
      allowLokaAllocRaw();
    }
  }

  void checkRequiredRefusals()
  {
    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;}compose(){this.c.state('late');return VStack()}});",
        "constructor");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;this.c.state(1.5)}compose(){return VStack()}});",
                        "state(number) requires an integer");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}get compose(){for(;;){}}});", "interrupted");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;for(let "
                        "i=0;i<129;++i)this.c.state(0)}compose(){return VStack()}});",
                        "kCardSeatBudget");
    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;}compose(){return VStack(Array.from({length:9},(_,r)=>Row("
        "Array.from({length:r==8?1:16},()=>Button('x',()=>{})))))}});",
        "kCardClickableBudget");
    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;}compose(){return VStack(Array.from({length:9},(_,r)=>Row("
        "Array.from({length:r==8?1:16},()=>Cell('x',()=>{})))))}});",
        "kCardClickableBudget");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return {kind:99}}});", "unknown kind");

    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;}compose(){return VStack(Array.from({length:9},(_,r)=>Row("
        "Array.from({length:r==8?1:16},()=>r%2?Cell('x',()=>{}):Button('x',()=>{})))))}});",
        "kCardClickableBudget");
    checkComposeRefusal(
        "card('first',class{constructor(c){this.c=c;this.s=this.c.state(1)}compose(){return Cell(this.s,()=>{})}});",
        "String state seat");
    checkComposeRefusal("card('first',class{constructor(c){this.c=c;}compose(){return Cell('x',42)}});", "function");

    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;}compose(){return "
                                    "VStack(Button('Throw',()=>{throw new Error('handler "
                                    "boom')}).TEST_ID('Throw'),Button('Loop',()=>{for(;;){}}).TEST_ID('Loop'),Text("
                                    "this.c.error).TEST_ID('SmirkyCard."
                                    "Status'))}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::TextNode *status = find(root, "SmirkyCard.Status")->asTextNode();
    find(root, "Throw")->asButtonNode()->props.getOnClick()->emit();
    LOKA_VERIFY(textValue(status).find("handler boom") != std::string::npos);
    find(root, "Loop")->asButtonNode()->props.getOnClick()->emit();
    LOKA_VERIFY(textValue(status).find("interrupted") != std::string::npos);

    smirkycard::ScriptRuntime exceptionRuntime;
    LOKA_VERIFY(exceptionRuntime.loadBuiltin(
        "card('first',class{constructor(c){this.c=c;this.value=this.c.state('ready')}compose(){return "
        "VStack(Button('Throw "
        "object',()=>{throw "
        "{toString(){for(;;){}}}}).TEST_ID('ObjectThrow'),Button('Recover',()=>this.value.set('working')).TEST_ID('"
        "Recover'),Text(this.value).TEST_ID('Value'),Text(this.c.error).TEST_ID('SmirkyCard.Status'))}});",
        error));
    NullPlatformContext exceptionContext;
    NullScenePlatformController exceptionPlatform;
    WindowProps exceptionProps;
    exceptionProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, exceptionRuntime));
    NullWindow exceptionWindow(&exceptionContext, exceptionProps, &exceptionPlatform);
    WindowAdmissionTestApp exceptionAdmission(exceptionWindow);
    loka::dsl::testing::SceneTestAccess::updateAttached(*exceptionWindow.scene(), true);
    loka::app::scene::Node *exceptionRoot = loka::dsl::testing::SceneTestAccess::rootNode(*exceptionWindow.scene());
    find(exceptionRoot, "ObjectThrow")->asButtonNode()->props.getOnClick()->emit();
    LOKA_VERIFY(textValue(find(exceptionRoot, "SmirkyCard.Status")->asTextNode()).find("interrupted")
                != std::string::npos);
    find(exceptionRoot, "Recover")->asButtonNode()->props.getOnClick()->emit();
    LOKA_VERIFY(textValue(find(exceptionRoot, "Value")->asTextNode()) == "working");
  }

  void expectJs(smirkycard::ScriptRuntime &runtime, const char *source, const char *expected)
  {
    loka::core::String result, error;
    LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Literal(source), result, error));
    if (result.compare(loka::core::String::Literal(expected)) != 0)
    {
      const loka::core::StringBuffer bytes = result.bufferWithEncoding(loka::core::StringEncodingUtf8);
      std::fprintf(stderr,
                   "JS pin: %s; expected %s; got %.*s\n",
                   source,
                   expected,
                   static_cast<int>(bytes.length()),
                   bytes.data() ? static_cast<const char *>(bytes.data()) : "");
    }
    LOKA_VERIFY(result.compare(loka::core::String::Literal(expected)) == 0);
  }

  void checkMaterializedGetters()
  {
    std::puts("[pin] card getters read materialized seats");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var reads=[],detached='';card('first',class{constructor(c){this.s=c.state('ready');this.c=c;}"
        "get onAttach(){reads.push('attach:'+this.s.get());return ()=>reads.push('attached')}"
        "get onDetach(){reads.push('detach:'+this.s.get());const s=this.s;return "
        "()=>{s.set('detached');detached=s.get()}}"
        "get compose(){reads.push('compose:'+this.s.get());const v=this.s.get();"
        "if(this.c.error.get()!=='')throw Error('unexpected error');return ()=>Text(v).TEST_ID('Getter.Value')}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *value = windowNode(window, "Getter.Value");
    LOKA_VERIFY(value && value->asTextNode());
    LOKA_VERIFY(textValue(value->asTextNode()) == "ready");
    expectJs(runtime, "reads.join(',')", "attach:ready,detach:ready,attached,compose:ready");
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), false);
    expectJs(runtime, "detached", "detached");
    expectJs(runtime, "reads.length", "4");
  }

  void checkEarlySeatRefusal()
  {
    std::puts("[pin] unmaterialized seats throw TypeError instead of entering native state access");
    const char *operations[] = {"c.state('s').get()",
                                "c.state(1).get()",
                                "c.state(true).get()",
                                "c.state('s').set('new')",
                                "c.state(1).set(2)",
                                "c.state(true).set(false)",
                                "c.error.get()"};
    for (unsigned i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i)
    {
      const std::string source =
          std::string("card('first',class{constructor(c){") + operations[i] + "}compose(){return VStack()}});";
      checkComposeRefusal(source.c_str(), "TypeError: state seat is not materialized", true);
    }
  }

  void checkMissingComposeAfterAttach()
  {
    std::puts("[pin] missing compose is refused after attach");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("var attached=false;card('first',c=>({onAttach(){attached=true}}));", error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *status = windowNode(window, "SmirkyCard.Status");
    LOKA_VERIFY(status && status->asTextNode());
    LOKA_VERIFY(textValue(status->asTextNode()).find("card(name, F): F(c) must return an object with compose()")
                != std::string::npos);
    expectJs(runtime, "attached", "true");
  }

  void checkCarryGo(const char *expression, const char *predicate)
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var source,received,firstContext,nextContext;"
        "card('first',c=>{firstContext=c;return {compose(){return Text('first').TEST_ID('First')}}});"
        "card('second',c=>{nextContext=c;received=c.carry;return {compose(){return "
        "Text('second').TEST_ID('Second')}}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    expectJs(runtime, "firstContext.carry === undefined", "true");
    const std::string call = std::string("source=(") + expression + ");firstContext.go('second',source);'queued'";
    expectJs(runtime, call.c_str(), "queued");
    LOKA_VERIFY(windowNode(window, "First"));
    expectJs(runtime, "received === undefined", "true");
    // Mutation is after the call but before admission: a deferred source-value copy loses here.
    expectJs(runtime,
             "if(source && typeof source==='object'){source.n=99;if(source.nested)source.nested.x=88;}'mutated'",
             "mutated");
    admission.flush();
    LOKA_VERIFY(windowNode(window, "Second"));
    expectJs(runtime, predicate, "true");
    expectJs(runtime, "nextContext.go('first');'queued'", "queued");
    admission.flush();
    LOKA_VERIFY(windowNode(window, "First"));
    expectJs(runtime, "firstContext.carry === undefined", "true");
    admission.flush();
  }

  void checkCarryRefusals(int selected = -1)
  {
    const char *bad[] = {
        "function(){}",
        "Symbol('v')",
        "{[Symbol('key')]:1}",
        "{x:undefined}",
        "[undefined]",
        "[,,]",
        "Object.assign([1],{extra:2})",
        "Object.defineProperty([], 'x', {value:1})",
        "{get x(){++getterCalls;return 1}}",
        "Object.defineProperty({},'x',{set(v){++getterCalls},enumerable:true})",
        "Object.defineProperty({},'x',{value:1})",
        "new Date()",
        "/x/",
        "new Map()",
        "new Set()",
        "new Uint8Array(2)",
        "new ArrayBuffer(2)",
        "new DataView(new ArrayBuffer(2))",
        "new Proxy({}, {ownKeys(){++getterCalls;return []},getPrototypeOf(){++getterCalls;return null}})",
        "Proxy.revocable({},{}).proxy",
        "(()=>{var p=Proxy.revocable({},{});p.revoke();return p.proxy})()",
        "new (class X{})",
        "Object.create({x:1})",
        "1n",
        "NaN",
        "Infinity",
        "-Infinity",
        "{x:()=>1}",
        "{toJSON(){++getterCalls;return 1}}",
        "new Number(2)",
        "new String('s')",
        "new Boolean(true)",
        "(()=>{var x={};x.self=x;return x})()",
        "(()=>{var x=[];x[0]=x;return x})()",
        "Object.defineProperty([1],'0',{get(){++getterCalls;return 1}})",
        "new (class X extends Array{})(1,2)"};
    const char *oversized[] = {"'x'.repeat(16383)",
                               "'é'.repeat(8192)",
                               "'\\n'.repeat(2731)",
                               "(()=>{var x=0;for(var i=0;i<33;i++)x=[x];return x})()",
                               "Array(1025).fill(0)",
                               "({a:Array(512).fill(0),b:Array(511).fill(0)})",
                               "(()=>{var x={};for(var i=0;i<1025;i++)x[i]=0;return x})()",
                               // Refused by length before its keys are listed (#1037 review).
                               "(()=>{var a=[];a.length=1000000;return a})()"};
    const char *doors[] = {"firstContext.go('second',", "firstContext.open('NEXT.JS',", "firstContext.reload("};
    for (unsigned d = 0; d < 3; ++d)
    {
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      LOKA_VERIFY(runtime.loadBuiltin("var getterCalls=0,firstContext;card('first',c=>{firstContext=c;return "
                                      "{compose(){return Text('live').TEST_ID('Live')}}});"
                                      "card('second',c=>({compose(){return Text('wrong').TEST_ID('Wrong')}}));",
                                      error));
      NullPlatformContext context;
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Scene *before = window.scene();
      smirkycard::JsEngine *engine = runtime.currentEngine();
      for (unsigned group = 0; group < 2; ++group)
      {
        const unsigned count = group ? sizeof(oversized) / sizeof(oversized[0]) : sizeof(bad) / sizeof(bad[0]);
        for (unsigned i = 0; i < count; ++i)
        {
          if (selected >= 0 && static_cast<unsigned>(selected) != i + (group ? sizeof(bad) / sizeof(bad[0]) : 0))
            continue;
          const std::string call =
              std::string("try{") + doors[d] + (group ? oversized[i] : bad[i]) + ");'accepted'}catch(e){e.name}";
          loka::core::String result, callError;
          LOKA_VERIFY(runtime.evaluateToString(loka::core::String::Utf8(call.data(), call.size()), result, callError));
          expectJs(runtime, "getterCalls", "0");
          LOKA_VERIFY(result.compare(loka::core::String::Literal(group ? "RangeError" : "TypeError")) == 0);
          admission.flush();
          LOKA_VERIFY(window.scene() == before && runtime.currentEngine() == engine);
          LOKA_VERIFY(windowNode(window, "Live") && !windowNode(window, "Wrong"));
          expectJs(runtime, "firstContext.error.get()", "");
        }
      }
      // Successful navigation after every refusal proves the card remained Live.
      expectJs(runtime, "firstContext.go('second');'queued'", "queued");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "Wrong"));
      admission.flush();
    }
  }

  void checkCarryAcrossEngines()
  {
    const char *directory = "_smirkycard_carry_fixture";
    const char *mainPath = "_smirkycard_carry_fixture/MAIN.JS";
    const char *nextPath = "_smirkycard_carry_fixture/NEXT.JS";
    std::remove(mainPath);
    std::remove(nextPath);
    removeDirectory(directory);
    LOKA_VERIFY(makeDirectory(directory));
    const char *source = "var current;card('first',c=>{current=c;return {compose(){return VStack("
                         "Text(c.carry===undefined?'absent':String(c.carry.n)).TEST_ID('Carry'),"
                         "Button('open',()=>{var v={n:7};c.open('NEXT.JS',v);v.n=99}).TEST_ID('Open'),"
                         "Button('reload',()=>{var v={n:8};c.reload(v);v.n=99}).TEST_ID('Reload'))}}});";
    writeMain(mainPath, source);
    writeMain(nextPath, source);
    NullPlatformContext context;
    context.setApplicationDirectory(loka::core::String::Literal(directory));
    smirkycard::ScriptRuntime runtime;
    runtime.loadMain(&context);
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      LOKA_VERIFY(textValue(windowNode(window, "Carry")->asTextNode()) == "absent");
      const char *buttons[] = {"Open", "Reload"};
      const char *expected[] = {"7", "8"};
      for (unsigned i = 0; i < 2; ++i)
      {
        smirkycard::JsEngine *old = runtime.currentEngine();
        clickCardButton(window, buttons[i]);
        LOKA_VERIFY(runtime.currentEngine() != old);
        admission.flush();
        LOKA_VERIFY(textValue(windowNode(window, "Carry")->asTextNode()) == expected[i]);
        expectJs(runtime, "current.carry.n=42;current.carry.n", "42");
        admission.flush();
        LOKA_VERIFY(runtime.retiredEngineCount() == 0);
      }
    }
    std::remove(mainPath);
    std::remove(nextPath);
    removeDirectory(directory);
  }

  void checkCarryAllocationFailure()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var current,started=false;card('first',c=>{current=c;return {compose(){return "
        "Text('live').TEST_ID('Live')}}});"
        "card('second',c=>{started=true;return {compose(){return Text('second').TEST_ID('Second')}}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    {
      // Refuse ownership of an otherwise completed encoding before any Scene is queued.
      const char *call = "current.go('second',{n:1})";
      loka::core::testing::failLokaAllocRaw("Managed", "ControlBlock", 1);
      JSValue result = JS_Eval(runtime.context(), call, std::strlen(call), "<carry allocation>", JS_EVAL_TYPE_GLOBAL);
      LOKA_VERIFY(JS_IsException(result));
      JSValue exception = JS_GetException(runtime.context());
      JS_FreeValue(runtime.context(), exception);
      JS_FreeValue(runtime.context(), result);
      LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == 1);
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
      loka::core::testing::allowLokaAllocRaw();
      admission.flush();
      LOKA_VERIFY(windowNode(window, "Live"));
    }
    expectJs(runtime, "current.go('second','x'.repeat(16000));'queued'", "queued");
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(runtime.jsRuntime(), &usage);
    // Context/method allocation fits; the destination's 16KB decoded string does not.
    JS_SetMemoryLimit(runtime.jsRuntime(), static_cast<size_t>(usage.malloc_size) + 4096);
    admission.flush();
    JS_SetMemoryLimit(runtime.jsRuntime(), static_cast<size_t>(-1));
    loka::app::scene::Node *status = windowNode(window, "SmirkyCard.Status");
    LOKA_VERIFY(status && status->asTextNode());
    LOKA_VERIFY(textValue(status->asTextNode()).find("Could not decode carry in the destination card.")
                != std::string::npos);
    LOKA_VERIFY(!windowNode(window, "Live") && !windowNode(window, "Second"));
    expectJs(runtime, "started", "false");
    admission.flush();
  }

  void checkCarry()
  {
    std::puts("[pin] carry: snapshot, destination mutation, absence, strict refusal, budgets and engine replacement");
    checkCarryGo("{n:1,list:[1,'a',true,null],nested:{x:2}}",
                 "received.n===1 && received.list.join(',')==='1,a,true,' && received.nested.x===2 && "
                 "(received.nested.x=5,source.nested.x===88)");
    checkCarryGo("(()=>{var shared={x:1};return {a:shared,b:shared}})()",
                 "received.a!==received.b && (received.a.x=9,received.b.x===1)");
    checkCarryGo("Object.assign(Object.create(null),{x:1})", "received.x===1");
    checkCarryGo("{['__proto__']:{x:1}, ['a\\u0000b']:'\\ud800\\udfff\\udc00\\n\\\"\\\\é'}",
                 "Object.hasOwn(received,'__proto__') && received['a\\u0000b']==='\\ud800\\udfff\\udc00\\n\\\"\\\\é'");
    checkCarryGo("undefined", "received===undefined && nextContext.carry===undefined");
    checkCarryGo("null", "received===null");
    checkCarryGo("[-0,1.7976931348623157e308,5e-324,-1.25]",
                 "Object.is(received[0],-0) && received[1]===Number.MAX_VALUE && received[2]===Number.MIN_VALUE && "
                 "received[3]===-1.25");
    checkCarryGo("'x'.repeat(16381)", "received.length===16381");
    checkCarryGo("'x'.repeat(16382)", "received.length===16382");
    checkCarryGo("'é'.repeat(8191)", "received.length===8191");
    checkCarryGo("(()=>{var x=0;for(var i=0;i<32;i++)x=[x];return x})()",
                 "(()=>{var x=received,n=0;while(Array.isArray(x)){++n;x=x[0]}return n===32&&x===0})()");
    checkCarryGo("Array(1024).fill(0)", "received.length===1024");
    checkCarryGo("({a:Array(511).fill(0),b:Array(511).fill(0)})", "received.a.length+received.b.length===1022");
    checkCarryGo("(JSON={stringify(){throw Error('replaceable JSON')},parse(){throw Error('replaceable JSON')}},[1])",
                 "received[0]===1");
    checkCarryGo("(globalThis.poisonCalls=0,Object.defineProperty(Object.prototype,'carry',"
                 "{get(){return 'poison'},set(v){++poisonCalls;v.n=9},configurable:true}),{n:1})",
                 "received.n===1 && poisonCalls===0 && Object.hasOwn(nextContext,'carry')");
    checkCarryGo("(Object.defineProperty(Object.prototype,'carry',{value:'poison',writable:false}),{n:1})",
                 "received.n===1 && Object.hasOwn(nextContext,'carry')");
    checkCarryAllocationFailure();
    checkCarryRefusals();
    checkCarryAcrossEngines();
  }

  void checkCardContext()
  {
    std::puts("[pin] machine CPU class: exact, total, distinct mapping");
    const char *cpuNames[] = {"68k", "ppc60x", "g3+"};
    LOKA_VERIFY(sizeof(cpuNames) / sizeof(cpuNames[0]) == loka::platform::CPU_CLASS_COUNT);
    for (int i = 0; i < loka::platform::CPU_CLASS_COUNT; ++i)
    {
      const char *name = smirkycard::testing::machineCpuClassName(static_cast<loka::platform::CpuClass>(i));
      LOKA_VERIFY(name && name[0]);
      LOKA_VERIFY(std::strcmp(name, cpuNames[i]) == 0);
      for (int j = 0; j < i; ++j)
        LOKA_VERIFY(std::strcmp(name, cpuNames[j]) != 0);
    }
    LOKA_VERIFY(!smirkycard::testing::machineCpuClassName(loka::platform::CPU_CLASS_COUNT));
    std::puts("[pin] card context: class, factory, arrow construction exactly once");
    const char *factories[] = {"class {constructor(c){++calls;this.s=c.state('class');this.c=c;globalThis.context=c;}"
                               "compose(d){globalThis.delegate=d;return Text(this.s).TEST_ID('Value')}}",
                               "function(c){++calls;globalThis.context=c;var s=c.state('factory');return {"
                               "compose(d){globalThis.delegate=d;return Text(s).TEST_ID('Value')}}}",
                               "(c)=>{++calls;globalThis.context=c;var s=c.state('arrow');return {"
                               "compose(d){globalThis.delegate=d;return Text(s).TEST_ID('Value')}}}"};
    const char *values[] = {"class", "factory", "arrow"};
    for (unsigned i = 0; i < 3; ++i)
    {
      NullPlatformContext context;
      smirkycard::ScriptRuntime runtime(&context);
      loka::core::String error;
      // An inherited accessor must not intercept the host's machine snapshot:
      // All fields are defined as own properties, never assigned (bot P2, #1463).
      const std::string source =
          std::string("['cpuClass','displayDepth','machine'].forEach(k=>Object.defineProperty(Object.prototype,k,"
                      "{configurable:true,get(){return 'spoof'},set(v){throw new TypeError('poison '+k)}}));"
                      "var calls=0;card('first',")
          + factories[i] + ");";
      LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      loka::app::scene::Node *valueNode = windowNode(window, "Value");
      LOKA_VERIFY(valueNode && valueNode->asTextNode());
      LOKA_VERIFY(textValue(valueNode->asTextNode()) == values[i]);
      expectJs(runtime, "calls", "1");
#if defined(_WIN32) || (defined(__APPLE__) && defined(__MACH__))
      expectJs(runtime, "context.machine.cpuClass === 'g3+'", "true");
      std::puts("[skip] Linux absent CPU class runtime branch on Windows/macOS");
#else
      expectJs(runtime, "context.machine.cpuClass === null", "true");
      std::puts("[skip] G3+ runtime branch: this host's queryCpuClass declines");
#endif
      expectJs(runtime, "context.machine.displayDepth === null", "true");
      std::puts("[skip] answering displayDepth runtime branch: this context has no native display answer");
      expectJs(runtime, "Object.isFrozen(context.machine)", "true");
      expectJs(runtime,
               "Object.prototype.hasOwnProperty.call(context,'machine')&&"
               "Object.prototype.hasOwnProperty.call(context.machine,'cpuClass')&&"
               "Object.prototype.hasOwnProperty.call(context.machine,'displayDepth')",
               "true");
      expectJs(runtime,
               "new class {check(){const before=context.machine.cpuClass;let refused=false;"
               "try{context.machine.cpuClass='x'}catch(e){refused=e instanceof TypeError}"
               "return refused && context.machine.cpuClass===before}}().check()",
               "true");
      expectJs(runtime,
               "new class {check(){const before=context.machine.displayDepth;let refused=false;"
               "try{context.machine.displayDepth=8}catch(e){refused=e instanceof TypeError}"
               "return refused && context.machine.displayDepth===before}}().check()",
               "true");
      expectJs(runtime, "Object.isFrozen(context)", "true");

      expectJs(runtime,
               "[typeof state,typeof go,typeof open,typeof reload].join(',')",
               "undefined,undefined,undefined,undefined");
      expectJs(runtime, "try{delegate.declare(VStack());'accepted'}catch(e){String(e).includes('only valid')}", "true");
      expectJs(runtime, "context.state('late')", "undefined");
      expectJs(runtime, "context.error.get().includes('constructor')", "true");
    }
    {
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      LOKA_VERIFY(runtime.loadBuiltin("var reads=0;card('first',c=>({get compose(){++reads;return d=>{"
                                      "globalThis.failedDeclare=d.declare;throw Error('compose threw')}},"
                                      "get onAttach(){++reads;return ()=>{}},get onDetach(){++reads;return ()=>{}}}));",
                                      error));
      NullPlatformContext context;
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      LOKA_VERIFY(textValue(windowNode(window, "SmirkyCard.Status")->asTextNode()).find("compose threw")
                  != std::string::npos);
      expectJs(runtime, "reads", "3");
      expectJs(runtime, "try{failedDeclare(VStack());'accepted'}catch(e){String(e).includes('only valid')}", "true");
    }
    std::puts("[pin] card context: malformed results and throwing property reads");
    const char *bad[] = {
        "function(c){return {}}", "function(c){return 1}", "function(c){}", "c=>1", "c=>undefined", "c=>({compose:1})"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
      const std::string source = std::string("card('first',") + bad[i] + ");";
      checkComposeRefusal(source.c_str(), "card(name, F): F(c) must return an object with compose()");
    }
    checkComposeRefusal("card('first',c=>({get compose(){throw Error('compose read')}}));", "compose read");
    checkComposeRefusal("card('first',c=>({compose(){return VStack()},get onAttach(){throw Error('attach read')}}));",
                        "attach read");
    checkComposeRefusal("card('first',c=>({compose(){return VStack()},get onDetach(){throw Error('detach read')}}));",
                        "detach read");
  }

  void checkExceptionContext()
  {
    std::puts("[pin] card context: exception stringification cannot reenter closed windows");
    const char *factories[] = {
        "c=>{throw {toString(){globalThis.late=String(c.state(1));return 'constructor throw'}}}",
        "c=>({compose(d){throw {toString(){try{d.declare(Text('late'));globalThis.late='accepted'}"
        "catch(e){globalThis.late='refused'}return 'compose throw'}}}})",
        "c=>{var s=c.state('live');return {compose(){return VStack()},onDetach(){throw {toString(){"
        "try{s.get();globalThis.late='accepted'}catch(e){globalThis.late='refused'}return 'detach throw'}}}}}"};
    for (unsigned i = 0; i < 3; ++i)
    {
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      const std::string source = std::string("var late='unset';card('first',") + factories[i] + ");";
      LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
      NullPlatformContext context;
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      if (i == 2)
        loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), false);
      expectJs(runtime, "late", i == 0 ? "undefined" : "refused");
    }
  }

  void checkContextRevocation()
  {
    std::puts("[pin] card context: TransitionPending, synchronous revocation, compose scope");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var secondRefused=false,detachValue='',detachGo=false,clicks=0;"
        "card('first',function(c){var seat=c.state('live');"
        "globalThis.saved=seat;globalThis.savedError=c.error;globalThis.savedContext=c;"
        "globalThis.savedGo=c.go;globalThis.savedState=c.state;"
        "return {compose(d){globalThis.savedDeclare=d.declare;return VStack("
        "Button('next',()=>{++clicks;c.go('second');try{c.go('first')}catch(e){secondRefused=true}}).TEST_ID('Next'),"
        "Text(c.error).TEST_ID('Error'))},"
        "onDetach(){seat.set('detaching');detachValue=seat.get();"
        "try{c.go('first')}catch(e){detachGo=true}throw Error('detach hook threw')}}});"
        "card('second',c=>({compose(){return Text('second').TEST_ID('Second')}}));",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    expectJs(runtime, "savedError.get()", "");
    expectJs(runtime, "try{savedDeclare(VStack());'accepted'}catch(e){String(e).includes('only valid')}", "true");
    windowNode(window, "Next")->asButtonNode()->props.getOnClick()->emit();
    expectJs(runtime, "secondRefused", "true");
    // Detach without reclaim: the capability must close on this line, even if the hook throws.
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), false);
    expectJs(runtime, "detachValue+','+detachGo", "detaching,true");
    const char *retained[] = {"savedGo('first')",
                              "savedState(1)",
                              "saved.get()",
                              "saved.set('bad')",
                              "savedError.get()",
                              "savedContext.reload()",
                              "savedContext.open('MAIN.JS')",
                              "savedDeclare(VStack())"};
    for (unsigned i = 0; i < sizeof(retained) / sizeof(retained[0]); ++i)
    {
      const std::string probe = std::string("try{") + retained[i] + ";'accepted'}catch(e){'refused'}";
      expectJs(runtime, probe.c_str(), "refused");
    }
    admission.flush();
    admission.flush();
    LOKA_VERIFY(windowNode(window, "Second") != 0);
    expectJs(runtime, "try{saved.get();'accepted'}catch(e){'refused'}", "refused");
  }

  void checkRetiredSeatAndIntegerRefusal()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',class{constructor(c){this.c=c;this.count=this.c.state(0);globalThis."
                                    "saved=this.count}compose(){return "
                                    "VStack(Button('Next',()=>this.c.go('second')).TEST_ID('Next'))}});card('second',"
                                    "class{constructor(c){this.c=c;}compose(){"
                                    "return "
                                    "VStack()}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Next")
        ->asButtonNode()
        ->props.getOnClick()
        ->emit();
    admission.flush();
    admission.flush();
    loka::core::String result;
    LOKA_VERIFY(!runtime.evaluateToString(loka::core::String::Literal("saved.get()"), result, error));
    LOKA_VERIFY(!runtime.evaluateToString(loka::core::String::Literal("saved.set(1)"), result, error));
    const loka::core::StringBuffer retiredError = error.bufferWithEncoding(loka::core::StringEncodingUtf8);
    LOKA_VERIFY(std::string(static_cast<const char *>(retiredError.data()), retiredError.length()).find("revoked card")
                != std::string::npos);

    smirkycard::ScriptRuntime integerRuntime;
    LOKA_VERIFY(integerRuntime.loadBuiltin(
        "card('first',class{constructor(c){this.c=c;this.count=this.c.state(1);globalThis.numberSeat=this.count}"
        "compose(){return "
        "VStack(Text(this.count).TEST_ID('Number.Count'),Text(this.c.error).TEST_ID('SmirkyCard.Status'))}});",
        error));
    NullPlatformContext integerContext;
    NullScenePlatformController integerPlatform;
    WindowProps integerProps;
    integerProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, integerRuntime));
    NullWindow integerWindow(&integerContext, integerProps, &integerPlatform);
    WindowAdmissionTestApp integerAdmission(integerWindow);
    loka::dsl::testing::SceneTestAccess::updateAttached(*integerWindow.scene(), true);
    loka::app::scene::Node *integerRoot = loka::dsl::testing::SceneTestAccess::rootNode(*integerWindow.scene());
    LOKA_VERIFY(integerRuntime.evaluateToString(loka::core::String::Literal("numberSeat.set(1.5)"), result, error));
    LOKA_VERIFY(textValue(find(integerRoot, "Number.Count")->asTextNode()) == "1");
    LOKA_VERIFY(textValue(find(integerRoot, "SmirkyCard.Status")->asTextNode()).find("integer") != std::string::npos);
    LOKA_VERIFY(integerRuntime.evaluateToString(loka::core::String::Literal("numberSeat.set(2)"), result, error));
    LOKA_VERIFY(textValue(find(integerRoot, "Number.Count")->asTextNode()) == "2");
  }

  void checkMessageBox(smirkycard::ScriptRuntime &runtime)
  {
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);

    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window.scene());
    loka::app::scene::Node *scriptNode = find(root, "SmirkyCard.Script");
    loka::app::scene::Node *runNode = find(root, "SmirkyCard.RunScript");
    loka::app::scene::Node *resultNode = find(root, "SmirkyCard.Result");
    LOKA_VERIFY(scriptNode && runNode && resultNode);
    loka::app::EditTextNode *script = scriptNode->asEditTextNode();
    loka::app::ButtonNode *run = runNode->asButtonNode();
    loka::app::TextNode *result = resultNode->asTextNode();
    LOKA_VERIFY(script && run && result && script->props.text_.isValid() && run->props.getOnClick());

    loka::app::scene::BoundaryNode *owner = loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene());
    const char *sources[] = {"1+1",
                             "['a','b'][1].toUpperCase()",
                             "throw new Error('x')",
                             "for(;;){}",
                             "1+1",
                             "this === globalThis",
                             "typeof this.script"};
    const char *expected[] = {"2", "B", 0, 0, "2", "true", "undefined"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i)
    {
      {
        loka::core::StateTrackerGuard guard(owner->tracker());
        script->props.text_.set(loka::core::String::Literal(sources[i]));
      }
      run->props.getOnClick()->emit();
      const std::string value = textValue(result);
      if (expected[i])
        LOKA_VERIFY(value == expected[i]);
      else if (i == 2)
        LOKA_VERIFY(value.find("Error:") == 0 && value.find(i == 2 ? "x" : "interrupted") != std::string::npos);
      else
      {
        loka::app::scene::Node *status = find(root, "SmirkyCard.Status");
        LOKA_VERIFY(status && textValue(status->asTextNode()).find("interrupted") != std::string::npos);
      }
    }
    // The interrupt budget also covers stringification: a result whose
    // toString loops must come back as an interrupted error, not a hang.
    {
      {
        loka::core::StateTrackerGuard guard(owner->tracker());
        script->props.text_.set(loka::core::String::Literal("({toString: function(){ for(;;){} }})"));
      }
      run->props.getOnClick()->emit();
      loka::app::scene::Node *status = find(root, "SmirkyCard.Status");
      LOKA_VERIFY(status && textValue(status->asTextNode()).find("interrupted") != std::string::npos);
    }
    // Embedded NULs survive (the seam returns byte counts, not strlen).
    {
      {
        loka::core::StateTrackerGuard guard(owner->tracker());
        script->props.text_.set(loka::core::String::Literal("'a\\u0000b'"));
      }
      run->props.getOnClick()->emit();
      const std::string value = textValue(result);
      LOKA_VERIFY(value.size() == 3 && value[0] == 'a' && value[1] == '\0' && value[2] == 'b');
    }
  }
} // namespace

namespace
{
  // A base allocator that hands out 2-byte-aligned blocks, the Mac Plus
  // Memory Manager's alignment (#837). Each block is a host malloc shifted by
  // two bytes so the shift is visible and the raw pointer can be recovered.
  int oddBlocksLive = 0;
  void *TwoByteAlignedAllocate(size_t size)
  {
    char *raw = static_cast<char *>(std::malloc(size + 2));
    if (!raw)
      return 0;
    ++oddBlocksLive;
    return raw + 2;
  }
  void TwoByteAlignedRelease(void *block)
  {
    --oddBlocksLive;
    std::free(static_cast<char *>(block) - 2);
  }

  void testScriptAllocatorAlignsTwoByteAlignedBase()
  {
    std::printf("[pin] testScriptAllocatorAlignsTwoByteAlignedBase\n");
    const SmirkyBaseAllocator base = {TwoByteAlignedAllocate, TwoByteAlignedRelease};
    for (size_t size = 1; size <= 96; size += 7)
    {
      void *block = SmirkyAlignedAllocate(&base, size);
      LOKA_VERIFY(block != 0);
      // QuickJS tags the low two bits of a context pointer and lays arenas
      // out with 8-byte members: nothing below 8 is enough.
      LOKA_VERIFY((reinterpret_cast<uintptr_t>(block) & (SMIRKY_SCRIPT_ALLOC_ALIGN - 1)) == 0);
      LOKA_VERIFY(SmirkyAlignedUsableSize(block) == size);
      std::memset(block, 0x5a, size);
      void *grown = SmirkyAlignedReallocate(&base, block, size + 40);
      LOKA_VERIFY(grown != 0);
      LOKA_VERIFY((reinterpret_cast<uintptr_t>(grown) & (SMIRKY_SCRIPT_ALLOC_ALIGN - 1)) == 0);
      LOKA_VERIFY(SmirkyAlignedUsableSize(grown) == size + 40);
      LOKA_VERIFY(static_cast<unsigned char *>(grown)[size - 1] == 0x5a);
      void *shrunk = SmirkyAlignedReallocate(&base, grown, size);
      LOKA_VERIFY(shrunk == grown && SmirkyAlignedUsableSize(shrunk) == size);
      SmirkyAlignedRelease(&base, shrunk);
    }
    void *fromNull = SmirkyAlignedReallocate(&base, 0, 8);
    LOKA_VERIFY(fromNull != 0 && (reinterpret_cast<uintptr_t>(fromNull) & 7) == 0);
    LOKA_VERIFY(SmirkyAlignedReallocate(&base, fromNull, 0) == 0);
    SmirkyAlignedRelease(&base, 0);
    LOKA_VERIFY(oddBlocksLive == 0);
    LOKA_VERIFY(SmirkyAlignedUsableSize(0) == 0);
  }

  void testScriptContextIsPointerTagAligned()
  {
    std::printf("[pin] testScriptContextIsPointerTagAligned\n");
    SmirkyScript *script = SmirkyScriptCreate();
    LOKA_VERIFY(script != 0);
    LOKA_VERIFY((reinterpret_cast<uintptr_t>(SmirkyScriptContext(script)) & 3) == 0);
    LOKA_VERIFY((reinterpret_cast<uintptr_t>(SmirkyScriptRuntime(script)) & (SMIRKY_SCRIPT_ALLOC_ALIGN - 1)) == 0);
    SmirkyScriptDestroy(script);
  }
  loka::platform::file::FileHandle scenarioAuditDestination()
  {
    loka::platform::file::FileHandle file;
    file.displayPath = loka::core::String::Literal("_smirky_scenario.audit");
    return file;
  }
  std::string readScenarioAudit()
  {
    FILE *file = std::fopen("_smirky_scenario.audit", "rb");
    LOKA_VERIFY(file);
    std::string text;
    char buffer[256];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)))
      text.append(buffer, n);
    LOKA_VERIFY(std::fclose(file) == 0);
    return text;
  }
  /** Plays the runner: owns services, enables before loading, drives only CardScene. */
  class CardRunnerHarness : public loka::dsl::testing::ScenarioAuditSink
  {
  public:
    CardRunnerHarness(const char *source,
                      const char *companion,
                      unsigned long seed = 7,
                      bool baked = false,
                      PlatformContext *nativeContext = 0,
                      const char *scenarioName = "card")
        : audit(scenarioAuditDestination(), scenarioName),
          runtime(nativeContext),
          window(0),
          admission(0),
          actionScene(0),
          terminalCount(0),
          terminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED)
    {
      LOKA_VERIFY(audit.isValid());
      std::remove("_smirky_scenario/MAIN.JS");
      std::remove("_smirky_scenario/CARD.FLOW.JS");
      removeDirectory("_smirky_scenario");
      LOKA_VERIFY(makeDirectory("_smirky_scenario"));
      context.setApplicationDirectory(loka::core::String::Literal("_smirky_scenario"));
      if (source)
        writeMain("_smirky_scenario/MAIN.JS", source);
      if (companion && !baked)
        writeMain("_smirky_scenario/CARD.FLOW.JS", companion);
      LOKA_VERIFY(runtime.enableRunner(this, &clock, seed, "CARD.FLOW.JS", baked ? companion : 0));
      if (baked)
      {
        loka::core::String error;
        LOKA_VERIFY(runtime.loadBuiltin(source, error));
      }
      else
        runtime.loadMain(&context);
    }
    ~CardRunnerHarness()
    {
      delete admission;
      delete window;
      std::remove("_smirky_scenario/MAIN.JS");
      std::remove("_smirky_scenario/CARD.FLOW.JS");
      removeDirectory("_smirky_scenario");
    }
    void mount()
    {
      LOKA_VERIFY(runtime.mainErrorFor(SMIRKY_CARD_FIRST).empty());
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      window = new NullWindow(&context, props, &platform);
      admission = new WindowAdmissionTestApp(*window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window->scene(), true);
      admission->flush();
      actionScene = static_cast<smirkycard::CardScene *>(window->scene());
    }
    void tick()
    {
      // Exercise native-class GC markers with live cards and accepted builders.
      JS_RunGC(runtime.jsRuntime());
      clock.advanceTo(clock.currentTick() + 1);
      static_cast<smirkycard::CardScene *>(window->scene())->tickScenario();
    }
    void ticks(int count)
    {
      for (int i = 0; i < count; ++i)
        tick();
    }
    virtual bool recordStep(const loka::dsl::testing::ScenarioStepTerminal &r)
    {
      steps.push_back(r);
      return audit.recordStep(r);
    }
    virtual bool recordMatch(const loka::dsl::testing::ScenarioMatchSelection &)
    {
      return true;
    }
    virtual bool recordSubstep(const loka::dsl::testing::ScenarioSubstepTerminal &)
    {
      return true;
    }
    virtual bool recordVerdict(const loka::dsl::SnapRecord &)
    {
      return true;
    }
    virtual bool recordTerminal(loka::dsl::testing::ScenarioAuditTerminalStatus value)
    {
      ++terminalCount;
      terminal = value;
      return audit.recordTerminal(value);
    }
    virtual bool recordLog(const std::string &text)
    {
      if (!audit.recordLog(text))
        return false;
      logs.push_back(text);
      if (text == "invalidate")
        actionScene->requestInvalidate();
      if (text == "reenter")
        actionScene->tickScenario();
      // Exercise the synchronous detach line while preserving the runner-owned
      // Scene until this tick unwinds; updateAttached also reclaims the root.
      if (text == "detach")
        loka::dsl::testing::SceneTestAccess::notifyComposeEvent(*actionScene, loka::app::scene::COMPOSE_EVENT_DETACH);
      return true;
    }
    loka::dsl::testing::ScenarioAuditFile audit;
    loka::dsl::testing::ScenarioClock clock;
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    NullWindow *window;
    WindowAdmissionTestApp *admission;
    smirkycard::CardScene *actionScene;
    std::vector<loka::dsl::testing::ScenarioStepTerminal> steps;
    std::vector<std::string> logs;
    int terminalCount;
    loka::dsl::testing::ScenarioAuditTerminalStatus terminal;
  };

  const char *scenarioCard =
      "var saved,invoked=0,steps=0,success=0,failure=0,after=0,firstRandom;"
      "card('first',function(c){saved=c;firstRandom=c.test.random();var s=c.state('before'),off=c.state(false);"
      "return {compose:function(){return VStack(Text(s).TEST_ID('title'),"
      "Button('change',function(){s.set('after');}).TEST_ID('change'),"
      "Button('disabled',function(){throw Error('must not click');}).enabled(off).TEST_ID('off'),"
      "Cell('cell',function(){s.set('cell');}).TEST_ID('cell'),"
      "Markup('<b>markup</b>').TEST_ID('markup'),EditText(s).TEST_ID('edit'),"
      "Text('a').TEST_ID('duplicate'),Text('b').TEST_ID('duplicate'));}};});"
      "card('second',function(c){return {compose:function(){return "
      "Button('back',function(){c.go('first');}).TEST_ID('back');}};});";

  void checkCardScenarios(int only = 0)
  {
    using namespace loka::dsl;
    using namespace loka::dsl::testing;
    std::puts("[pin] runner Flow happy path, values, freeze, once, failure, cancellation");
    if (!only || only == 1)
    {
      CardRunnerHarness h(
          scenarioCard,
          "scenario('first',function(c){++invoked;var f=Flow().step(function(v){"
          "if(v!==undefined)throw Error('initial value');c.test.click('change');"
          "if(c.test.text('title')!=='after'||c.test.text('edit')!=='after'||c.test.text('change')!=='change')throw "
          "Error('text');"
          "if(c.test.text('markup')!=='markup')throw Error('markup');"
          "if(c.test.text('cell')!=='cell')throw Error('cell text');c.test.click('cell');"
          "if(c.test.text('title')!=='cell')throw Error('cell');return 41;}).named('click')"
          ".step(function(v){if(v!==41)throw Error('value41');return 42;}).named('value')"
          ".step(function(v){c.test.log(String(v));}).named('log')"
          ".onSuccess(function(){++success;}).onFailure(function(){++failure;});c.run(f);"
          "try{f.step(function(){});throw Error('mutation accepted');}catch(e){if(!(e instanceof TypeError))throw e;}"
          "try{c.run(f);throw Error('reuse accepted');}catch(e){if(!(e instanceof TypeError))throw e;}});");
      h.mount();
      // Class registrations must remain distinct across independent engines:
      // force tracing of the card capability while its scenario roots are live.
      JS_RunGC(h.runtime.jsRuntime());
      expectJs(h.runtime, "invoked", "0");
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_SUCCEEDED);
      LOKA_VERIFY(h.steps.size() == 6 && h.steps[0].name() == "click" && h.steps[2].name() == "value");
      LOKA_VERIFY(h.logs.size() == 1 && h.logs[0] == "42");
      const std::string audit = readScenarioAudit();
      LOKA_VERIFY(audit.find("name=click\n") < audit.find("terminal status=succeeded\n"));
      LOKA_VERIFY(audit.find("log text=42\n") != std::string::npos);
      expectJs(h.runtime, "[invoked,success,failure].join(',')", "1,1,0");
      expectJs(h.runtime,
               "(function(){try{saved.run(Flow().step(function(){}));return 'bad';}catch(e){return e instanceof "
               "TypeError;}})()",
               "true");
      SceneTestAccess::updateAttached(*h.window->scene(), false);
      SceneTestAccess::updateAttached(*h.window->scene(), true);
      h.ticks(10);
      expectJs(h.runtime, "invoked", "1");
    }
    if (!only || only == 2)
    {
      CardRunnerHarness h(scenarioCard,
                          "scenario('first',function(c){++invoked;c.run(Flow().step(function(){++steps;c.test.log('"
                          "reenter');throw Error('boom message');})"
                          ".named('throwing').onFailure(function(){++failure;}));});");
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_FAILED);
      LOKA_VERIFY(h.steps.size() == 1 && h.steps[0].status() == FLOW_STEP_FAILED);
      LOKA_VERIFY(h.steps[0].message().find("boom message") != std::string::npos);
      LOKA_VERIFY(readScenarioAudit().find("message=Error%3A%20boom%20message\n") != std::string::npos);
      expectJs(h.runtime, "[invoked,steps,failure].join(',')", "1,1,1");
    }
    if (!only || only == 3)
    {
      CardRunnerHarness h(
          scenarioCard,
          "scenario('first',function(c){c.run(Flow().step(function(){++steps;c.test.log('invalidate');})"
          ".named('once').step(function(){++after;}));});");
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.window->scene()->hasPendingInvalidation());
      LOKA_VERIFY(h.terminalCount == 0 && h.steps.size() == 1);
      expectJs(h.runtime, "[steps,after].join(',')", "1,0");
      h.admission->flush();
      h.tick();
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_SUCCEEDED);
      expectJs(h.runtime, "[steps,after].join(',')", "1,1");
    }
    if (!only || only == 4)
    {
      CardRunnerHarness h(scenarioCard,
                          "scenario('first',function(c){c.run(Flow().step(function(){if(c.test.enabled('off'))throw "
                          "Error('enabled');c.test.click('off');}));});");
      h.mount();
      h.tick();
      LOKA_VERIFY(h.terminal == SCENARIO_AUDIT_FAILED && h.terminalCount == 1);
      LOKA_VERIFY(h.steps[0].error().code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
      LOKA_VERIFY(h.steps[0].message().find("button 'off' is disabled") != std::string::npos);
    }
    if (!only || only == 5)
    {
      CardRunnerHarness h(
          scenarioCard,
          "scenario('first',function(c){++invoked;c.run(Flow().step(function(){++steps;c.go('second');})"
          ".named('go').step(function(){++after;}).onSuccess(function(){++success;}).onFailure(function(){++failure;}))"
          ";});"
          "scenario('second',function(c){++invoked;c.run(Flow().step(function(){++after;}));});");
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.window->sceneManager()->hasPendingReplacement());
      LOKA_VERIFY(h.terminalCount == 0 && h.steps.size() == 1);
      expectJs(h.runtime, "[invoked,steps,after,success,failure].join(',')", "1,1,0,0,0");
      h.admission->flush();
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_CANCELED);
      h.ticks(10);
      loka::app::scene::Scene *scene = h.window->scene();
      FlowError error;
      loka::app::scene::Scene *out = 0;
      LOKA_VERIFY(ClickButton("back").run(scene, out, error) == FLOW_STEP_SUCCEEDED);
      h.admission->flush();
      h.ticks(10);
      expectJs(h.runtime, "[invoked,steps,after,success,failure].join(',')", "1,1,0,0,0");
      LOKA_VERIFY(h.terminalCount == 1);
    }
    for (int throwing = 0; (!only || only == 6) && throwing < 2; ++throwing)
    {
      const std::string companion =
          std::string("scenario('first',function(c){c.run(Flow().step(function(){c.test.log('detach');")
          + (throwing ? "throw Error('detached throw');" : "return 99;")
          + "}).named('inflight').step(function(){++after;}).onSuccess(function(){++success;}).onFailure(function(){++"
            "failure;}));});";
      CardRunnerHarness h(scenarioCard, companion.c_str());
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_CANCELED);
      LOKA_VERIFY(h.steps.size() == 1 && h.steps[0].status() == (throwing ? FLOW_STEP_FAILED : FLOW_STEP_SUCCEEDED));
      if (throwing)
        LOKA_VERIFY(h.steps[0].message().find("detached throw") != std::string::npos);
      expectJs(h.runtime, "[after,success,failure].join(',')", "0,0,0");
    }
    const char *badOperations[] = {"c.test.click('missing')",
                                   "c.test.click('duplicate')",
                                   "c.test.click('title')",
                                   "c.test.text('missing')",
                                   "c.test.enabled('title')"};
    for (unsigned i = 0; (!only || only == 7) && i < sizeof(badOperations) / sizeof(badOperations[0]); ++i)
    {
      const std::string companion =
          std::string("scenario('first',function(c){c.run(Flow().step(function(){") + badOperations[i] + ";}));});";
      CardRunnerHarness h(scenarioCard, companion.c_str());
      h.mount();
      h.tick();
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_FAILED);
    }
    if (!only || only == 8)
    {
      std::puts("[pin] runner click settles formatted integer text within one JS step");
      CardRunnerHarness h(
          "card('first',function(c){var n=c.state(0);return {compose:function(){return VStack("
          "Button('inc',function(){n.set(n.get()+1);}).TEST_ID('inc'),Text(n).TEST_ID('count'));}};});",
          "scenario('first',function(c){c.run(Flow().step(function(){c.test.click('inc');"
          "if(c.test.text('count')!=='1')throw Error('first count: '+c.test.text('count'));"
          "c.test.click('inc');if(c.test.text('count')!=='2')throw Error('second count: '+c.test.text('count'));}));});");
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == SCENARIO_AUDIT_SUCCEEDED);
    }
  }

  void checkScenarioSetup()
  {
    const char *sources[] = {0,
                             "scenario('unknown',function(){});",
                             "scenario('first',function(){});scenario('first',function(){});",
                             "throw Error('companion exploded');",
                             "var nothing=0;",
                             "scenario('first',42);"};
    const char *messages[] = {
        "missing", "unknown card", "duplicate", "companion exploded", "no registration", "function"};
    for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i)
    {
      CardRunnerHarness h(scenarioCard, sources[i]);
      const loka::core::StringBuffer b =
          h.runtime.mainErrorFor(SMIRKY_CARD_FIRST).bufferWithEncoding(loka::core::StringEncodingUtf8);
      LOKA_VERIFY(std::string(static_cast<const char *>(b.data()), b.length()).find(messages[i]) != std::string::npos);
      LOKA_VERIFY(!h.runtime.currentEngine()->hasConstructor(SMIRKY_CARD_FIRST));
    }
    {
      CardRunnerHarness h(scenarioCard, "scenario('first',function(c){throw Error('invocation exploded');});");
      h.mount();
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_FAILED);
      LOKA_VERIFY(h.logs[0].find("invocation exploded") != std::string::npos);
    }
    {
      CardRunnerHarness h(scenarioCard, "scenario('first',function(c){c.run(Flow().step(function(){}));});");
      h.mount();
      expectJs(h.runtime,
               "(function(){try{saved.run(Flow().step(function(){}));return 'bad';}catch(e){return "
               "String(e);}})().indexOf('scenario invocation')>=0",
               "true");
      expectJs(
          h.runtime,
          "(function(){try{scenario('first',function(){});return 'bad';}catch(e){return e instanceof TypeError;}})()",
          "true");
      smirkycard::JsEngine *old = h.runtime.currentEngine();
      writeMain("_smirky_scenario/CARD.FLOW.JS", "throw Error('reload companion');");
      loka::core::String error;
      LOKA_VERIFY(!h.runtime.prepareReload(SMIRKY_CARD_FIRST, error));
      LOKA_VERIFY(h.runtime.currentEngine() == old && !error.empty());
      LOKA_VERIFY(!h.runtime.prepareOpen("MAIN.JS", error));
      writeMain("_smirky_scenario/CARD.FLOW.JS",
                "scenario('first',function(c){c.run(Flow().step(function(){c.test.log('fresh');}));});");
      smirkycard::JsEngine *candidate = h.runtime.prepareOpen("MAIN.JS", error);
      LOKA_VERIFY(candidate && candidate != old);
      h.runtime.discardReload(candidate);
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.logs.empty());
    }
    {
      std::puts("[pin] runner refuses MAIN.JS exceeding 64 KiB");
      const std::string source(64u * 1024u + 1u, ' ');
      CardRunnerHarness h(source.c_str(), "scenario('first',function(c){c.run(Flow().step(function(){}));});");
      const loka::core::StringBuffer b =
          h.runtime.mainErrorFor(SMIRKY_CARD_FIRST).bufferWithEncoding(loka::core::StringEncodingUtf8);
      LOKA_VERIFY(std::string(static_cast<const char *>(b.data()), b.length()).find("exceeds 64 KiB")
                  != std::string::npos);
      LOKA_VERIFY(!h.runtime.currentEngine()->hasConstructor(SMIRKY_CARD_FIRST));
    }
    {
      CardRunnerHarness h(0, "scenario('first',function(c){c.run(Flow().step(function(){c.test.log('builtin');}));});");
      h.mount();
      h.tick();
      LOKA_VERIFY(h.logs.size() == 1 && h.logs[0] == "builtin");
      LOKA_VERIFY(h.recordLog("space tab\tline\n%"));
      LOKA_VERIFY(readScenarioAudit().find("log text=space%20tab%09line%0A%25\n") != std::string::npos);
    }
  }

  void checkScenarioEngineReplacement()
  {
    for (int open = 0; open < 2; ++open)
    {
      const std::string companion =
          std::string("scenario('first',function(c){c.run(Flow().step(function(){")
          + (open ? "c.open('MAIN.JS');" : "c.reload();")
          + "return 'old engine value';}).named('replace').step(function(){throw Error('old next');})"
            ".onSuccess(function(){throw Error('old success');}).onFailure(function(){throw Error('old "
            "failure');}));});";
      CardRunnerHarness h(scenarioCard, companion.c_str());
      h.mount();
      smirkycard::JsEngine *old = h.runtime.currentEngine();
      // Two steps: the replacement scenario is invoked after the clock has
      // advanced, and its steps must still be due one per tick from that
      // invocation, not from tick 1 (an absolute schedule would be due at once).
      writeMain("_smirky_scenario/CARD.FLOW.JS",
                "scenario('first',function(c){c.run(Flow().step(function(){c.test.log('new engine');})"
                ".step(function(){c.test.log('new second');}));});");
      h.tick();
      LOKA_VERIFY(h.runtime.currentEngine() != old && h.runtime.retiredEngineCount() == 1);
      JS_RunGC(old->jsRuntime());
      LOKA_VERIFY(h.terminalCount == 0 && h.steps.size() == 1);
      h.admission->flush();
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_CANCELED);
      h.tick();
      LOKA_VERIFY(h.terminalCount == 1 && h.logs.size() == 1 && h.logs[0] == "new engine");
      h.tick();
      LOKA_VERIFY(h.terminalCount == 2 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
      LOKA_VERIFY(h.logs.size() == 2 && h.logs[1] == "new second");
      h.admission->flush();
      LOKA_VERIFY(h.runtime.retiredEngineCount() == 0);
    }
  }

  void checkScenarioRandom()
  {
    std::string sequence;
    for (int pass = 0; pass < 2; ++pass)
    {
      CardRunnerHarness h(
          scenarioCard,
          "scenario('first',function(c){c.run(Flow().step(function(){var "
          "a=[firstRandom,c.test.random(),c.test.random(),c.test.random()];"
          "for(var i=0;i<a.length;++i)if(!(a[i]>=0&&a[i]<1))throw Error('range');c.test.log(a.join(','));}));});",
          7);
      h.mount();
      h.tick();
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
      LOKA_VERIFY(h.logs.size() == 1);
      if (!pass)
        sequence = h.logs[0];
      else
        LOKA_VERIFY(sequence == h.logs[0]);
    }
    std::printf("[probe] scenario random seed=7 first4=%s\n", sequence.c_str());
    // Golden captured from --scenarios on the host, then independently checked
    // with integer LCG states divided by 2^32 (see #1033 d1 findings).
    LOKA_VERIFY(sequence == "0.23878083983436227,0.9134932646993548,0.6124916663393378,0.9269814591389149");
  }

  void checkBakedCompanionReplacement()
  {
    for (int open = 0; open < 2; ++open)
    {
      CardRunnerHarness h(scenarioCard,
          "scenario('first',function(c){c.run(Flow().step(function(){c.test.log('baked');}).named('baked'));});",
          7, true);
      // File entry establishes the platform directory for reload/open.
      h.runtime.loadMain(&h.context);
      h.mount();
      h.tick();
      writeMain("_smirky_scenario/CARD.FLOW.JS", "throw Error('file companion must not run');");
      loka::core::String error;
      smirkycard::JsEngine *candidate = open ? h.runtime.prepareOpen("MAIN.JS", error)
                                           : h.runtime.prepareReload(SMIRKY_CARD_FIRST, error);
      LOKA_VERIFY(candidate && error.empty());
      h.runtime.discardReload(candidate);
    }
  }

  void verifyChosenTransaction(void *data)
  {
    loka::core::StateTracker *tracker = static_cast<loka::core::StateTracker *>(data);
    LOKA_VERIFY(tracker->phase() == loka::core::TRACKER_PRECOMMIT);
  }

  void checkChosenFileDelivery()
  {
    CardRunnerHarness h(
      "var own,wrong,run,ctx,n=0;card('first',c=>{ctx=c;own=c.state.file();wrong=c.state(false);"
      "c.flow(Flow().watch(own,r=>{n++;return Flow.SKIP}));"
      "run=c.flow(Flow().step(()=>c.test.deliverChosenFile(own,null)));"
      "return {chosen:own,wrong,compose(){return Text('delivery')}}});"
      "card('second',c=>{ctx=c;return {compose(){return Text('second')}}});",
      "scenario('first',c=>{c.run(Flow().step(()=>{"
      "for(const seat of [wrong,{},null,'wrong','missing']){let refused=false;"
      "try{c.test.deliverChosenFile(seat,null)}catch(e){refused=e instanceof TypeError}"
      "if(!refused)throw Error('seat refusal')}"
      "c.test.deliverChosenFile('chosen',null);c.test.deliverChosenFile(own,null);"
      "if(n!==2)throw Error('forced cancellation notification');"
      "c.test.deliverChosenFile(own,'Sun.pict');c.test.deliverChosenFile(own,'Sun.pict');"
      "if(n!==4)throw Error('forced file notification');"
      "run.run();if(n!==4)throw Error('CardFlow delivery admitted');"
      "c.test.log('delivery ok')}));});");
    h.mount();
    smirkycard::testing::CardFlowAccess::scene = h.window->scene();
    loka::core::State<loka::app::FileChooserResult> *file = smirkycard::testing::CardFlowAccess::fileState();
    LOKA_VERIFY(file);
    file->bind(verifyChosenTransaction, smirkycard::testing::CardFlowAccess::card()->tracker(),
               false, false, loka::core::STATE_PRIORITY_HIGH);
    h.ticks(20);
    file->unbind(verifyChosenTransaction, smirkycard::testing::CardFlowAccess::card()->tracker());
    LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
    LOKA_VERIFY(h.logs.size() == 1 && h.logs[0] == "delivery ok");
    expectJs(h.runtime, "ctx.go('second');'ok'", "ok");
    h.admission->flush();
    expectJs(h.runtime,
      "let refused=false;try{ctx.test.deliverChosenFile(own,null)}catch(e){refused=e instanceof TypeError}refused",
      "true");
  }

  void checkChosenFileLookupNavigation()
  {
    CardRunnerHarness h(
      "var saved;card('first',c=>{saved=c;const chosen=c.state.file();"
      "return new Proxy({chosen,compose(){return Text('proxy')}},{"
      "getOwnPropertyDescriptor(t,k){if(k==='chosen')c.go('second');"
      "return Reflect.getOwnPropertyDescriptor(t,k)}})});"
      "card('second',c=>({compose(){return Text('second')}}));",
      "scenario('first',c=>c.run(Flow().step(()=>{})));");
    h.mount();
    smirkycard::testing::CardFlowAccess::scene = h.window->scene();
    loka::core::State<loka::app::FileChooserResult> *file = smirkycard::testing::CardFlowAccess::fileState();
    LOKA_VERIFY(file);
    const loka::app::FileChooserResult before = file->get();
    expectJs(h.runtime,
      "let refused=false;try{saved.test.deliverChosenFile('chosen','Sun.pict')}"
      "catch(e){refused=e instanceof TypeError}refused", "true");
    LOKA_VERIFY(!(file->get() != before));
    h.admission->flush();
  }

  void checkImageFacts()
  {
    CardRunnerHarness h(
      "var a,b,picture,other,run,activeRefused=false;"
      "card('first',c=>{a=c;picture=c.state.image();const chosen=c.state.file(),flag=c.state(false);"
      "run=c.flow(Flow().step(()=>{try{c.test.imageFacts(picture)}"
      "catch(e){activeRefused=e instanceof TypeError}}));"
      "return {picture,chosen,flag,compose(){return Text('first')}}});"
      "card('second',c=>{b=c;other=c.state.image();return {picture:other,compose(){return Text('second')}}});",
      "scenario('first',c=>c.run(Flow().step(()=>{})));");
    h.mount();
    expectJs(h.runtime,
      "let facts=a.test.imageFacts('picture');"
      "[Object.getPrototypeOf(facts)===Object.prototype,Object.isFrozen(facts),"
      "Object.keys(facts).sort().join(','),facts.empty,facts.width,facts.height].join(':')",
      "true:true:empty,height,width:true:0:0");
    expectJs(h.runtime, "JSON.stringify(a.test.imageFacts(picture))===JSON.stringify(facts)", "true");
    expectJs(h.runtime,
      "let setterCalls=0;for(const key of ['empty','width','height'])"
      "Object.defineProperty(Object.prototype,key,{set(){setterCalls++},configurable:true});"
      "let sealed=a.test.imageFacts(picture);"
      "for(const key of ['empty','width','height'])delete Object.prototype[key];"
      "[setterCalls,Object.isFrozen(sealed),Object.keys(sealed).sort().join(','),"
      "sealed.empty,sealed.width,sealed.height].join(':')", "0:true:empty,height,width:true:0:0");
    expectJs(h.runtime,
      "['chosen','flag','missing',{},null].every(v=>{try{a.test.imageFacts(v);return false}"
      "catch(e){return e instanceof TypeError}})", "true");
    expectJs(h.runtime, "run.run();activeRefused", "true");
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_SECOND, h.runtime));
      NullWindow window(&h.context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      admission.flush();
      // Both owners are Live; equal empty facts must not mask foreign authority.
      expectJs(h.runtime,
        "[[a,other],[b,picture]].every(([c,s])=>{try{c.test.imageFacts(s);return false}"
        "catch(e){return e instanceof TypeError}})", "true");
    }
    expectJs(h.runtime, "a.go('second');'ok'", "ok");
    h.admission->flush();
    expectJs(h.runtime,
      "let refused=false;try{a.test.imageFacts(picture)}catch(e){refused=e instanceof TypeError}refused",
      "true");
  }

  void checkImageFactsLookupNavigation()
  {
    CardRunnerHarness h(
      "var saved;card('first',c=>{saved=c;const picture=c.state.image();"
      "return new Proxy({picture,compose(){return Text('proxy')}},{"
      "getOwnPropertyDescriptor(t,k){if(k==='picture')c.go('second');"
      "return Reflect.getOwnPropertyDescriptor(t,k)}})});"
      "card('second',c=>({compose(){return Text('second')}}));",
      "scenario('first',c=>c.run(Flow().step(()=>{})));");
    h.mount();
    expectJs(h.runtime,
      "let refused=false;try{saved.test.imageFacts('picture')}"
      "catch(e){refused=e instanceof TypeError}refused", "true");
    h.admission->flush();
  }

  void checkViewerNavigation()
  {
    const char *directory = "_smirkycard_viewer_nav_fixture";
    const char *mainPath = "_smirkycard_viewer_nav_fixture/MAIN.JS";
    const char *viewerPath = "_smirkycard_viewer_nav_fixture/VIEWER.JS";
    std::remove(mainPath);
    std::remove(viewerPath);
    removeDirectory(directory);
    LOKA_VERIFY(makeDirectory(directory));
    writeMain(mainPath, readCardSource("MAIN.JS"));
    writeMain(viewerPath, readCardSource("VIEWER.JS"));
    NullPlatformContext context;
    context.setApplicationDirectory(loka::core::String::Literal(directory));
    smirkycard::ScriptRuntime runtime;
    runtime.loadMain(&context);
    {
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      // Card One reaches the viewer card, and the viewer card returns.
      clickCardButton(window, "SmirkyCard.OpenViewer");
      admission.flush();
      loka::app::scene::Node *open = windowNode(window, "Viewer.Open");
      LOKA_VERIFY(open && open->asButtonNode() && open->asButtonNode()->props.getText());
      // #1076: the Toolbox text path garbles non-ASCII on System 7, so the label stays ASCII.
      const loka::core::StringBuffer label =
          open->asButtonNode()->props.getText()->get().bufferWithEncoding(loka::core::StringEncodingUtf8);
      LOKA_VERIFY(label.length() > 0);
      for (std::size_t i = 0; i < label.length(); ++i)
        LOKA_VERIFY(static_cast<const unsigned char *>(label.data())[i] < 0x80);
      LOKA_VERIFY(!windowNode(window, "SmirkyCard.OpenViewer"));
      clickCardButton(window, "SmirkyCard.OpenMain");
      admission.flush();
      LOKA_VERIFY(windowNode(window, "SmirkyCard.OpenViewer"));
      LOKA_VERIFY(!windowNode(window, "Viewer.Open"));
    }
    std::remove(mainPath);
    std::remove(viewerPath);
    removeDirectory(directory);
  }

  void checkMinesScenario()
  {
    // Pin that the runner ignores the production seed, using checkMines' known stream.
    const std::string source = "globalThis.SMIRKY_SEED = 0x13579BDF;\n" + readCardSource("MINES.JS");
    const std::string companion = readCardSource("MINES.FLOW.JS");
    CardRunnerHarness h(source.c_str(), companion.c_str(), SMIRKYCARD_SCENARIO_SEED, true);
    h.mount();
    h.ticks(20);
    LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
    const char *names[] = {"initial-status", "flag-cell", "reveal-mode", "flood-fill", "summary"};
    LOKA_VERIFY(h.steps.size() == 10);
    for (size_t i = 0; i < 5; ++i)
      LOKA_VERIFY(h.steps[2 * i].name() == names[i]);
  }

  class StandaloneCardTestApp : public NullApp
  {
  public:
    explicit StandaloneCardTestApp(AppConfigurable *config) : NullApp(config) {}
    Window *window() { return this->group_->getComponents()[0]->asWindow(); }
  };

  void checkStandaloneScenario()
  {
    NullPlatformContext context;
    const loka::platform::file::FileHandle file = scenarioAuditDestination();
    SmirkyCardStandaloneFlowAppConfig config(&context, &file);
    LOKA_VERIFY(config.exitCode() == 0);
    StandaloneCardTestApp app(&config);
    config.setApp(&app);
    app.run();
    LOKA_VERIFY(resolveWindowContentMargin(app.window(), loka::app::layout::RailSpacing(12, 4, 9)) == 0);
    app.setActiveWindow(app.window());
    loka::dsl::testing::SceneTestAccess::updateAttached(*app.window()->scene(), true);
    loka::app::testing::AppTestAccess::flushWindowInvalidations(app);
    for (int i = 0; i < 20 && !app.quitRequested(); ++i)
      app.handleIdle(0.1);
    LOKA_VERIFY(app.quitRequested() && config.exitCode() == 0);
    const std::string audit = readScenarioAudit();
    char seed[40];
    std::sprintf(seed, "log text=seed%%3D%lu\n", static_cast<unsigned long>(SMIRKYCARD_SCENARIO_SEED));
    LOKA_VERIFY(audit.find(seed) != std::string::npos);
    LOKA_VERIFY(audit.find(seed) < audit.find("name=initial-status"));
    LOKA_VERIFY(audit.find("terminal status=succeeded\n") != std::string::npos);
    LOKA_VERIFY(audit == readCardSource("tests/MINES.audit"));
  }

  void checkProductionFlowCase(const char *name,
                               const char *declaration,
                               const char *operation,
                               const char *expected,
                               int detach = 0,
                               int activationFailure = 0)
  {
    std::fprintf(stderr, "[pin] production Flow %s\n", name);
    if (activationFailure)
      loka::core::testing::failLokaAllocRaw("CardFlow", "Subscription", activationFailure);
    {
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      std::string source = "var c0,s,f,g,log=[],n=0,result;card('first',c=>{c0=c;s=c.state(0);";
      source += declaration;
      source += ";return {compose(){return Text('flow')},onDetach(){if(typeof "
                "watchCount==='function')result=watchCount();s.set(9);log.push('detached')}}});"
                "card('second',c=>({compose(){return Text('second')}}));";
      LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
      NullPlatformContext context;
      NullScenePlatformController platform;
      WindowProps props;
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      smirkycard::testing::CardFlowAccess::scene = window.scene();
      JSValue globals = JS_GetGlobalObject(runtime.context());
      LOKA_VERIFY(
          JS_SetPropertyStr(
              runtime.context(),
              globals,
              "projection",
              JS_NewCFunction(runtime.context(), smirkycard::testing::CardFlowAccess::projection, "projection", 0))
          >= 0);
      JS_FreeValue(runtime.context(), globals);
      if (detach)
      {
        smirkycard::testing::CardFlowAccess::scene = window.scene();
        JSValue global = JS_GetGlobalObject(runtime.context());
        LOKA_VERIFY(
            JS_SetPropertyStr(
                runtime.context(),
                global,
                "watchCount",
                JS_NewCFunction(runtime.context(), smirkycard::testing::CardFlowAccess::watchCount, "watchCount", 0))
            >= 0);
        JS_FreeValue(runtime.context(), global);
        expectJs(runtime, "watchCount()", "1");
        // Isolate the entrance: the normal traversal also withdraws callbacks in
        // beginComposition, which otherwise masks a missing entrance withdrawal.
        if (detach == 1)
        {
          smirkycard::testing::CardFlowAccess::detachEntrance();
          expectJs(runtime, "result", "0");
        }
        else
          loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(
              loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()));
      }
      expectJs(runtime, operation, expected);
      LOKA_VERIFY(!JS_HasException(runtime.context()));
    }
    if (activationFailure)
      loka::core::testing::allowLokaAllocRaw();
  }

  void checkProductionFlowNesting()
  {
    checkProductionFlowCase("other Flow run is refused",
                            "g=c.flow(Flow().step(v=>n++));f=c.flow(Flow().step(v=>{result=g.run(v);return v}))",
                            "f.run(1);[result,n].join(':')",
                            "false:0");
#if !defined(_WIN32)
    // The debug contract must abort, after emitting its diagnostic. Release
    // executes the same pin and proves the dropped completion never replays.
    FILE *diagnostic = std::tmpfile();
    LOKA_VERIFY(diagnostic);
    std::fflush(0);
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      LOKA_VERIFY(dup2(fileno(diagnostic), STDERR_FILENO) >= 0);
      checkProductionFlowCase(
          "nested completion refuses before adapter",
          "g=c.flow(Flow().watch(s,v=>{n++;return v}));f=c.flow(Flow().step(v=>{s.set(v);return v}))",
          "f.run(1);n",
          "0");
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
#ifndef NDEBUG
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    LOKA_VERIFY(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
    std::rewind(diagnostic);
    char bytes[4096];
    const size_t length = std::fread(bytes, 1, sizeof(bytes), diagnostic);
    std::fclose(diagnostic);
    LOKA_VERIFY(std::string(bytes, length).find("CardFlow: nested watch firing dropped (stage 2a)")
                != std::string::npos);
#endif
  }

  void checkProductionFlowForeignSeat()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var foreign,refused=false;card('first',c=>{foreign=c.state(1);return {compose(){return Text('first')}}});"
        "card('second',c=>{try{c.flow(Flow().watch(foreign,v=>v))}catch(e){refused=e instanceof TypeError}"
        "return {compose(){return Text('second')}}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps firstProps;
    firstProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow first(&context, firstProps, &platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*first.scene(), true);
    WindowProps secondProps;
    secondProps.scene(smirkycard::CreateCard(SMIRKY_CARD_SECOND, runtime));
    NullWindow second(&context, secondProps, &platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*second.scene(), true);
    expectJs(runtime, "refused", "true");
  }

  JSValue failFlowDiagnostic(JSContext *ctx, JSValueConst, int, JSValueConst *argv)
  {
    // Refuse allocation during exception capture and diagnostic argument creation.
    JS_SetMemoryLimit(JS_GetRuntime(ctx), 1);
    return JS_Throw(ctx, JS_DupValue(ctx, argv[0]));
  }

  void checkProductionFlowDiagnosticAllocationFailure()
  {
    std::fprintf(stderr, "[pin] production Flow diagnostic allocation failure\n");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var c0,f,diagnostic='x'.repeat(100),failures=0,failureUndefined=false;card('first',c=>{c0=c;"
        "f=c.flow(Flow().step(()=>failDiagnostic(diagnostic)).onFailure(e=>{failures++;failureUndefined=e===undefined}));"
        "return {compose(){return Text('flow')}}});", error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    JSContext *ctx = runtime.context();
    JSValue global = JS_GetGlobalObject(ctx);
    LOKA_VERIFY(JS_SetPropertyStr(ctx, global, "failDiagnostic",
                                 JS_NewCFunction(ctx, failFlowDiagnostic, "failDiagnostic", 0)) >= 0);
    JSValue run = JS_Eval(ctx, "()=>f.run()", 10, "<pin>", JS_EVAL_TYPE_GLOBAL);
    LOKA_VERIFY(!JS_IsException(run));
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(JS_GetRuntime(ctx), &usage);
    JSValue result = JS_Call(ctx, run, JS_UNDEFINED, 0, 0);
    const bool pending = JS_HasException(ctx);
    JS_SetMemoryLimit(JS_GetRuntime(ctx), static_cast<size_t>(usage.malloc_limit));
    LOKA_VERIFY(JS_IsBool(result) && JS_ToBool(ctx, result));
    LOKA_VERIFY(!pending);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, run);
    JS_FreeValue(ctx, global);
    expectJs(runtime, "failures+':'+failureUndefined", "1:true");
  }

  void checkProductionFlowInputAllocationFailure()
  {
    std::fprintf(stderr, "[pin] production Flow input allocation failure\n");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var c0,f,steps=0,successes=0,failures=0,failureUndefined=false,again;"
        "card('first',c=>{c0=c;let s=c.state('');"
        "f=c.flow(Flow().watch(s,v=>{steps++;return v}).step(v=>{steps++;return v})"
        ".onSuccess(v=>successes++).onFailure(e=>{failures++;failureUndefined=e===undefined;again=f.run()}));"
        "return {compose(){return Text('flow')}}});", error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    smirkycard::testing::CardFlowAccess::scene = window.scene();
    JSRuntime *rt = runtime.currentEngine()->jsRuntime();
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(rt, &usage);
    const std::string longInput(1024 * 1024, 'x');
    // Leave room for the failure callback, but not the one-megabyte seat read.
    JS_SetMemoryLimit(rt, static_cast<size_t>(usage.malloc_size) + 4096);
    smirkycard::testing::CardFlowAccess::writeString(loka::core::String::Literal(longInput.c_str()));
    const bool pending = JS_HasException(runtime.context());
    JS_SetMemoryLimit(rt, static_cast<size_t>(usage.malloc_limit));
    expectJs(runtime, "steps", "0");
    expectJs(runtime, "successes", "0");
    expectJs(runtime, "failures+':'+failureUndefined+':'+again", "1:true:false");
    LOKA_VERIFY(!pending);
    expectJs(runtime, "c0.error.get()", "Flow input could not be read (out of memory)");
  }

  void checkProductionFlowWithoutWindow()
  {
    std::fprintf(stderr, "[pin] production Flow watch/run without Window\n");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var s,result,f,constructing;card('first',c=>{s=c.state(0);result=c.state(0);"
        "f=c.flow(Flow().watch(s,v=>v+1).step(v=>v*2).onSuccess(v=>result.set(v)));"
        "constructing=f.run(7);return {compose(){return Text('flow')}}});",
        error));
    NullScenePlatformController platform;
    smirkycard::CardScene *scene = smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime);
    LOKA_VERIFY(scene != 0);
    LOKA_VERIFY(scene->mount(&platform));
    loka::dsl::testing::SceneTestAccess::updateAttached(*scene, true);
    expectJs(runtime, "constructing", "false");
    expectJs(runtime, "result.get()", "0");
    expectJs(runtime, "s.set(3);result.get()", "8");
    expectJs(runtime, "f.run(5)", "true");
    expectJs(runtime, "result.get()", "10");
    delete scene;
    expectJs(runtime, "f.run(9)", "false");
  }

  void checkProductionFlowBeforeComposition()
  {
    std::fprintf(stderr, "[pin] production Flow constructor exception formatter refuses run\n");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var f,admitted,n=0;card('first',c=>{f=c.flow(Flow().step(v=>n++));"
        "throw {toString(){admitted=f.run(7);return 'constructor failed'}}});",
        error));
    NullScenePlatformController platform;
    smirkycard::CardScene *scene = smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime);
    LOKA_VERIFY(scene != 0);
    LOKA_VERIFY(scene->mount(&platform));
    loka::dsl::testing::SceneTestAccess::updateAttached(*scene, true);
    expectJs(runtime, "admitted", "false");
    expectJs(runtime, "n", "0");
    delete scene;
  }

  void checkProductionFlowEdges()
  {
    checkProductionFlowWithoutWindow();
    checkProductionFlowBeforeComposition();
    checkProductionFlowInputAllocationFailure();
    checkProductionFlowDiagnosticAllocationFailure();
    checkProductionFlowNesting();
    checkProductionFlowForeignSeat();
    checkProductionFlowCase(
        "one transaction includes terminal notification",
        "f=c.flow(Flow().step(v=>{s.set(v);log.push(projection());return v}).onSuccess(v=>log.push(projection())))",
        "f.run(7);log.push(projection());log.join(',')",
        "0,0,7");
    checkProductionFlowCase(
        "own watch is dropped",
        "f=c.flow(Flow().watch(s,v=>{n++;return v}).step(v=>{s.set(v+1);return v}).onSuccess(v=>log.push(v)))",
        "s.set(2);[n,s.get(),log.join(',')].join(':')",
        "1:3:2");
    // An unchanged write still committed the seat and re-ran its watches and
    // Show seats; LAPTOP.JS's viewport Flow drew the list twice that way.
    checkProductionFlowCase("same-value integer seat write is silent",
                            "f=c.flow(Flow().watch(s,v=>{n++;return v}))",
                            "s.set(0);s.set(5);s.set(5);[n,s.get()].join(':')",
                            "1:5");
    checkProductionFlowCase("same-value boolean and string seat writes are silent",
                            "globalThis.b=c.state(false);globalThis.t=c.state('a');"
                            "f=c.flow(Flow().watch(b,v=>{n++;return v}));g=c.flow(Flow().watch(t,v=>{n+=10;return v}))",
                            "b.set(false);t.set('a');b.set(true);t.set('z');b.set(true);t.set('z');n",
                            "11");
    checkProductionFlowCase(
        "watch throws and failure runs once",
        "f=c.flow(Flow().watch(s,v=>{throw 'adapter'}).step(v=>log.push('bad')).onFailure(e=>{n++;result=f.run(8)}))",
        "s.set(1);[n,result,log.length].join(':')",
        "1:false:0");
    checkProductionFlowCase(
        "failure throws without reentry",
        "f=c.flow(Flow().step(v=>{throw 'step'}).onFailure(e=>{n++;result=f.run(8);throw 'terminal'}))",
        "f.run(1);[n,result,c0.error.get().includes('terminal')].join(':')",
        "1:false:true");
    checkProductionFlowCase("exception formatter throws without leaving an exception",
                            "f=c.flow(Flow().step(v=>{throw {toString(){throw 'format'}}}).onFailure(e=>n++))",
                            "f.run(1);n",
                            "1");
    checkProductionFlowCase(
        "failure door reopens after unwind",
        "f=c.flow(Flow().step(v=>{if(v<0)throw 'bad';return v}).onFailure(e=>n++).onSuccess(v=>log.push(v)))",
        "[f.run(-1),f.run(4),n,log[0]].join(':')",
        "true:true:1:4");
    checkProductionFlowCase("run-only and undefined value",
                            "f=c.flow(Flow().step(v=>typeof v).onSuccess(v=>log.push(v)))",
                            "[f.run(),log[0],Object.keys(f).join(','),typeof f.idle].join(':')",
                            "true:undefined:run:undefined");
    checkProductionFlowCase("last step navigation cancels success",
                            "f=c.flow(Flow().step(v=>{c.go('second');return 1}).onSuccess(v=>n++).onFailure(e=>n++))",
                            "f.run(0);n",
                            "0");
    checkProductionFlowCase("middle navigation cancels remaining steps",
                            "f=c.flow(Flow().step(v=>c.go('second')).step(v=>n++).onSuccess(v=>n++))",
                            "f.run(0);n",
                            "0");
    checkProductionFlowCase(
        "post-call gate precedes exception formatting",
        "f=c.flow(Flow().step(v=>{c.go('second');throw {toString(){n++;return 'bad'}}}).onFailure(e=>n++))",
        "f.run(0);n",
        "0");
    checkProductionFlowCase("Detaching withdraws before hook and revokes run",
                            "f=c.flow(Flow().watch(s,v=>{n++;return v}).onSuccess(v=>n++))",
                            "[n,log.join(','),f.run(1)].join(':')",
                            "0:detached:false",
                            true);
    checkProductionFlowCase("retirement-only withdrawal backstop",
                            "f=c.flow(Flow().watch(s,v=>{n++;return v}))",
                            "[watchCount(),f.run(1),n].join(':')",
                            "0:false:0",
                            2);
    checkProductionFlowCase("non-seat identity rejected",
                            "for (const v of [{},undefined,null,c.error]) "
                            "{try{c.flow(Flow().watch(v,x=>x))}catch(e){if(e instanceof TypeError)n++}}",
                            "n",
                            "4");
    checkProductionFlowCase(
        "frozen unique SKIP and builder",
        "f=c.flow(Flow().watch(s,v=>Flow.SKIP).onSuccess(v=>n++).onFailure(e=>n++))",
        "s.set(1);[n,Object.isFrozen(Flow),Object.isFrozen(Flow.SKIP),Flow.SKIP===Flow.SKIP].join(':')",
        "0:true:true:true");
    checkProductionFlowCase("consumed description refuses reuse and mutation",
                            "let d=Flow().step(v=>v);f=c.flow(d);try{c.flow(d)}catch(e){if(e instanceof TypeError)n++}"
                            "try{d.step(v=>v)}catch(e){if(e instanceof TypeError)n++}",
                            "n",
                            "2");
    checkProductionFlowCase("activation allocation failure refuses the card",
                            "f=c.flow(Flow().watch(s,v=>{n++;return v}));g=c.flow(Flow().watch(s,v=>{n++;return v}))",
                            "[c0.error.get().includes('activate'),f.run(1),g.run(1),n].join(':')",
                            "true:false:false:0",
                            false,
                            2);
    checkProductionFlowCase(
        "per-card flow budget",
        "for(let i=0;i<33;i++){try{c.flow(Flow().step(v=>v))}catch(e){if(e instanceof RangeError)n++}}",
        "n",
        "1");
    checkProductionFlowCase(
        "per-flow step budget",
        "let d=Flow();for(let i=0;i<129;i++)d.step(v=>v);try{c.flow(d)}catch(e){if(e instanceof RangeError)n++}",
        "n",
        "1");
  }

  class HandlePlatform : public NullPlatformContext
  {
  public:
    enum Mode { Success, ReadFailure, DecodeFailure, CapacityFailure, Navigate, ReleaseProbe, NoNativeWork };
    explicit HandlePlatform(Mode m) : mode(m), opens(0), decodes(0), capacityCalls(0), releases(0) {}
    virtual bool openFile(const loka::file::File &file, loka::platform::file::FileHandle &out) const
    {
      ++opens;
      out = loka::platform::file::FileHandle();
      if (file.base() == loka::file::File::BASE_REFUSED) return false;
      const unsigned char *bytes = 0;
      std::size_t size = 0;
      if (loka::platform::file::FileLocatorAccess::query(file, bytes, size))
      {
        if (size != 1 || (bytes[0] != 1 && bytes[0] != 2)) return false;
        out.displayPath = loka::core::String::Literal(mode == ReadFailure ? "_missing_handle_file"
            : bytes[0] == 1 ? "_handle_image.bin" : "_handle_other.bin");
      }
      else
      {
        // Existing app-relative viewer fixture and ordinary path reads.
        const loka::core::String path = file.base() == loka::file::File::BASE_APPLICATION
            ? file.relativePath() : file.toString();
        if (!path.equals(loka::core::String::Literal("Sun.pict"))
            && !path.equals(loka::core::String::Literal("_handle_image.bin"))) return false;
        out.displayPath = loka::core::String::Literal("_handle_image.bin");
      }
      return true;
    }
    virtual bool queryLargestContiguousAllocation(std::size_t &out) const
    {
      ++capacityCalls;
      out = mode == CapacityFailure ? 0 : 1024;
      return true;
    }
    static void release(void *, void *data)
    {
      HandlePlatform *self = static_cast<HandlePlatform *>(data);
      ++self->releases;
      if (self->mode == ReleaseProbe)
        LOKA_VERIFY(!smirkycard::testing::CardFlowAccess::active());
    }
    virtual bool createImageFromBlob(const loka::core::resource::Blob &blob, std::size_t offset,
                                    std::size_t length, loka::core::resource::Image &out)
    {
      ++decodes;
      LOKA_VERIFY(offset == 0 && length == 3 && blob.size() == 3);
      if (mode == DecodeFailure)
        return false;
      out = loka::core::resource::Image::FromNative(const_cast<HandlePlatform *>(this), blob.data()[0] == 't' ? 4 : 2, 3, release,
                                                   const_cast<HandlePlatform *>(this));
      if (mode == Navigate)
        smirkycard::testing::CardFlowAccess::navigate();
      return true;
    }
    Mode mode;
    mutable int opens, decodes, capacityCalls, releases;
  };

  // Null has no image decoder. This fixture pins file bytes and ownership using
  // the same three-byte decoder-double shape as HandlePlatform and VIEWER.
  class NamedPicturePlatform : public HandlePlatform
  {
  public:
    NamedPicturePlatform()
        : HandlePlatform(Success)
    {
    }
    virtual bool openFile(const loka::file::File &file, loka::platform::file::FileHandle &out) const
    {
      ++this->opens;
      LOKA_VERIFY(file.base() == loka::file::File::BASE_APPLICATION);
      LOKA_VERIFY(file.relativePath().equals(loka::core::String::Literal("Pictures/") + file.name()));
      out.displayPath = loka::core::String::Literal("_named_pictures/") + file.name();
      return true;
    }
    virtual bool createImageFromBlob(const loka::core::resource::Blob &blob,
                                     std::size_t offset,
                                     std::size_t length,
                                     loka::core::resource::Image &out)
    {
      if (length != 3 || std::memcmp(blob.data(), "img", 3))
        return false;
      return HandlePlatform::createImageFromBlob(blob, offset, length, out);
    }
  };

  void checkNamedPictures()
  {
    LOKA_VERIFY(makeDirectory("_named_pictures"));
    writeMain("_named_pictures/PB100.PNG", "img");
    writeMain("_named_pictures/CLASSIC.PICT", "img");
    writeMain("_named_pictures/BROKEN.PNG", "garbage");
    NamedPicturePlatform context;
    {
      CardRunnerHarness h("let facts, budget=false; card('first',class {constructor(c){"
                          "this.picture=c.state.image('PB100');this.empty=c.state.image();"
                          "for(let i=0;i<126;i++)c.state.image();"
                          "try{c.state.image('PB100')}catch(e){budget=e instanceof RangeError}"
                          "facts=()=>[c.test.imageFacts(this.picture),c.test.imageFacts(this.empty),budget];}"
                          "compose(){return ImageView(this.picture).TEST_ID('Named.Picture')}});",
                          "scenario('first',c=>c.run(Flow().step(()=>{let f=facts();"
                          "if(f[0].empty||f[0].width!==2||f[0].height!==3||!f[1].empty||"
                          "f[1].width!==0||f[1].height!==0||!f[2])throw Error('facts')})))",
                          7,
                          true,
                          &context);
      h.mount();
      loka::app::scene::Node *picture = windowNode(*h.window, "Named.Picture");
      LOKA_VERIFY(picture && picture->asImageViewNode());
      h.ticks(10);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
      LOKA_VERIFY(context.opens == 1 && context.decodes == 1);
    }
    LOKA_VERIFY(context.releases == 1);
    const char *arguments[] = {"'NOPE'",
                               "'a/b'",
                               "'a:b'",
                               "'..'",
                               "'PB100.PNG'",
                               "'abcdefghijklmnopqrstuvwxyzA'",
                               "''",
                               "5",
                               "'BROKEN'",
                               "'CLASSIC'",
                               "'a\\\\b'",
                               "'a\\0b'",
                               "'PB100','extra'"};
    const char *messages[] = {"NOPE",
                              "a/b",
                              "a:b",
                              "..",
                              "PB100.PNG",
                              "abcdefghijklmnopqrstuvwxyzA",
                              "invalid picture name",
                              "optional picture name",
                              "BROKEN: image decode failure",
                              "CLASSIC",
                              "invalid picture name",
                              "invalid picture name",
                              "optional picture name"};
    for (std::size_t i = 0; i < sizeof(arguments) / sizeof(arguments[0]); ++i)
    {
      const std::string source = std::string("card('first',class {constructor(c){c.state.image(") + arguments[i]
                                 + ");}compose(){return Text('unexpected').TEST_ID('Unexpected')}});";
      CardRunnerHarness h(source.c_str(), "scenario('first',c=>{})", 7, true, &context);
      h.mount();
      loka::app::scene::Node *status = windowNode(*h.window, "SmirkyCard.Status");
      LOKA_VERIFY(status && textValue(status->asTextNode()).find(messages[i]) != std::string::npos);
      LOKA_VERIFY(textValue(status->asTextNode()).find("TypeError") != std::string::npos);
      LOKA_VERIFY(!windowNode(*h.window, "Unexpected"));
      smirkycard::testing::CardFlowAccess::scene = h.window->scene();
      LOKA_VERIFY(!smirkycard::testing::CardFlowAccess::imageState());
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::seatCount() == 0);
    }
    checkComposeRefusal("card('first',class {constructor(c){c.state.image('PB100')}"
                        "compose(){return Text('bad')}});",
                        "PB100: missing PlatformContext");
    LOKA_VERIFY(std::remove("_named_pictures/PB100.PNG") == 0);
    LOKA_VERIFY(std::remove("_named_pictures/CLASSIC.PICT") == 0);
    LOKA_VERIFY(std::remove("_named_pictures/BROKEN.PNG") == 0);
    removeDirectory("_named_pictures");
  }

  class ViewerPlatform : public HandlePlatform
  {
  public:
    ViewerPlatform() : HandlePlatform(Success) {}
    virtual bool createImageFromBlob(const loka::core::resource::Blob &blob, std::size_t offset,
                                    std::size_t length, loka::core::resource::Image &out)
    {
      ++this->decodes;
      LOKA_VERIFY(offset == 0 && length == 3 && blob.size() == 3);
      out = loka::core::resource::Image::FromNative(const_cast<ViewerPlatform *>(this), 256, 256,
                                                   release, const_cast<ViewerPlatform *>(this));
      return true;
    }
  };

  void checkViewerScenario()
  {
    writeMain("_handle_image.bin", "img");
    ViewerPlatform context;
    const std::string source = readCardSource("VIEWER.JS");
    const std::string companion = readCardSource("VIEWER.FLOW.JS");
    {
      CardRunnerHarness h(source.c_str(), companion.c_str(), 7, true, &context);
      h.mount();
      h.ticks(20);
      LOKA_VERIFY(h.terminalCount == 1 && h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
      const char *logs[] = {"image.load ok", "image.width 256", "image.height 256", "cancel.picture.facts unchanged"};
      LOKA_VERIFY(h.logs.size() == 4);
      for (size_t i = 0; i < 4; ++i)
        LOKA_VERIFY(h.logs[i] == logs[i]);
      const char *names[] = {"initial-empty", "deliver-sun", "image-loaded", "deliver-cancel", "cancel-unchanged"};
      LOKA_VERIFY(h.steps.size() == 10);
      for (size_t i = 0; i < 5; ++i)
        LOKA_VERIFY(h.steps[2 * i].name() == names[i]);
      LOKA_VERIFY(context.opens == 1 && context.decodes == 1 && context.releases == 0);
      smirkycard::testing::CardFlowAccess::scene = h.window->scene();
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::imageState()->getRef().nativeHandle() == &context);
      const std::string audit = readScenarioAudit();
      LOKA_VERIFY(audit.find("log text=image.load%20ok\n") != std::string::npos);
      LOKA_VERIFY(audit.find("log text=image.width%20256\n") != std::string::npos);
      LOKA_VERIFY(audit.find("log text=image.height%20256\n") != std::string::npos);
      LOKA_VERIFY(audit.find("log text=cancel.picture.facts%20unchanged\n") != std::string::npos);
      LOKA_VERIFY(audit.find("terminal status=succeeded\n") != std::string::npos);
    }
    LOKA_VERIFY(context.releases == 1);
    LOKA_VERIFY(std::remove("_handle_image.bin") == 0);
  }

  loka::file::File HandleChoice(unsigned char identity)
  {
    loka::file::File file;
    LOKA_VERIFY(loka::platform::file::FileLocatorAccess::capture(
        loka::core::String::Literal("_handle_image.bin"), loka::file::File::KIND_FILE,
        &identity, 1, file));
    return file;
  }

  void checkHandleCase(const char *name, const char *declaration, const char *operation, const char *expected,
                       HandlePlatform::Mode mode = HandlePlatform::Success, bool contextMissing = false,
                       bool exhaust = false, bool details = false)
  {
    BlobAllocationProbe allocation;
    std::fprintf(stderr, "[pin] handles %s\n", name);
    FILE *file = std::fopen("_handle_image.bin", "wb");
    LOKA_VERIFY(file);
    LOKA_VERIFY(std::fwrite("img", 1, 3, file) == 3);
    LOKA_VERIFY(std::fclose(file) == 0);
    HandlePlatform context(mode);
    smirkycard::ScriptRuntime runtime;
    if (exhaust)
      smirkycard::testing::ExecutionSerialAccess::seed(runtime, UINT32_MAX - 1);
    if (!contextMissing)
      runtime.loadMain(&context);
    context.opens = context.decodes = context.capacityCalls = 0;
    loka::core::String error;
    std::string source = "var c0,fs,im,f,g,saved,log=[],work=v=>v;"
        "function refuses(fn){try{fn();return false}catch(e){return e instanceof TypeError}}"
        "card('first',c=>{c0=c;fs=c.state.file();im=c.state.image();";
    source += declaration;
    source += ";return {compose(){return Text('handles')},onDetach(){log.push(refuses(()=>c.native.loadImage(saved)))}}});"
              "card('second',c=>({compose(){return Text('second')}}));";
    LOKA_VERIFY(runtime.loadBuiltin(source.c_str(), error));
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    smirkycard::testing::CardFlowAccess::scene = window.scene();
    smirkycard::testing::CardFlowAccess::writeFile(loka::app::FileChooserResult::File(HandleChoice(1)));
    if (mode == HandlePlatform::CapacityFailure) allocation.refuse("Blob", "Bytes");
    expectJs(runtime, operation, expected);
    if (mode == HandlePlatform::NoNativeWork)
      LOKA_VERIFY(context.opens == 0 && context.decodes == 0);
    if (mode == HandlePlatform::CapacityFailure)
    {
      LOKA_VERIFY(allocation.refusals == 1 && context.capacityCalls == 0 && context.decodes == 0);
    }
    if (mode == HandlePlatform::ReleaseProbe || mode == HandlePlatform::Navigate)
      LOKA_VERIFY(context.releases == 1);
    if (details)
    {
      using loka::app::FileChooserResult;
      const FileChooserResult kinds[] = {FileChooserResult(),
        FileChooserResult::File(HandleChoice(1)),
        FileChooserResult::Folder(loka::file::File::FromPath("folder")),
        FileChooserResult::Canceled(), FileChooserResult::Error(17)};
      const char *facts[] = {"0:false:false:false:true", "1:true:false:false:true",
                            "2:false:false:false:true", "3:false:true:false:true", "4:false:false:true:true"};
      for (unsigned i=0; i<5; ++i)
      {
        smirkycard::testing::CardFlowAccess::writeFile(kinds[i]);
        expectJs(runtime, "(()=>{let r=fs.get();return [r.kind,r.isFile,r.isCanceled,r.isError,r.file===null].join(':')})()", facts[i]);
      }
      smirkycard::testing::CardFlowAccess::writeFile(kinds[1]);
      expectJs(runtime, "work=()=>{saved=c0.native.loadImage(fs.get().file);im.set(saved)};f.run();'ok'", "ok");
      writeMain("_handle_other.bin", "two");
      const loka::file::File first = HandleChoice(1), second = HandleChoice(2);
      LOKA_VERIFY(first.toString().equals(second.toString()) && first != second);
      smirkycard::testing::CardFlowAccess::writeFile(FileChooserResult::File(second));
      expectJs(runtime, "f.run();'ok'", "ok");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::image().width() == 4);
      smirkycard::testing::CardFlowAccess::writeFile(FileChooserResult::File(first));
      expectJs(runtime, "f.run();'ok'", "ok");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::image().width() == 2);
      // A readable same-display decoy must not rescue failed resolution/open.
      smirkycard::testing::CardFlowAccess::writeFile(FileChooserResult::File(HandleChoice(3)));
      expectJs(runtime, "work=()=>log.push(refuses(()=>c0.native.loadImage(fs.get().file)));f.run();log.pop()", "true");
      smirkycard::testing::CardFlowAccess::writeFile(FileChooserResult::File(first));
      LOKA_VERIFY(std::remove("_handle_other.bin") == 0);
      const loka::core::resource::Image prior = smirkycard::testing::CardFlowAccess::image();
      expectJs(runtime, "work=()=>log.push(refuses(()=>im.set(fs.get().file)));f.run();log.pop()", "true");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::image() == prior);
      context.mode = HandlePlatform::ReadFailure;
      expectJs(runtime, "work=()=>log.push(refuses(()=>im.set(c0.native.loadImage(fs.get().file))));f.run();log.pop()", "true");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::image() == prior);
      context.mode = HandlePlatform::DecodeFailure;
      expectJs(runtime, "f.run();log.pop()", "true");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::image() == prior);
      context.mode = HandlePlatform::Success;
      const int opens = context.opens;
      expectJs(runtime, "work=()=>{saved=fs.get().file;fs.get();fs.get();fs.get();try{c0.native.loadImage(saved)}catch(e){log.push(e instanceof RangeError)}};f.run();log.pop()", "true");
      LOKA_VERIFY(context.opens == opens);
      smirkycard::testing::CardFlowAccess::observeImage();
      expectJs(runtime, "work=()=>{saved=im.get();im.set(saved);im.set(saved)};f.run();'ok'", "ok");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::notifications == 2);
      expectJs(runtime, "im.set(null);im.set(null);'ok'", "ok");
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::notifications == 4);
      smirkycard::testing::CardFlowAccess::unobserveImage();
      // An enclosing transaction settles only after the Flow's stack owner ends.
      {
        loka::core::StateTrackerGuard outer(smirkycard::testing::CardFlowAccess::card()->tracker());
        smirkycard::testing::CardFlowAccess::observeSettlement(runtime);
        expectJs(runtime, "work=()=>{saved=c0.native.loadImage(fs.get().file);im.set(saved)};f.run();'ok'", "ok");
      }
      LOKA_VERIFY(smirkycard::testing::CardFlowAccess::settledStale);
      smirkycard::testing::CardFlowAccess::unobserveImage();
      expectJs(runtime, "globalThis.a={c:c0,fs,im,f};'ok'", "ok");
      WindowProps otherProps;
      otherProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow other(&context, otherProps, &platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(*other.scene(), true);
      expectJs(runtime,
        "log=[];work=()=>{saved=a.c.native.loadImage(a.fs.get().file);"
        "log.push(refuses(()=>a.im.set.call(im,saved)));"
        "work=v=>log.push(refuses(()=>im.set(v)));f.run(saved)};a.f.run();log.join(':')", "true:true");
      // Detach while an execution still owns a File handle; capability is still bound.
      JSValue global = JS_GetGlobalObject(runtime.context());
      LOKA_VERIFY(JS_SetPropertyStr(runtime.context(), global, "detachNow", JS_NewCFunction(runtime.context(),
        smirkycard::testing::CardFlowAccess::detachCall, "detachNow", 0)) >= 0);
      JS_FreeValue(runtime.context(), global);
      const int beforeDetach = context.opens;
      expectJs(runtime, "log=[];work=()=>{saved=a.fs.get().file;detachNow()};a.f.run();log.join(':')", "true");
      LOKA_VERIFY(context.opens == beforeDetach);
    }
    std::remove("_handle_image.bin");
  }

  void settleLowering(NullWindow &window)
  {
    if (window.scene()->hasPendingInvalidation())
      (void)window.scene()->flushInvalidation();
  }

  void checkMinimumLowerings()
  {
    std::fputs("[pin] H8 Show, chooser completion, ImageView identity and A10\n", stderr);
    FILE *file = std::fopen("_handle_image.bin", "wb");
    LOKA_VERIFY(file);
    LOKA_VERIFY(std::fwrite("img", 1, 3, file) == 3);
    LOKA_VERIFY(std::fclose(file) == 0);
    HandlePlatform context(HandlePlatform::Success);
    smirkycard::ScriptRuntime runtime;
    runtime.loadMain(&context);
    context.opens = context.decodes = 0;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
      "var c0,shown,fs,picture,inside,log=[];card('first',c=>{c0=c;"
      "shown=c.state(false);fs=c.state.file();picture=c.state.image();"
      "c.flow(Flow().watch(fs,r=>{if(r.kind===0)return Flow.SKIP;"
      "shown.set(false);log.push('closed:'+shown.get());"
      "if(r.isCanceled)return Flow.SKIP;if(r.isError)throw Error('chooser');return r.file})"
      ".step(h=>{log.push('load:'+shown.get());picture.set(c.native.loadImage(h))})"
      ".onSuccess(()=>log.push('ok')).onFailure(()=>log.push('failed')));"
      "inside=c.flow(Flow().step(()=>shown.set(true)));"
      "return {compose(){return VStack(Button('open',()=>shown.set(true)).TEST_ID('Open'),"
      "Button('hide',()=>shown.set(false)).TEST_ID('Hide'),"
      "Show(shown,VStack(Text('child').TEST_ID('Child'),OpenFileDialog(fs).TEST_ID('Chooser'))),"
      "ImageView(picture).TEST_ID('Picture'))}}});", error));
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    smirkycard::testing::CardFlowAccess::scene = window.scene();
    LOKA_VERIFY(!windowNode(window, "Child") && !windowNode(window, "Chooser"));
    loka::app::scene::Node *imageNode = windowNode(window, "Picture");
    LOKA_VERIFY(imageNode && imageNode->asImageViewNode());
    loka::app::ImageViewNode *picture = imageNode->asImageViewNode();
    LOKA_VERIFY(picture->props.image_ == smirkycard::testing::CardFlowAccess::imageState());
    LOKA_VERIFY(!picture->props.image_->get().isValid());
    clickCardButton(window, "Open");
    settleLowering(window);
    LOKA_VERIFY(windowNode(window, "Child") && windowNode(window, "Chooser"));
    clickCardButton(window, "Hide");
    settleLowering(window);
    LOKA_VERIFY(!windowNode(window, "Child") && !windowNode(window, "Chooser"));

    using loka::app::FileChooserResult;
    const FileChooserResult outcomes[] = {
      FileChooserResult::File(loka::file::File::FromPath("_handle_image.bin")),
      FileChooserResult::File(loka::file::File::FromPath("_handle_image.bin")),
      FileChooserResult::Canceled(), FileChooserResult::Error(7), FileChooserResult::Canceled()};
    for (unsigned i = 0; i < 5; ++i)
    {
      clickCardButton(window, "Open");
      settleLowering(window);
      loka::app::scene::Node *node = windowNode(window, "Chooser");
      LOKA_VERIFY(node && node->asOpenFileDialogNode() && windowNode(window, "Child"));
      loka::app::OpenFileDialogNode *dialog = node->asOpenFileDialogNode();
      LOKA_VERIFY(dialog->props.result_.isValid());
      // Mirror the rail: copy the result door before completion can detach it.
      loka::app::scene::NodeState<FileChooserResult> result = dialog->props.result_;
      {
        loka::core::StateTrackerGuard transaction(smirkycard::testing::CardFlowAccess::card()->tracker());
        result.set(outcomes[i], true);
      }
      settleLowering(window);
      LOKA_VERIFY(!windowNode(window, "Chooser") && !windowNode(window, "Child"));
      LOKA_VERIFY(context.opens == static_cast<int>(i < 2 ? i + 1 : 2));
      LOKA_VERIFY(context.decodes == context.opens);
      LOKA_VERIFY(picture->props.image_->get().isValid());
      LOKA_VERIFY(picture->props.image_->get() == smirkycard::testing::CardFlowAccess::image());
    }
    expectJs(runtime, "log.join(',')",
      "closed:false,load:false,ok,closed:false,load:false,ok,closed:false,closed:false,failed,closed:false");
    expectJs(runtime, "picture.set(null);'cleared'", "cleared");
    LOKA_VERIFY(!picture->props.image_->get().isValid());

    // Null has no native attach completion. This observation is not permission
    // to open inside a Flow on synchronous rails (A10); no replay is installed.
    expectJs(runtime, "log=[];inside.run();shown.get()", "true");
    settleLowering(window);
    LOKA_VERIFY(windowNode(window, "Chooser"));
    expectJs(runtime, "log.length", "0");
    smirkycard::testing::CardFlowAccess::writeFile(FileChooserResult::Canceled());
    settleLowering(window);
    LOKA_VERIFY(!windowNode(window, "Chooser"));
    expectJs(runtime, "log.join(',')", "closed:false");
    LOKA_VERIFY(std::remove("_handle_image.bin") == 0);
  }

  void checkViewportSeat()
  {
    std::puts("[pin] viewport seat: seed projection, frozen snapshots, rail-only writes and Null relayout");
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin(
        "var viewport,before,log=[],factoryRefused=false,earlySet=false;card('first',c=>{"
        "try{c.state.viewport(1)}catch(e){factoryRefused=e instanceof TypeError}"
        "viewport=c.state.viewport();"
        "try{viewport.set({})}catch(e){earlySet=e instanceof TypeError&&e.message==='viewport is a rail-written fact'}"
        "c.flow(Flow().watch(viewport,v=>{log.push(v);return v}));return {compose(){"
        "before=viewport.get();return ScrollView(Text('content'),{viewport}).TEST_ID('Viewport')}}});",
        error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    expectJs(runtime, "JSON.stringify(before)", "{\"x\":0,\"y\":0,\"width\":0,\"height\":0}");
    expectJs(runtime, "factoryRefused&&earlySet&&Object.isFrozen(before)", "true");
    expectJs(runtime, "JSON.stringify(log)", "[{\"x\":0,\"y\":0,\"width\":100,\"height\":20}]");
    expectJs(runtime, "Object.isFrozen(log[0])&&Object.isFrozen(viewport.get())", "true");
    expectJs(runtime,
             "(()=>{try{viewport.set({x:1,y:2,width:3,height:4});return false}catch(e){"
             "return e instanceof TypeError&&e.message==='viewport is a rail-written fact'}})()",
             "true");
    loka::app::scene::Node *node = windowNode(window, "Viewport");
    LOKA_VERIFY(node && node->asScrollViewNode());
    const loka::core::Frame first = node->asScrollViewNode()->props.viewport_.state()->get();
    std::fprintf(stderr, "[probe] viewport first: %d,%d,%d,%d\n", first.x, first.y, first.width, first.height);
    loka::app::scene::LayoutState bounds;
    bounds.width = 240;
    bounds.height = 60;
    {
      loka::core::Operation operation;
      platform.projectLayoutForTesting(node, bounds);
    }
    const loka::core::Frame second = node->asScrollViewNode()->props.viewport_.state()->get();
    std::fprintf(stderr, "[probe] viewport second: %d,%d,%d,%d\n", second.x, second.y, second.width, second.height);
    expectJs(runtime,
             "JSON.stringify(log)",
             "[{\"x\":0,\"y\":0,\"width\":100,\"height\":20},{\"x\":0,\"y\":0,\"width\":240,\"height\":60}]");
    {
      loka::core::Operation operation;
      platform.projectLayoutForTesting(node, bounds);
    }
    expectJs(runtime, "log.length", "2");
    expectJs(runtime, "JSON.stringify(viewport.get())===JSON.stringify(log[1])&&Object.isFrozen(log[1])", "true");
    expectJs(runtime,
             "(()=>{let calls=0;Object.defineProperty(Object.prototype,'x',"
             "{set(){++calls},configurable:true});const v=viewport.get();delete Object.prototype.x;"
             "return calls===0&&Object.hasOwn(v,'x')&&v.x===0})()",
             "true");
    {
      smirkycard::ScriptRuntime insetRuntime;
      LOKA_VERIFY(
          insetRuntime.loadBuiltin("var previous,initial,seen=[];card('first',c=>{const v=c.state.viewport();"
                                   "const borrowed=previous;previous=v;c.flow(Flow().watch(v,x=>seen.push(x)));"
                                   "return {compose(){initial=v.get();return ScrollView(Text('gap'),"
                                   "{viewport:borrowed||v,insets:{top:12,bottom:3}}).TEST_ID('InsetViewport')}}});",
                                   error));
      WindowProps insetProps;
      insetProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, insetRuntime));
      NullWindow insetWindow(&context, insetProps);
      WindowAdmissionTestApp insetAdmission(insetWindow);
      loka::dsl::testing::SceneTestAccess::updateAttached(*insetWindow.scene(), true);
      expectJs(insetRuntime, "JSON.stringify(initial)", "{\"x\":0,\"y\":0,\"width\":0,\"height\":0}");
      expectJs(insetRuntime, "JSON.stringify(previous.get())", "{\"x\":0,\"y\":-12,\"width\":100,\"height\":20}");
      expectJs(insetRuntime, "Object.isFrozen(seen[0])", "true");
      // A second card in the same engine cannot borrow the first card's seat.
      WindowProps foreignProps;
      foreignProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, insetRuntime));
      NullWindow foreignWindow(&context, foreignProps);
      WindowAdmissionTestApp foreignAdmission(foreignWindow);
      loka::dsl::testing::SceneTestAccess::updateAttached(*foreignWindow.scene(), true);
      loka::app::scene::Node *status = windowNode(foreignWindow, "SmirkyCard.Status");
      LOKA_VERIFY(status && status->asTextNode());
      LOKA_VERIFY(textValue(status->asTextNode())
                  == "JavaScript ScrollView viewport must be a c.state.viewport() seat of this card");
    }
    std::puts("[pin] viewport seat: own typed seat required");
    const char *invalid[] = {"c.state(0)", "c.state.file()", "c.state.image()", "{}", "null", "undefined"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
      const std::string source = std::string("card('first',c=>{const v=") + invalid[i]
                                 + ";return {compose(){return ScrollView(Text('a'),{viewport:v})}}});";
      checkComposeRefusal(source.c_str(),
                          "JavaScript ScrollView viewport must be a c.state.viewport() seat of this card");
    }
  }

  void checkScrollViewportPersistence()
  {
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',c=>{const visible=c.state(true);return {compose(){"
                                    "const rows=()=>VStack(VStack(Array.from({length:10},()=>Text('row'))),"
                                    "VStack(Array.from({length:10},()=>Text('row'))));return VStack("
                                    "ScrollView(rows()).TEST_ID('Browse'),ScrollView(rows()).TEST_ID('Detail'),"
                                    "ScrollView(Show(visible,Text('shown').TEST_ID('Shown'))).TEST_ID('Branch'))}}});",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    smirkycard::testing::CardFlowAccess::scene = window.scene();
    loka::app::scene::Node *browseNode = windowNode(window, "Browse");
    loka::app::scene::Node *detailNode = windowNode(window, "Detail");
    LOKA_VERIFY(browseNode && browseNode->asScrollViewNode());
    LOKA_VERIFY(detailNode && detailNode->asScrollViewNode());
    loka::app::ScrollViewNode *browse = browseNode->asScrollViewNode();
    loka::app::ScrollViewNode *detail = detailNode->asScrollViewNode();
    LOKA_VERIFY(browse->props.viewport_.isValid() && detail->props.viewport_.isValid());
    LOKA_VERIFY(browse->props.viewport_.state() != detail->props.viewport_.state());
    LOKA_VERIFY(browse->requestedOrigin().y == 0 && detail->requestedOrigin().y == 0);
    LOKA_VERIFY(windowNode(window, "Shown"));
    {
      loka::core::StateTrackerGuard transaction(smirkycard::testing::CardFlowAccess::card()->tracker());
      browse->publishOrigin(0, 40);
    }
    LOKA_VERIFY(browse->requestedOrigin().y == 40 && detail->requestedOrigin().y == 0);
    settleLowering(window);
    LOKA_VERIFY(browse->requestedOrigin().y == 40);
    {
      loka::core::StateTrackerGuard transaction(smirkycard::testing::CardFlowAccess::card()->tracker());
      detail->publishOrigin(0, 80);
    }
    settleLowering(window);
    LOKA_VERIFY(browse->requestedOrigin().y == 40 && detail->requestedOrigin().y == 80);
  }

  /** Answers what the three rails answer (ToolboxNaturalWidth.cpp, Win32 and
      macOS controllers): Text and Button labels and INTRINSIC images; a Cell
      has no natural width on any rail, so a card that puts its click target
      in a Cell gives a horizontal ScrollView no range. */
  class LaptopNaturalWidthSource : public loka::app::layout::INaturalWidthSource
  {
  public:
    virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const
    {
      if (!child || !(child->asTextNode() || child->asButtonNode()))
        return false;
      width = 10;
      return true;
    }
  };

  void checkNaturalRowLowering()
  {
    std::fputs("[pin] natural-width Row modifier and carousel overflow\n", stderr);
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',c=>{const pic=c.state.image();"
                                    "function cards(cell){return Row(...[0,1,2,3,4].map(i=>"
                                    "Card(VStack(ImageView(pic,{width:110,height:70}),"
                                    "cell?Cell('x',()=>{}):Button('x',()=>{})))))}"
                                    "return {compose(){return VStack("
                                    "Row(Text('a')).naturalWidth().TEST_ID('Natural'),"
                                    "Row(Text('a')).TEST_ID('Shared'),"
                                    "ScrollView(cards(false).naturalWidth()).TEST_ID('WideScroll'),"
                                    "ScrollView(cards(false)).TEST_ID('SharedScroll'),"
                                    "ScrollView(cards(true).naturalWidth()).TEST_ID('CellScroll'))}}});",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *natural = windowNode(window, "Natural");
    loka::app::scene::Node *shared = windowNode(window, "Shared");
    LOKA_VERIFY(natural && natural->asStackNode());
    LOKA_VERIFY(shared && shared->asStackNode());
    LOKA_VERIFY(natural->asStackNode()->props.rowUndeclaredWidth_ == loka::app::ROW_UNDECLARED_WIDTH_NATURAL);
    LOKA_VERIFY(shared->asStackNode()->props.rowUndeclaredWidth_ == loka::app::ROW_UNDECLARED_WIDTH_SHARED);
    expectJs(runtime,
             "(()=>{const original=Row(Text('a'));const tree=original.naturalWidth();"
             "return Object.isFrozen(original)&&Object.isFrozen(tree)&&tree!==original&&"
             "original.rowWidth===undefined&&tree.rowWidth===1&&tree.naturalWidth()===tree&&"
             "tree.TEST_ID('x').rowWidth===1&&Row(Text('a')).TEST_ID('y').naturalWidth().testId==='y'})()",
             "true");
    const char *wrongKinds[] = {"VStack(Text('a'))", "Card(Text('a'))"};
    for (unsigned i = 0; i < 2; ++i)
    {
      const std::string expression = std::string("(()=>{try{") + wrongKinds[i]
                                     + ".naturalWidth();return false}catch(e){return e instanceof TypeError&&"
                                       "e.message==='naturalWidth() applies to Row'}})()";
      expectJs(runtime, expression.c_str(), "true");
    }
    expectJs(runtime,
             "(()=>{try{Row(Text('a')).naturalWidth(1);return false}catch(e){return e instanceof TypeError}})()",
             "true");
    LaptopNaturalWidthSource source;
    const loka::app::layout::RailSpacing spacing(12, 4, 9);
    const int gap = spacing.horizontalGap();
    short width = 0;
    loka::app::scene::Node *scroll = windowNode(window, "WideScroll");
    LOKA_VERIFY(scroll && scroll->asScrollViewNode());
    LOKA_VERIFY(loka::app::layout::queryContentOverflowWidth(scroll, spacing, &source, width));
    const loka::app::CardStyle style = loka::app::StandardCardStyle();
    const loka::app::detail::CardShadowTable &geometry = style.geometry();
    const int insets = 2 * loka::app::CardStyle::kBorderBand + geometry.allowance[0] + geometry.allowance[2];
    LOKA_VERIFY(width == 5 * (110 + insets) + 4 * gap);
    scroll = windowNode(window, "SharedScroll");
    LOKA_VERIFY(scroll && scroll->asScrollViewNode());
    width = 0;
    LOKA_VERIFY(!loka::app::layout::queryContentOverflowWidth(scroll, spacing, &source, width) || width == 0);
    // A Cell inside the card declines on every rail, so the natural Row has no range
    // (the Classic run of the first candidate showed five cards squeezed into the viewport).
    scroll = windowNode(window, "CellScroll");
    LOKA_VERIFY(scroll && scroll->asScrollViewNode());
    width = 0;
    LOKA_VERIFY(!loka::app::layout::queryContentOverflowWidth(scroll, spacing, &source, width));
  }

  void checkNaturalColumnLowering()
  {
    std::fputs("[pin] natural-height VStack modifier and lowering\n", stderr);
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',c=>({compose(){return VStack(Box(Text('a'),{height:20}))"
                                    ".naturalHeight().TEST_ID('NaturalColumn')}}));",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    expectJs(runtime,
             "(()=>{const original=VStack(Text('a'));const tree=original.naturalHeight();"
             "return Object.isFrozen(original)&&Object.isFrozen(tree)&&tree!==original&&"
             "original.columnHeight===undefined&&tree.columnHeight===1&&tree.naturalHeight()===tree&&"
             "tree.TEST_ID('x').columnHeight===1&&VStack(Text('a')).TEST_ID('y').naturalHeight().testId==='y'})()",
             "true");
    expectJs(runtime,
             "(()=>{try{Row(Text('a')).naturalHeight();return false}catch(e){return e instanceof TypeError}})()",
             "true");
    expectJs(runtime,
             "(()=>{try{VStack(Text('a')).naturalWidth();return false}catch(e){return e instanceof TypeError}})()",
             "true");
    expectJs(runtime,
             "(()=>{try{VStack(Text('a')).naturalHeight(1);return false}catch(e){return e instanceof TypeError}})()",
             "true");
    loka::app::scene::Node *node = windowNode(window, "NaturalColumn");
    LOKA_VERIFY(node && node->asStackNode());
    LOKA_VERIFY(node->asStackNode()->props.columnUndeclaredHeight_ == loka::app::COLUMN_UNDECLARED_HEIGHT_NATURAL);
    // A Null filling surface discriminates the height offered by the lowered policy.
    node->asStackNode()->addChild(loka::app::RectSurface().create());
    loka::app::scene::LayoutState bounds;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = 200;
    bounds.height = 100;
    bounds.spacing = 4;
    bounds.lineHeight = 12;
    LOKA_VERIFY(platform.projectLayoutForTesting(node, bounds) == 32);
  }

  void checkZStackLowering()
  {
    smirkycard::ScriptRuntime methodRuntime;
    expectJs(
        methodRuntime,
        "(()=>{const original=ZStack(Text('g'));const tree=original.expStaticBlurUnit();"
        "return Object.isFrozen(tree)&&tree!==original&&original.staticBlurUnit===undefined&&"
        "tree.staticBlurUnit===1&&tree.expStaticBlurUnit()===tree&&"
        "tree.TEST_ID('x').staticBlurUnit===1&&ZStack(Text('g')).TEST_ID('y').expStaticBlurUnit().testId==='y'})()",
        "true");
    expectJs(methodRuntime,
             "(()=>{const original=ZStack(Text('g'));const tree=original.expBlurUnit();"
             "return Object.isFrozen(tree)&&tree!==original&&original.blurUnit===undefined&&"
             "tree.blurUnit===1&&tree.expBlurUnit()===tree&&"
             "tree.TEST_ID('x').blurUnit===1&&ZStack(Text('g')).TEST_ID('y').expBlurUnit().testId==='y'&&"
             "tree.expStaticBlurUnit().blurUnit===0&&tree.expStaticBlurUnit().expBlurUnit().staticBlurUnit===0})()",
             "true");
    expectJs(methodRuntime,
             "(()=>{try{ZStack(Text('g')).expBlurUnit(1);return false}catch(e){return e instanceof TypeError}})()",
             "true");
    const char *refused[] = {"Row(Text('x'))", "VStack(Text('x'))", "Text('x')", "Box(Text('x'))"};
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
      const std::string check = std::string("(()=>{try{") + refused[i]
                                + ".expStaticBlurUnit();return false}catch(e){return e instanceof TypeError&&"
                                  "e.message==='expStaticBlurUnit() applies to ZStack'}})()";
      expectJs(methodRuntime, check.c_str(), "true");
      const std::string liveCheck = std::string("(()=>{try{") + refused[i]
                                    + ".expBlurUnit();return false}catch(e){return e instanceof TypeError&&"
                                      "e.message==='expBlurUnit() applies to ZStack'}})()";
      expectJs(methodRuntime, liveCheck.c_str(), "true");
    }
    expectJs(
        methodRuntime,
        "(()=>{try{ZStack(Text('g')).expStaticBlurUnit(1);return false}catch(e){return e instanceof TypeError}})()",
        "true");
    const char *children[] = {"Text('g'),Text('u')", "[Text('g'),Text('u')]", "Text('g')"};
    const char *policies[] = {".expStaticBlurUnit()",
                              ".expBlurUnit()",
                              ".expStaticBlurUnit().expBlurUnit()",
                              ".expBlurUnit().expStaticBlurUnit()"};
    for (unsigned policy = 0; policy < 4; ++policy)
      for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); ++i)
      {
        smirkycard::ScriptRuntime runtime;
        loka::core::String error;
        const std::string script = std::string("card('first',class{compose(){return ZStack(") + children[i] + ")"
                                   + policies[policy] + ".TEST_ID('Depth')}});";
        LOKA_VERIFY(runtime.loadBuiltin(script.c_str(), error));
        NullPlatformContext context;
        NullScenePlatformController platform;
        WindowProps props;
        props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
        NullWindow window(&context, props, &platform);
        WindowAdmissionTestApp admission(window);
        loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
        loka::app::scene::Node *stack = find(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), "Depth");
        LOKA_VERIFY(stack && stack->kind() == loka::app::scene::NODE_KIND_ZSTACK);
        LOKA_VERIFY(stack->asZStackNode()->props.unitGround
                    == (policy == 0 || policy == 3 ? loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR
                                                   : loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR));
        LOKA_VERIFY(stack->asNestable());
        loka::app::scene::Node *ground = stack->asNestable()->childrenHead();
        LOKA_VERIFY(ground && ground->asTextNode());
        LOKA_VERIFY(ground->asTextNode()->props.text_->get().compare(loka::core::String::Literal("g")) == 0);
        loka::app::scene::Node *unit = ground->nextInComposition;
        if (i == 2)
          LOKA_VERIFY(!unit);
        else
        {
          LOKA_VERIFY(unit && unit->asTextNode() && !unit->nextInComposition);
          LOKA_VERIFY(unit->asTextNode()->props.text_->get().compare(loka::core::String::Literal("u")) == 0);
        }
      }
    // A policy placed on Object.prototype is not the tree's own claim: lowering
    // reads only the property the modifier wrote, for both tree policies.
    smirkycard::ScriptRuntime inherited;
    loka::core::String error;
    LOKA_VERIFY(inherited.loadBuiltin(
        "Object.prototype.staticBlurUnit=1;Object.prototype.blurUnit=1;Object.prototype.rowWidth=1;"
        "card('first',class{compose(){return VStack("
        "ZStack(Text('g'),Text('u')).TEST_ID('Depth'),"
        "Row(Text('a')).TEST_ID('Shared'),"
        "ZStack(Text('g'),Text('u')).expBlurUnit().TEST_ID('Live'),"
        "ZStack(Text('g'),Text('u')).expStaticBlurUnit().TEST_ID('Frozen'),"
        "Row(Text('a')).naturalWidth().TEST_ID('Natural'))}});",
        error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, inherited));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *stack = windowNode(window, "Depth");
    loka::app::scene::Node *row = windowNode(window, "Shared");
    LOKA_VERIFY(stack && stack->kind() == loka::app::scene::NODE_KIND_ZSTACK);
    LOKA_VERIFY(stack->asZStackNode()->props.unitGround == loka::app::ZSTACK_UNIT_GROUND_WINDOW);
    LOKA_VERIFY(row && row->asStackNode());
    LOKA_VERIFY(row->asStackNode()->props.rowUndeclaredWidth_ == loka::app::ROW_UNDECLARED_WIDTH_SHARED);
    // An inherited claim must not let a modifier return the unclaimed tree.
    loka::app::scene::Node *live = windowNode(window, "Live");
    loka::app::scene::Node *frozen = windowNode(window, "Frozen");
    loka::app::scene::Node *natural = windowNode(window, "Natural");
    LOKA_VERIFY(live && live->asZStackNode()->props.unitGround == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR);
    LOKA_VERIFY(frozen && frozen->asZStackNode()->props.unitGround == loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR);
    LOKA_VERIFY(natural
                && natural->asStackNode()->props.rowUndeclaredWidth_ == loka::app::ROW_UNDECLARED_WIDTH_NATURAL);
  }
  void checkCardMarginPlacement()
  {
    const char *files[] = {"MAIN.JS", "MINES.JS"};
    const char *ids[] = {"SmirkyCard.Title", "Mines.Status"};
    for (unsigned i = 0; i < 2; ++i)
    {
      NullPlatformContext context;
      NullScenePlatformController platform;
      smirkycard::ScriptRuntime runtime;
      loka::core::String error;
      LOKA_VERIFY(runtime.loadBuiltin(readCardSource(files[i]).c_str(), error));
      WindowProps props = smirkycard::CardWindowProps();
      props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
      NullWindow window(&context, props, &platform);
      WindowAdmissionTestApp admission(window);
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      // Null has no native client rectangle; offer the window's client area
      // through its existing layout fixture, resolving the outer margin first.
      const int margin = resolveWindowContentMargin(&window, loka::app::layout::RailSpacing(12, 4, 9));
      loka::app::scene::LayoutState bounds;
      bounds.x = bounds.y = margin;
      bounds.width = 420 - 2 * margin;
      bounds.height = 340 - 2 * margin;
      platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), bounds);
      loka::app::scene::Node *node = windowNode(window, ids[i]);
      LOKA_VERIFY(node && node->getContext());
      if (i == 0)
        LOKA_VERIFY(static_cast<NullAttributedTextContext *>(node->getContext())
                        ->commitPresented(loka::app::AttributedString(), platform.paintScope()));
      else
        LOKA_VERIFY(static_cast<NullTextContext *>(node->getContext())
                        ->commitPresented(loka::core::String::Literal("different"), platform.paintScope()));
      const loka::app::scene::PaintQuery query = {platform.paintScope(), loka::app::scene::PLACEMENT_ELIGIBLE};
      const loka::app::scene::PaintAnswer placed =
          static_cast<loka::app::scene::NativeNodeContext *>(node->getContext())->queryPaintDamage(query);
      LOKA_VERIFY(placed.kind == loka::app::scene::PAINT_ANSWER_EXACT);
      LOKA_VERIFY(placed.damage.x == 9 && placed.damage.y == 9 && placed.damage.width == 402);
      std::fprintf(stderr,
                   "[margin probe] %s x=%d y=%d width=%d\n",
                   files[i],
                   placed.damage.x,
                   placed.damage.y,
                   placed.damage.width);
    }
  }

  void checkBoxPadding()
  {
    std::fputs("[pin] Box padding forms, frozen copy and own data properties\n", stderr);
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("Object.prototype.padding=99;card('first',c=>({compose(){return VStack("
                                    "Box(Text('s'),{padding:'standard'}).TEST_ID('Standard'),"
                                    "Box(Text('z'),{padding:0}).TEST_ID('Zero'),"
                                    "Box(Text('n'),{padding:12}).TEST_ID('Number'),"
                                    "Box(Text('m'),{padding:4096}).TEST_ID('Maximum'),"
                                    "Box(Text('i'),{}).TEST_ID('Inherited'))}}));",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    typedef loka::app::layout::LayoutGap Gap;
    const char *ids[] = {"Standard", "Zero", "Number", "Maximum", "Inherited"};
    const Gap expected[] = {Gap::standardWindowMargin(), Gap::of(0), Gap::of(12), Gap::of(4096), Gap::unspecified()};
    for (unsigned i = 0; i < 5; ++i)
    {
      loka::app::scene::Node *node = windowNode(window, ids[i]);
      LOKA_VERIFY(node && node->asBoxNode());
      LOKA_VERIFY(node->asBoxNode()->props.padding == expected[i]);
    }
    expectJs(runtime,
             "(()=>{const o={padding:'standard'},t=Box(Text('s'),o);o.padding=0;"
             "const n={padding:12},u=Box(Text('n'),n);n.padding=0;"
             "return Object.isFrozen(t)&&Object.isFrozen(u)&&t.padding==='standard'&&u.padding===12&&"
             "!Object.hasOwn(Box(Text('i'),{}),'padding')})()",
             "true");
    checkComposeRefusal("card('first',c=>({compose(){return Object.defineProperty(Object.assign({},Box(Text('x'))),"
                        "'padding',{get(){throw new Error('must not run')},enumerable:true})}}));",
                        "JavaScript Box has an invalid width, height or padding.");
    checkComposeRefusal("card('first',c=>({compose(){return Object.assign({},Box(Text('x')),{padding:'big'})}}));",
                        "JavaScript Box has an invalid width, height or padding.");
  }

  void checkCardWindowMargin()
  {
    NullPlatformContext context;
    SmirkyCardAppConfig config(&context);
    StandaloneCardTestApp app(&config);
    app.run();
    LOKA_VERIFY(app.window());
    LOKA_VERIFY(resolveWindowContentMargin(app.window(), loka::app::layout::RailSpacing(12, 4, 9)) == 0);
  }

  void checkBoxLowering()
  {
    std::fputs("[pin] Box independent dimensions, validation and depth\n", stderr);
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("card('first',c=>({compose(){return VStack("
                                    "Box(Text('w'),{width:300}).TEST_ID('Width'),"
                                    "Box(Text('h'),{height:8}).TEST_ID('Height'),"
                                    "Box(Text('b'),{width:120,height:40}).TEST_ID('Both'),"
                                    "Box(Text('p')).TEST_ID('Plain'),Box(Text('e'),{}).TEST_ID('Empty'),"
                                    "Box(Text('m'),{width:4096,height:1}).TEST_ID('Max'),"
                                    "ScrollView(Box(Text('wide'),{width:800})).TEST_ID('WideBox'),"
                                    "Box(Text('x').TEST_ID('NarrowText'),{width:60}))}}));",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    const char *ids[] = {"Width", "Height", "Both", "Plain", "Empty", "Max"};
    const char *labels[] = {"w", "h", "b", "p", "e", "m"};
    const int widths[] = {300, 0, 120, 0, 0, 4096};
    const int heights[] = {0, 8, 40, 0, 0, 1};
    for (unsigned i = 0; i < 6; ++i)
    {
      loka::app::scene::Node *node = windowNode(window, ids[i]);
      LOKA_VERIFY(node && node->asBoxNode());
      loka::app::BoxProps expected;
      if (widths[i])
        expected.setWidth(widths[i]);
      if (heights[i])
        expected.setHeight(heights[i]);
      const loka::app::BoxProps &actual = node->asBoxNode()->props;
      LOKA_VERIFY(!(actual < expected) && !(expected < actual));
      loka::app::scene::Node *text = node->asNestable()->childrenHead();
      LOKA_VERIFY(text && text->asTextNode() && !text->nextInComposition);
      LOKA_VERIFY(text->asTextNode()->props.text_->get().compare(loka::core::String::Literal(labels[i])) == 0);
    }
    LaptopNaturalWidthSource source;
    short width = 0;
    loka::app::scene::Node *scroll = windowNode(window, "WideBox");
    LOKA_VERIFY(scroll && scroll->asScrollViewNode());
    LOKA_VERIFY(
        loka::app::layout::queryContentOverflowWidth(scroll, loka::app::layout::RailSpacing(12, 4, 9), &source, width));
    loka::app::scene::Node *text = windowNode(window, "NarrowText");
    LOKA_VERIFY(text && text->asTextNode() && text->getContext());
    NullTextContext *textContext = static_cast<NullTextContext *>(text->getContext());
    // As in NullMeasurementReuseTests, query the completed placement through paint damage.
    // A different previously presented value makes the answer carry the whole Text seat.
    LOKA_VERIFY(textContext->commitPresented(loka::core::String::Literal("y"), platform.paintScope()));
    const loka::app::scene::PaintQuery query = {platform.paintScope(), loka::app::scene::PLACEMENT_ELIGIBLE};
    const loka::app::scene::PaintAnswer placed = textContext->queryPaintDamage(query);
    LOKA_VERIFY(placed.kind == loka::app::scene::PAINT_ANSWER_EXACT);
    LOKA_VERIFY(width == 800);
    LOKA_VERIFY(placed.damage.width == 60);
    const char *badBuilds[] = {"Box()",
                               "Box(Text('a'),{},1)",
                               "Box([Text('a')])",
                               "Box(1)",
                               "Box({})",
                               "Box(Text('a'),[])",
                               "Box(Text('a'),5)",
                               "Box(Text('a'),{width:0})",
                               "Box(Text('a'),{width:-1})",
                               "Box(Text('a'),{width:1.5})",
                               "Box(Text('a'),{width:4097})",
                               "Box(Text('a'),{height:NaN})",
                               "Box(Text('a'),{height:Infinity})",
                               "Box(Text('a'),{width:'12'})",
                               "Box(Text('a'),{padding:-1})",
                               "Box(Text('a'),{padding:4097})",
                               "Box(Text('a'),{padding:1.5})",
                               "Box(Text('a'),{padding:'big'})",
                               "Box(Text('a'),{padding:null})",
                               "Box(Text('a'),{minWidth:10})",
                               "Box(Text('a'),{[Symbol('w')]:1})",
                               "Box(Text('a'),Object.create({width:10}))",
                               "Box(Text('a'),{width:undefined})",
                               "Box(Text('a'),null)"};
    for (unsigned i = 0; i < sizeof(badBuilds) / sizeof(badBuilds[0]); ++i)
    {
      const std::string expression =
          std::string("(()=>{try{") + badBuilds[i] + ";return false}catch(e){return e instanceof TypeError}})()";
      expectJs(runtime, expression.c_str(), "true");
    }
    expectJs(runtime,
             "(()=>{const child=Text('x'),o={width:300,height:8},t=Box(child,o).TEST_ID('copy');"
             "o.width=1;o.height=2;return Object.isFrozen(t)&&t.child===child&&t.width===300&&t.height===8&&"
             "![Box(child),Box(child,{})].some(t=>'width' in t||'height' in t)&&"
             "[Box(child),Box(child,{})].every(Object.isFrozen)})()",
             "true");
    const char *invalid[] = {"{width:0}", "{height:1.5}", "{width:'12'}", "{height:Infinity}", "{width:undefined}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
      const std::string script =
          std::string("card('first',c=>({compose(){return Object.assign({},Box(Text('x')),") + invalid[i] + ")}}));";
      checkComposeRefusal(script.c_str(), "JavaScript Box has an invalid width, height or padding.");
    }
    checkComposeRefusal("card('first',c=>({compose(){return Object.assign({},Box(Text('x')),{child:42})}}));",
                        "JavaScript Box has an invalid child.");
    checkComposeRefusal("card('first',c=>({compose(){return Object.defineProperty(Object.assign({},Box(Text('x'))),"
                        "'width',{get(){return 10},enumerable:true})}}));",
                        "JavaScript Box has an invalid width, height or padding.");
    checkComposeRefusal("card('first',c=>({compose(){return ScrollView(Box(ScrollView(Text('in'))))}}));",
                        "cannot contain another ScrollView");
    std::string tree = "Text('deep').TEST_ID('DeepBox')";
    for (int depth = 0; depth < 10; ++depth)
      tree = "Box(" + tree + ")";
    smirkycard::ScriptRuntime deepRuntime;
    const std::string script = "card('first',c=>({compose(){return " + tree + "}}));";
    LOKA_VERIFY(deepRuntime.loadBuiltin(script.c_str(), error));
    WindowProps deepProps;
    deepProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, deepRuntime));
    NullWindow deepWindow(&context, deepProps, &platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*deepWindow.scene(), true);
    LOKA_VERIFY(windowNode(deepWindow, "DeepBox"));
    const std::string refused = "card('first',c=>({compose(){return Box(" + tree + ")}}));";
    checkComposeRefusal(refused.c_str(), "exceeds depth 10");
    // A width on Object.prototype is not a claim: lowering reads the tree's own properties only.
    smirkycard::ScriptRuntime poisonRuntime;
    LOKA_VERIFY(poisonRuntime.loadBuiltin("Object.prototype.width=300;"
                                          "card('first',c=>({compose(){return Box(Text('p')).TEST_ID('Poison')}}));",
                                          error));
    WindowProps poisonProps;
    poisonProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, poisonRuntime));
    NullWindow poisonWindow(&context, poisonProps, &platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*poisonWindow.scene(), true);
    loka::app::scene::Node *poisoned = windowNode(poisonWindow, "Poison");
    LOKA_VERIFY(poisoned && poisoned->asBoxNode());
    const loka::app::BoxProps plain;
    LOKA_VERIFY(!(poisoned->asBoxNode()->props < plain) && !(plain < poisoned->asBoxNode()->props));
  }

  void checkLaptopLowerings()
  {
    checkNaturalRowLowering();
    checkNaturalColumnLowering();
    std::fputs("[pin] SmirkyTracker ScrollView, Card, ImageView options and depth\n", stderr);
    NullPlatformContext context;
    NullScenePlatformController platform;
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("var pic;card('first',c=>{pic=c.state.image();return {compose(){return VStack("
                                    "ScrollView(VStack(Text('a'),Text('b'))).TEST_ID('Scroll'),"
                                    "ScrollView(Text('inset'),{insets:{top:12,bottom:3}}).TEST_ID('Inset'),"
                                    "ScrollView(Text('empty'),{}).TEST_ID('EmptyScroll'),"
                                    "ScrollView(Text('zero'),{insets:{}}).TEST_ID('ZeroScroll'),"
                                    "ScrollView(Text('top'),{insets:{top:12}}).TEST_ID('TopScroll'),"
                                    "ScrollView(Text('bottom'),{insets:{bottom:3}}).TEST_ID('BottomScroll'),"
                                    "Card(Text('x')).TEST_ID('Card'),"
                                    "ImageView(pic,{width:120,height:80,fit:'contain'}).TEST_ID('Contain'),"
                                    "ImageView(pic,{fit:'cover'}).TEST_ID('Cover'),"
                                    "ImageView(pic,{fit:'stretch'}).TEST_ID('Stretch'),"
                                    "ImageView(pic).TEST_ID('Plain'),ImageView(pic,{}).TEST_ID('Empty'),"
                                    "ImageView(pic,{width:1,height:4096}).TEST_ID('Size'))}}});",
                                    error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    loka::app::scene::Node *scroll = windowNode(window, "Scroll");
    LOKA_VERIFY(scroll && scroll->asScrollViewNode());
    LOKA_VERIFY(scroll->asScrollViewNode()->props.viewport_.isValid());
    LOKA_VERIFY(scroll->asScrollViewNode()->requestedOrigin().x == 0);
    LOKA_VERIFY(scroll->asScrollViewNode()->requestedOrigin().y == 0);
    // Null offers 100 x 20. The first ScrollView takes that height; later
    // VStack seats have zero remaining height (confirmed by the layout probe).
    LOKA_VERIFY(scroll->asScrollViewNode()->props.viewport_.state()->get() == loka::core::Frame(0, 0, 100, 20));
    const char *scrollIds[] = {"Inset", "EmptyScroll", "ZeroScroll", "TopScroll", "BottomScroll"};
    for (unsigned i = 0; i < 5; ++i)
    {
      loka::app::scene::Node *node = windowNode(window, scrollIds[i]);
      LOKA_VERIFY(node && node->asScrollViewNode());
      const loka::app::ScrollViewProps &actual = node->asScrollViewNode()->props;
      const int top = i == 0 || i == 3 ? 12 : 0;
      const int bottom = i == 0 || i == 4 ? 3 : 0;
      LOKA_VERIFY(actual.contentInsets_ == loka::app::ScrollViewContentInsets::of(top, bottom));
      LOKA_VERIFY(actual.viewport_.state()->get() == loka::core::Frame(0, -top, 100, 0));
      LOKA_VERIFY(actual.viewport_.state()->get().y == -top);
    }
    loka::app::scene::Node *column = scroll->asNestable()->childrenHead();
    LOKA_VERIFY(column && column->asNestable() && !column->nextInComposition);
    LOKA_VERIFY(column->asStackNode() && column->asStackNode()->props.axis_ == loka::app::STACK_AXIS_COLUMN);
    loka::app::scene::Node *text = column->asNestable()->childrenHead();
    LOKA_VERIFY(text && text->asTextNode() && text->nextInComposition);
    LOKA_VERIFY(text->nextInComposition->asTextNode() && !text->nextInComposition->nextInComposition);
    LOKA_VERIFY(text->asTextNode()->props.text_->get().compare(loka::core::String::Literal("a")) == 0);
    LOKA_VERIFY(text->nextInComposition->asTextNode()->props.text_->get().compare(loka::core::String::Literal("b"))
                == 0);
    loka::app::scene::Node *card = windowNode(window, "Card");
    LOKA_VERIFY(card && card->kind() == loka::app::scene::NODE_KIND_CARD);
    column = card->asNestable()->childrenHead();
    LOKA_VERIFY(column && column->asNestable());
    text = column->asNestable()->childrenHead();
    LOKA_VERIFY(text && text->asTextNode() && !text->nextInComposition);
    LOKA_VERIFY(text->asTextNode()->props.text_->get().compare(loka::core::String::Literal("x")) == 0);
    const char *ids[] = {"Contain", "Cover", "Stretch", "Plain", "Empty", "Size"};
    for (unsigned i = 0; i < 6; ++i)
    {
      loka::app::scene::Node *node = windowNode(window, ids[i]);
      LOKA_VERIFY(node && node->asImageViewNode());
      const loka::app::ImageViewProps &actual = node->asImageViewNode()->props;
      loka::app::ImageViewProps expected;
      expected.image(actual.image_);
      LOKA_VERIFY(actual.image_);
      if (i == 0)
        expected.size(120, 80);
      if (i == 5)
        expected.size(1, 4096);
      if (i < 3)
        expected.attr(loka::app::ImageViewAttr().fit(static_cast<loka::app::ImageFit>(i + 1)));
      LOKA_VERIFY(!(actual < expected) && !(expected < actual));
    }
    const char *badBuilds[] = {"ScrollView()",
                               "ScrollView(Text('a'),[])",
                               "ScrollView(Text('a'),{insets:5})",
                               "ScrollView(Text('a'),{insets:{top:-1}})",
                               "ScrollView(Text('a'),{insets:{top:1.5}})",
                               "ScrollView(Text('a'),{insets:{left:1}})",
                               "ScrollView(Text('a'),{padding:1})",
                               "ScrollView(Text('a'),{padding:{}})",
                               "ScrollView(Text('a'),{insets:{top:40000}})",
                               "ScrollView(Text('a'),{insets:{bottom:1.5}})",
                               "ScrollView(Text('a'),{insets:{top:NaN}})",
                               "ScrollView(Text('a'),{insets:{top:Infinity}})",
                               "ScrollView(Text('a'),{insets:{top:'12'}})",
                               "ScrollView(Text('a'),{insets:[]})",
                               "ScrollView(Text('a'),null)",
                               "ScrollView(Text('a'),Object.create({insets:{top:1}}))",
                               "ScrollView(Text('a'),{insets:Object.create({top:1})})",
                               "ScrollView(Text('a'),{[Symbol()]:1})",
                               "ScrollView(Text('a'),{insets:{[Symbol()]:1}})",
                               "ScrollView(Text('a'),{insets:{'top\\0':1}})",
                               "ScrollView(Text('a'),{}, {})",
                               "ScrollView(Text('a'),Text('b'))",
                               "ScrollView(42)",
                               "ScrollView([])",
                               "ScrollView({})",
                               "Card()",
                               "Card([])",
                               "Card(Text('a'),Text('b'))",
                               "Card({})",
                               "ImageView(pic,5)",
                               "ImageView(pic,{width:10})",
                               "ImageView(pic,{height:10})",
                               "ImageView(pic,{width:10,height:10,fit:'tile'})",
                               "ImageView(pic,{width:0,height:10})",
                               "ImageView(pic,{width:10,height:10,bogus:1})",
                               "ImageView(pic,{width:1.5,height:10})",
                               "ImageView(pic,{width:10,height:4097})",
                               "ImageView(pic,{width:'10',height:10})",
                               "ImageView(pic,{width:NaN,height:10})",
                               "ImageView(pic,{width:10,height:Infinity})",
                               "ImageView(pic,{fit:1})",
                               "ImageView(pic,[])",
                               "ImageView(pic,null)",
                               "ImageView(pic,Object.create({width:10}))",
                               "ImageView(pic,{[Symbol()]:1})",
                               "ImageView(pic,{fit:'contain\\0'})",
                               "ImageView(pic,{width:undefined,height:10})"};
    for (unsigned i = 0; i < sizeof(badBuilds) / sizeof(badBuilds[0]); ++i)
    {
      const std::string expression =
          std::string("(()=>{try{") + badBuilds[i] + ";return false}catch(e){return e instanceof TypeError}})()";
      expectJs(runtime, expression.c_str(), "true");
    }
    expectJs(runtime,
             "[ScrollView(Text('x'),{insets:{top:12}}).TEST_ID('FrozenInset'),ScrollView(Text('x')),Card(Text('x')),"
             "ImageView(pic,{fit:'cover'})]"
             ".every(Object.isFrozen)",
             "true");
    expectJs(runtime,
             "(()=>{const t=ScrollView(Text('x'),{insets:{top:32767,bottom:32767}});"
             "return Object.isFrozen(t)&&t.insetTop===32767&&t.insetBottom===32767})()",
             "true");
    checkComposeRefusal(
        "card('first',c=>({compose(){return Object.assign({},ScrollView(Text('x')),{insetTop:40000})}}));",
        "invalid insets");
    checkComposeRefusal(
        "card('first',c=>({compose(){return Object.assign({},ScrollView(Text('x')),{insetBottom:1.5})}}));",
        "invalid insets");
    checkComposeRefusal("card('first',c=>({compose(){return Object.assign({},ScrollView(Text('x')),{child:42})}}));",
                        "JavaScript ScrollView has an invalid child.");
    checkComposeRefusal("card('first',c=>({compose(){return Object.assign({},Card(Text('x')),{child:[]})}}));",
                        "JavaScript Card has an invalid child.");
    checkComposeRefusal("card('first',c=>{const pic=c.state.image();return {compose(){return "
                        "Object.assign({},ImageView(pic),{width:10})}}});",
                        "invalid options");
    checkComposeRefusal("card('first',c=>{const tree=ImageView(c.state.image(),{width:10});"
                        "return {compose(){return tree}}});",
                        "provided together");
    expectJs(runtime,
             "(()=>{const options={width:120,height:80,fit:'contain'};"
             "const tree=ImageView(pic,options);options.width=7;options.fit='cover';"
             "return tree.width===120&&tree.height===80&&tree.fit===1})()",
             "true");
    expectJs(runtime, "Object.isFrozen(ImageView(pic,Object.assign(Object.create(null),{fit:'cover'})))", "true");
    checkComposeRefusal("card('first',c=>({compose(){return ScrollView(ScrollView(Text('x')))}}));",
                        "cannot contain another ScrollView");
    checkComposeRefusal("card('first',c=>{const s=c.state(true);return {compose(){return ScrollView(VStack(Text('a'),"
                        "Card(Show(s,ScrollView(Text('x'))))))}}});",
                        "cannot contain another ScrollView");
    std::string nested = "ScrollView(Text('nested'))";
    for (int depth = 0; depth < 8; ++depth)
      nested = "Box(" + nested + ")";
    const std::string nestedSource = "card('first',c=>({compose(){return ScrollView(" + nested + ")}}));";
    checkComposeRefusal(nestedSource.c_str(), "cannot contain another ScrollView");
    // A ScrollView may not nest, so its depth chain is one ScrollView over Cards.
    const char *wrappers[] = {"ScrollView", "Card"};
    for (unsigned i = 0; i < 2; ++i)
    {
      std::string tree = "Text('deep').TEST_ID('Deep')";
      for (int depth = 0; depth < 10; ++depth)
        tree = std::string(depth == 9 ? wrappers[i] : "Card") + "(" + tree + ")";
      smirkycard::ScriptRuntime deepRuntime;
      const std::string source = "card('first',c=>({compose(){return " + tree + "}}));";
      LOKA_VERIFY(deepRuntime.loadBuiltin(source.c_str(), error));
      WindowProps deepProps;
      deepProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, deepRuntime));
      NullWindow deepWindow(&context, deepProps, &platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(*deepWindow.scene(), true);
      LOKA_VERIFY(windowNode(deepWindow, "Deep"));
      const std::string refused = "card('first',c=>({compose(){return Card(" + tree + ")}}));";
      checkComposeRefusal(refused.c_str(), "exceeds depth 10");
    }
  }

  void countLaptopBands(loka::app::scene::Node *node, int &live, int &frozen, int &toolbars)
  {
    if (!node)
      return;
    if (node->testId() == "Laptop.Title")
      ++toolbars;
    if (node->asZStackNode())
    {
      const loka::app::ZStackUnitGround ground = node->asZStackNode()->props.unitGround;
      if (ground == loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR)
        ++frozen;
      if (ground == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR)
      {
        ++live;
        loka::app::scene::Node *page = node->asNestable()->childrenHead();
        LOKA_VERIFY(page);
        if (page->asScrollViewNode())
          LOKA_VERIFY(page->asScrollViewNode()->props.contentInsets_ == loka::app::ScrollViewContentInsets::of(30, 0));
        else
        {
          LOKA_VERIFY(page->asBoxNode());
          LOKA_VERIFY(page->asBoxNode()->props.padding == loka::app::StandardWindowMargin());
          loka::app::scene::Node *content = page->asNestable()->childrenHead();
          LOKA_VERIFY(content && content->asStackNode());
          loka::app::scene::Node *spacer = content->asNestable()->childrenHead();
          LOKA_VERIFY(spacer && spacer->asBoxNode() && spacer->asBoxNode()->props.height == 30);
        }
        loka::app::scene::Node *bar = page->nextInComposition;
        LOKA_VERIFY(bar && bar->asBoxNode() && bar->asBoxNode()->props.height == 30
                    && bar->asBoxNode()->props.padding == loka::app::layout::LayoutGap::of(4));
        LOKA_VERIFY(!bar->nextInComposition);
      }
    }
    loka::app::scene::INestable *nest = node->asNestable();
    for (loka::app::scene::Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
      countLaptopBands(child, live, frozen, toolbars);
  }

  void checkLaptopBands(NullWindow &window)
  {
    int live = 0, frozen = 0, toolbars = 0;
    countLaptopBands(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), live, frozen, toolbars);
    LOKA_VERIFY(live == 1 && frozen == 1 && toolbars == 1);
  }

  const char *const laptopStems[] = {"MACPLUS",
                                     "PB180C",
                                     "PB550C",
                                     "PDQ",
                                     "PISMO",
                                     "TIBOOK",
                                     "IBOOKG3",
                                     "MB2006",
                                     "MBP2011",
                                     "MBP2015",
                                     "MBP2019",
                                     "MBP2023",
                                     "NEO"};

  // Exercise the production query through ScriptRuntime; no production test API.
  template <int Depth> class LaptopPicturePlatform : public NamedPicturePlatform
  {
  public:
    virtual bool queryMainDisplayDepth(int &out) const
    {
      if (Depth == 0)
        return NamedPicturePlatform::queryMainDisplayDepth(out);
      out = Depth;
      return true;
    }
    virtual bool openFile(const loka::file::File &file, loka::platform::file::FileHandle &out) const
    {
      const bool mono = Depth == 1;
      // Machine pictures first, then the About logo (ICON / ICONMS).
      const int machines = mono ? 26 : 13;
      const int index = this->opens % (machines + 1);
      const std::string expected = index == machines ? std::string("Pictures/") + (mono ? "ICONMS" : "ICON") + ".PNG"
                                                     : std::string("Pictures/") + laptopStems[index % 13]
                                                           + (mono ? (index < 13 ? "M" : "MS") : "") + ".PNG";
      LOKA_VERIFY(file.relativePath().compare(loka::core::String::Utf8(expected.data(), expected.size())) == 0);
      return NamedPicturePlatform::openFile(file, out);
    }
  };

  void checkLaptopPictureChoice(NamedPicturePlatform &context, bool mono)
  {
    std::string source = readCardSource("LAPTOP.JS");
    // Register a test subclass solely to retain the actual instance. Its
    // inherited constructor and all production composition methods are intact.
    const std::string registration = "card('first', class {";
    const std::string::size_type start = source.find(registration);
    LOKA_VERIFY(start != std::string::npos);
    source.replace(start, registration.size(), "class LaptopUnderTest {");
    const std::string::size_type end = source.rfind("});");
    LOKA_VERIFY(end != std::string::npos);
    source.replace(end,
                   3,
                   "}\ncard('first', class extends LaptopUnderTest {"
                   "constructor(c){super(c);globalThis.laptop=this;}});");
    CardRunnerHarness h(source.c_str(), "scenario('first',c=>c.run(Flow().step(()=>{})));", 7, true, &context);
    h.mount();
    expectJs(h.runtime, "laptop.c.error.get()", "");
    LOKA_VERIFY(context.decodes == (mono ? 26 : 13) + 1); // plus the About logo
    expectJs(h.runtime, "laptop.thumbs === laptop.pics", mono ? "false" : "true");
    expectJs(h.runtime, "MACHINES.length===13 && MACHINES.every((m,i)=>laptop[m.id]===laptop.pics[i])", "true");
    expectJs(h.runtime, "laptop.pics.length===13 && laptop.thumbs.length===13", "true");
    expectJs(h.runtime, "laptop.list().children[0].child.children.length", "14");
    // The seventh wide row holds the odd thirteenth machine and an empty Box
    // partner, so the card keeps the width of every other card.
    expectJs(h.runtime,
             "(r=>r.children.length===2 && r.children[0].child.children.length===2"
             " && r.children[1].child.text==='')(laptop.list().children[0].child.children[6].child)",
             "true");
    expectJs(h.runtime, "laptop.compose().staticBlurUnit===1 && laptop.aboutShown.get()===false", "true");
    expectJs(h.runtime,
             "laptop.toolbar().child.children.length===2 && laptop.toolbar().child.children[0].children.length===4"
             " && laptop.toolbar().child.children[1].testId==='Laptop.Title'",
             "true");
    expectJs(h.runtime,
             "(()=>{const depths=[];function walk(t,d){"
             "if(t.testId && /Laptop\\.(Open|Narrow)\\./.test(t.testId))depths.push(d);"
             "if(t.child)walk(t.child,d+1);if(t.children)t.children.forEach(c=>walk(c,d+1));}"
             "walk(laptop.compose(),0);return depths.length===26 && depths.every(d=>d===10)})()",
             "true");
    expectJs(h.runtime,
             "[laptop.list(),laptop.detailPage()].every(t=>t.kind===ZStack(Text('')).kind&&t.blurUnit===1&&"
             "t.children[0].kind===ScrollView(Text('')).kind&&t.children[0].insetTop===30&&"
             "t.children[1].kind===Box(Text('')).kind&&t.children[1].height===30)",
             "true");
    checkLaptopBands(*h.window);
    expectJs(h.runtime, "laptop.aboutShown.set(true);laptop.aboutShown.get()", "true");
    h.ticks(2);
    LOKA_VERIFY(windowNode(*h.window, "Laptop.About.Name"));
    checkLaptopBands(*h.window);
    // Inspect the actual ImageView inputs, not merely the existence of thumbs.
    expectJs(h.runtime, "laptop.machineCard(0,150,94,'x','x').child.children[0].seat===laptop.thumbs[0]", "true");
  }

  void checkLaptopScenario()
  {
    std::fputs("[pin] LAPTOP.JS scenario under the Null harness\n", stderr);
    LOKA_VERIFY(makeDirectory("_named_pictures"));
    const char *suffixes[] = {"", "M", "MS"};
    for (unsigned i = 0; i < 13; ++i)
      for (unsigned j = 0; j < 3; ++j)
        writeMain((std::string("_named_pictures/") + laptopStems[i] + suffixes[j] + ".PNG").c_str(), "img");
    // The About logo: colour and mono-thumbnail variants are staged.
    writeMain("_named_pictures/ICON.PNG", "img");
    writeMain("_named_pictures/ICONMS.PNG", "img");
    {
      std::fputs("[pin] laptop picture choice: null color, 1-bit full + thumbnails, 8-bit color\n", stderr);
      LaptopPicturePlatform<0> unknown;
      checkLaptopPictureChoice(unknown, false);
      LaptopPicturePlatform<1> mono;
      checkLaptopPictureChoice(mono, true);
      LaptopPicturePlatform<8> color;
      checkLaptopPictureChoice(color, false);
    }
    LaptopPicturePlatform<0> context;
    const std::string source = readCardSource("LAPTOP.JS");
    const std::string companion = "globalThis.LAPTOP_PICTURE_SIZE = {width: 2, height: 3};\n"
                                  "globalThis.LAPTOP_NARROW_LIST = true;\n"
                                  + readCardSource("LAPTOP.FLOW.JS");
    const char *cells[] = {0, "startup", "laptop-carousel", "laptop-detail", "unknown"};
    for (unsigned cell = 0; cell < 5; ++cell)
    {
      const std::string selected =
          cells[cell] ? std::string("globalThis.LAPTOP_CELL = \"") + cells[cell] + "\";\n" + companion : companion;
      CardRunnerHarness h(source.c_str(), selected.c_str(), 7, true, &context, cells[cell] ? cells[cell] : "laptop");
      LOKA_VERIFY(h.recordLog("seed=7"));
      {
        // Mount's viewport publication settles after the Show seats attach,
        // as it does inside a rail entry operation.
        loka::core::Operation operation;
        h.mount();
        operation.settle();
        h.admission->flush();
      }
      h.ticks(40);
      LOKA_VERIFY(h.terminalCount == 1);
      if (cell == 4)
      {
        LOKA_VERIFY(h.terminal == loka::dsl::testing::SCENARIO_AUDIT_FAILED);
        continue;
      }
      LOKA_VERIFY(h.terminal == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
      checkLaptopBands(*h.window);
      LOKA_VERIFY(!windowNode(*h.window, "Laptop.About.Name"));
      LOKA_VERIFY(!windowNode(*h.window, "Laptop.About.Credit"));
      LOKA_VERIFY(!windowNode(*h.window, "Laptop.About.OK"));
      if (cell == 2)
      {
        LOKA_VERIFY(windowNode(*h.window, "Laptop.Wide.macplus"));
        LOKA_VERIFY(!windowNode(*h.window, "Laptop.Narrow.pb180c"));
      }
      else if (cell == 3)
      {
        LOKA_VERIFY(windowNode(*h.window, "Laptop.Back"));
        LOKA_VERIFY(!windowNode(*h.window, "Laptop.Presentation"));
      }
      else
        LOKA_VERIFY(windowNode(*h.window, "Laptop.Narrow.pb180c"));
      std::string audit = readScenarioAudit();
      // Only the Null decoder's fixture dimensions differ from the shipped pictures.
      std::string::size_type position = 0;
      while ((position = audit.find("%202x3\n", position)) != std::string::npos)
        audit.replace(position, 7, "%20320x200\n");
      const std::string expected =
          cell ? std::string("../../tests/scenarios/expected/smirkycard/") + cells[cell] + ".audit"
               : "tests/LAPTOP.audit";
      LOKA_VERIFY(audit == readCardSource(expected.c_str()));
      if (cell == 1)
      {
        std::fputs("[pin] laptop viewport: 320 wide, 319 narrow, zero unreported, 380 Classic-sized wide\n", stderr);
        const short widths[] = {320, 319, 0, 380};
        for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
        {
          loka::app::scene::LayoutState bounds;
          bounds.width = widths[i];
          bounds.height = 340;
          {
            loka::core::Operation operation;
            h.platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(*h.window->scene()),
                                               bounds);
            operation.settle();
            h.admission->flush();
          }
          const bool narrow = widths[i] == 319;
          const char *ids[] = {"macplus",
                               "pb180c",
                               "pb550c",
                               "pdq",
                               "pismo",
                               "tibook",
                               "ibookg3",
                               "mb2006",
                               "mbp2011",
                               "mbp2015",
                               "mbp2019",
                               "mbp2023",
                               "neo"};
          for (unsigned j = 0; j < sizeof(ids) / sizeof(ids[0]); ++j)
          {
            const std::string oneColumn = std::string("Laptop.Narrow.") + ids[j];
            const std::string twoColumns = std::string("Laptop.Open.") + ids[j];
            LOKA_VERIFY((windowNode(*h.window, oneColumn.c_str()) != 0) == narrow);
            LOKA_VERIFY((windowNode(*h.window, twoColumns.c_str()) != 0) == !narrow);
          }
        }
      }
    }
    for (unsigned i = 0; i < 13; ++i)
      for (unsigned j = 0; j < 3; ++j)
        LOKA_VERIFY(std::remove((std::string("_named_pictures/") + laptopStems[i] + suffixes[j] + ".PNG").c_str())
                    == 0);
    LOKA_VERIFY(std::remove("_named_pictures/ICON.PNG") == 0);
    LOKA_VERIFY(std::remove("_named_pictures/ICONMS.PNG") == 0);
    removeDirectory("_named_pictures");
  }

  void checkLoweringRefusals()
  {
    const char *trees[] = {"Show(c.state(1),Text('x'))", "Show({},Text('x'))",
      "OpenFileDialog(c.state.image())", "OpenFileDialog(null)",
      "ImageView(c.state.file())", "ImageView(42)",
      "Show(c.state(true),VStack(Text('prefix').TEST_ID('Prefix'),{kind:99999}))"};
    const char *messages[] = {"Bool", "Bool", "File", "File", "Image", "Image", "tree"};
    for (unsigned i = 0; i < sizeof(trees) / sizeof(trees[0]); ++i)
    {
      std::string source = "card('first',c=>{const tree=";
      source += trees[i];
      source += ";return {compose(){return tree}}});";
      checkComposeRefusal(source.c_str(), messages[i]);
    }
    const char *badBuilds[] = {"Show()",
                               "Show({},[])",
                               "Show({},Text('x'),Text('y'))",
                               "OpenFileDialog()",
                               "OpenFileDialog({}, {})",
                               "ImageView()",
                               "ImageView({}, 5)"};
    smirkycard::ScriptRuntime runtime;
    for (unsigned i = 0; i < sizeof(badBuilds) / sizeof(badBuilds[0]); ++i)
    {
      std::string expression = "(()=>{try{";
      expression += badBuilds[i];
      expression += ";return false}catch(e){return e instanceof TypeError}})()";
      expectJs(runtime, expression.c_str(), "true");
    }
    expectJs(runtime, "[Show({},Text('x')),OpenFileDialog({}),ImageView({})].every(Object.isFrozen)", "true");

    // Keep the first card alive so foreign identity refusal cannot be explained
    // by revocation. Direct lowering also pins failure before a definition escapes.
    NullPlatformContext context;
    NullScenePlatformController platform;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("var own,foreign;card('first',c=>{own={b:c.state(true),"
      "f:c.state.file(),i:c.state.image(),c};return {compose(){return Text('alive')}}});", error));
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow first(&context, props, &platform);
    WindowAdmissionTestApp admission(first);
    loka::dsl::testing::SceneTestAccess::updateAttached(*first.scene(), true);
    expectJs(runtime, "foreign=own;'saved'", "saved");
    WindowProps secondProps;
    secondProps.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow second(&context, secondProps, &platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*second.scene(), true);
    smirkycard::testing::CardFlowAccess::scene = second.scene();
    const char *foreignTrees[] = {"Show(foreign.b,Text('x'))", "OpenFileDialog(foreign.f)",
      "ImageView(foreign.i)", "Show(own.b,{kind:99999})"};
    for (unsigned i = 0; i < 4; ++i)
    {
      JSValue tree = JS_Eval(runtime.context(), foreignTrees[i], std::strlen(foreignTrees[i]),
                            "lowering pin", JS_EVAL_TYPE_GLOBAL);
      LOKA_VERIFY(!JS_IsException(tree));
      loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> lowered(
        smirkycard::testing::CardFlowAccess::card()->lowerChild(runtime.context(), tree, 0));
      JS_FreeValue(runtime.context(), tree);
      LOKA_VERIFY(!lowered.isSet());
      expectJs(runtime, i == 3 ? "own.c.error.get().includes('tree')"
                              : "own.c.error.get().includes('own')", "true");
    }
  }

  void checkExecutionSerial()
  {
    std::fputs("[pin] execution serial constructor seed and permanent exhaustion\n", stderr);
    smirkycard::ExecutionSerial serial(UINT32_MAX - 1);
    LOKA_VERIFY(serial.issue() == UINT32_MAX);
    for (unsigned i = 0; i < 3; ++i)
      LOKA_VERIFY(serial.issue() == 0);
  }

  void checkExecutionAdmissionExhaustion()
  {
    checkHandleCase("issuer last serial admitted once then refuses forever",
      "f=c.flow(Flow().step(()=>log.push('ran')))",
      "[f.run(),f.run(),f.run(),log.join(':')].join(':')", "true:false:false:ran", HandlePlatform::Success, false, true);
  }

  void checkHandles()
  {
    checkHandleCase("extended typed ownership and settlement",
      "f=c.flow(Flow().step(v=>work(v)))", "'ready'", "ready", HandlePlatform::Success, false, false, true);
    checkHandleCase("image watch forced null notifications",
      "f=c.flow(Flow().watch(im,v=>{log.push(v===null);return v}))",
      "im.set(null);im.set(null);log.join(':')", "true:true");
    checkHandleCase("same execution, stale later, run input, wrong kind, imitation",
      "f=c.flow(Flow().step(v=>work(v)).onSuccess(v=>log.push('ok')).onFailure(v=>log.push('fail')))",
      "work=()=>{saved=c0.native.loadImage(fs.get().file);im.set(saved);"
      "log.push(refuses(()=>im.set(fs.get().file)),refuses(()=>im.set({serial:1,slot:0,kind:1})));};f.run();"
      "work=v=>{im.get();im.get();log.push(refuses(()=>im.set(saved)),refuses(()=>im.set(v)));im.set(im.get())};f.run(saved);log.join(':')",
      "true:true:ok:true:true:ok");
    checkHandleCase("five slots failure preserves writes",
      "f=c.flow(Flow().step(()=>{saved=fs.get().file;im.set(null);for(let i=0;i<4;i++)fs.get()})"
      ".onSuccess(()=>log.push('bad')).onFailure(()=>log.push('failed')))",
      "f.run();log.join(':')", "failed");
    checkHandleCase("caught capacity exception succeeds",
      "f=c.flow(Flow().step(()=>{for(let i=0;i<4;i++)fs.get();try{fs.get()}catch(e){log.push(e instanceof RangeError)}})"
      ".onSuccess(()=>log.push('ok')))", "f.run();log.join(':')", "true:ok");
    checkHandleCase("constructors, read-only file, clearing, outside reads",
      "f=c.flow(Flow().step(()=>{im.set(c.native.loadImage(fs.get().file));im.set(null);log.push(im.get()===null)}))",
      "log.push(refuses(()=>c0.state.file()),refuses(()=>c0.state.image()),refuses(()=>fs.set(null)),"
      "refuses(()=>fs.set(1)),refuses(()=>im.get()),fs.get().file===null,Object.isFrozen(fs.get()));"
      "f.run();im.set(null);log.join(':')", "true:true:true:true:true:true:true:true");
    checkHandleCase("typed watch projects facts and a handle",
      "f=c.flow(Flow().watch(fs,v=>{log.push(v.kind,v.isFile,v.isCanceled,v.isError,Object.isFrozen(v));return v.file})"
      ".step(h=>im.set(c.native.loadImage(h))).onSuccess(()=>log.push('ok')))",
      "log.join(':')", "1:true:false:false:true:ok");
    checkHandleCase("read error", "f=c.flow(Flow().step(()=>c.native.loadImage(fs.get().file)).onFailure(e=>log.push(e.includes('READ_STDIO_OPEN_FAILED'))))",
      "f.run();log.join(':')", "true", HandlePlatform::ReadFailure);
    checkHandleCase("decode error", "f=c.flow(Flow().step(()=>c.native.loadImage(fs.get().file)).onFailure(e=>log.push(e.includes('decode failure'))))",
      "f.run();log.join(':')", "true", HandlePlatform::DecodeFailure);
    checkHandleCase("allocation has no stdio retry", "f=c.flow(Flow().step(()=>c.native.loadImage(fs.get().file)).onFailure(e=>log.push(e.includes('READ_ALLOCATION_REFUSED'))))",
      "f.run();log.join(':')", "true", HandlePlatform::CapacityFailure);
    checkHandleCase("missing runtime context", "f=c.flow(Flow().step(()=>log.push(refuses(()=>c.native.loadImage(fs.get().file)))))",
      "f.run();log.join(':')", "true", HandlePlatform::Success, true);
    checkHandleCase("post-native navigation refuses publication and cancels terminal",
      "f=c.flow(Flow().step(()=>log.push(refuses(()=>c.native.loadImage(fs.get().file))))"
      ".onSuccess(()=>log.push('bad')).onFailure(()=>log.push('bad')))",
      "f.run();log.join(':')", "true", HandlePlatform::Navigate);
    checkHandleCase("resolver refuses TransitionPending",
      "f=c.flow(Flow().step(()=>{saved=fs.get().file;c.go('second');log.push(refuses(()=>c.native.loadImage(saved)))}))",
      "f.run();log.join(':')", "true", HandlePlatform::NoNativeWork);
    checkHandleCase("release observes cleared execution",
      "f=c.flow(Flow().step(()=>{saved=c.native.loadImage(fs.get().file)}))",
      "f.run();'done'", "done", HandlePlatform::ReleaseProbe);
    checkExecutionAdmissionExhaustion();
  }

  void checkTypedSeats()
  {
    checkProductionFlowCase("typed and scalar seats share one budget",
      "for(let i=0;i<126;i++)c.state(0);c.state.file();try{c.state.image()}catch(e){n=e instanceof RangeError?1:0}",
      "n", "1");
    checkProductionFlowCase("typed seats constructor and empty access",
      "globalThis.fs=c.state.file();globalThis.im=c.state.image();"
      "f=c.flow(Flow().step(()=>im.get()).onSuccess(v=>log.push(v===null)))",
      "f.run();[Object.isFrozen(c0.state),Object.isFrozen(c0.native),fs.get().kind,"
      "fs.get().file===null,log[0],(()=>{try{im.get();return false}catch(e){return e instanceof TypeError}})()].join(':')",
      "true:true:0:true:true:true");
  }

  void checkProductionFlow()
  {
    std::puts("[pin] production Flow watch/run, SKIP, failure and constructor doors");
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(runtime.loadBuiltin("var c0,s,f,log=[],again,failures=0;"
                                    "card('first',c=>{c0=c;s=c.state(0);"
                                    "f=c.flow(Flow().watch(s,v=>v<0?Flow.SKIP:v+1)"
                                    ".step(v=>{again=f.run(99);return v*2})"
                                    ".onSuccess(v=>log.push(v)).onFailure(e=>failures++));"
                                    "return {compose(){return Text('flow')}}});",
                                    error));
    NullPlatformContext context;
    NullScenePlatformController platform;
    WindowProps props;
    props.scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, runtime));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp admission(window);
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    expectJs(runtime, "c0.error.get()", "");
    expectJs(runtime, "log.length", "0");
    expectJs(runtime, "s.set(3);log.join(',')", "8");
    expectJs(runtime, "f.run(5);log.join(',')", "8,10");
    expectJs(runtime, "again", "false");
    expectJs(runtime, "s.set(-1);log.join(',')+':'+failures", "8,10:0");
    expectJs(runtime, "try{c0.flow(Flow().step(v=>v));'bad'}catch(e){e instanceof TypeError}", "true");
  }

  void checkScenarioOff()
  {
    smirkycard::ScriptRuntime runtime;
    loka::core::String error;
    LOKA_VERIFY(
        runtime.loadBuiltin("var off;card('first',function(c){off=[typeof Flow,typeof scenario,typeof c.run,typeof "
                            "c.test].join(',');return {compose:function(){return Text('off');}};});",
                            error));
    NullPlatformContext context;
    mountedTitle(runtime, context);
    expectJs(runtime, "off", "function,undefined,undefined,undefined");
    std::remove("_smirky_scenario.audit");
  }
} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && !std::strcmp(argv[1], "--viewport-seat"))
  {
    checkViewportSeat();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--named-pictures"))
  {
    checkNamedPictures();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--viewer-scenario"))
  {
    checkChosenFileDelivery();
    checkChosenFileLookupNavigation();
    checkImageFacts();
    checkImageFactsLookupNavigation();
    checkViewerScenario();
    checkScenarioOff();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--lowerings"))
  {
    checkViewportSeat();
    checkScrollViewportPersistence();
    checkLaptopLowerings();
    checkBoxPadding();
    checkCardWindowMargin();
    checkCardMarginPlacement();
    checkBoxLowering();
    checkMinimumLowerings();
    checkZStackLowering();
    checkLoweringRefusals();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--execution-serial"))
  {
    checkExecutionSerial();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--execution-exhaustion"))
  {
    checkExecutionAdmissionExhaustion();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--typed-seats"))
  {
    checkTypedSeats();
    checkHandles();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--production-flow"))
  {
    checkProductionFlow();
    checkProductionFlowEdges();
    checkScenarioOff();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--laptop-scenario"))
  {
    checkLaptopScenario();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--mines-scenario"))
  {
    checkBakedCompanionReplacement();
    checkChosenFileDelivery();
    checkChosenFileLookupNavigation();
    checkImageFacts();
    checkImageFactsLookupNavigation();
    checkViewerScenario();
    checkMinesScenario();
    checkStandaloneScenario();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--materialized-getters"))
  {
    checkMaterializedGetters();
    checkMissingComposeAfterAttach();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--early-seats"))
  {
    checkEarlySeatRefusal();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--exception-context"))
  {
    checkExceptionContext();
    return 0;
  }
  if (argc == 3 && !std::strcmp(argv[1], "--carry-refusal"))
  {
    checkCarryRefusals(std::atoi(argv[2]));
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--carry-engines"))
  {
    checkCarryAcrossEngines();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--carry"))
  {
    checkCarry();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--card-context"))
  {
    checkCardContext();
    checkContextRevocation();
    checkExceptionContext();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--clickable-lifetime"))
  {
    checkClickableLifetime();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--many-clickables"))
  {
    checkManyClickables("Button");
    checkManyClickables("Cell");
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--grid"))
  {
    checkGrid();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--mines"))
  {
    checkMines();
    checkViewerNavigation();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--viewer-navigation"))
  {
    checkViewerNavigation();
    return 0;
  }
  if (argc == 3 && !std::strcmp(argv[1], "--scenario-case"))
  {
    checkCardScenarios(std::atoi(argv[2]));
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--scenarios"))
  {
    checkCardScenarios();
    checkScenarioSetup();
    checkScenarioRandom();
    checkScenarioEngineReplacement();
    checkScenarioOff();
    checkBakedCompanionReplacement();
    checkChosenFileDelivery();
    checkChosenFileLookupNavigation();
    checkImageFacts();
    checkImageFactsLookupNavigation();
    checkViewerScenario();
    checkMinesScenario();
    checkStandaloneScenario();
    return 0;
  }
  checkExecutionSerial();
  checkViewportSeat();
  checkScrollViewportPersistence();
  checkLaptopLowerings();
  checkBoxPadding();
  checkCardWindowMargin();
  checkCardMarginPlacement();
  checkBoxLowering();
  checkMinimumLowerings();
  checkLoweringRefusals();
  checkTypedSeats();
  checkHandles();
  checkProductionFlow();
  checkProductionFlowEdges();
  checkCardScenarios();
  checkScenarioSetup();
  checkScenarioRandom();
  checkScenarioEngineReplacement();
  checkScenarioOff();
  checkBakedCompanionReplacement();
  checkChosenFileDelivery();
  checkChosenFileLookupNavigation();
  checkImageFacts();
  checkImageFactsLookupNavigation();
  checkNamedPictures();
  checkViewerScenario();
  checkMinesScenario();
  checkLaptopScenario();
  checkStandaloneScenario();
  checkCarry();
  checkMaterializedGetters();
  checkEarlySeatRefusal();
  checkMissingComposeAfterAttach();
  checkCardContext();
  checkContextRevocation();
  checkExceptionContext();
  checkGrid();
  checkMines();
  testScriptAllocatorAlignsTwoByteAlignedBase();
  testScriptContextIsPointerTagAligned();
  checkRegistry();
  checkCounterAndRefusals();
  checkTextStyle();
  checkTextColors();
  checkMarkupParser();
  checkMarkup();
  checkBlockStyle();
  checkMarkupAllocationRefusal();
  checkTreePropertyCopy();
  checkTextStyleRefusals();
  checkChangedTextStyleRefusal();
  printRecordCosts();
  checkClickableLifetime();
  checkManyClickables("Button");
  checkManyClickables("Cell");
  checkManyClickables("Cell", 128);
  checkEnabledSeat();
  checkArrayChildren();
  checkZStackLowering();
  checkLifecycleHooks();
  smirkycard::ScriptRuntime runtime;
  loka::core::String builtinError;
  LOKA_VERIFY(runtime.loadBuiltin(smirkycard::BuiltinMainJs(), builtinError));
  checkEvaluation(runtime);
  checkMessageBox(runtime);
  checkSceneSwitching(runtime);
  checkRecordAllocationRefusal();
  checkRequiredRefusals();
  checkRetiredSeatAndIntegerRefusal();
  checkMainJsLoad();
  checkOpenSibling();
  LOKA_VERIFY(!smirkycard::CreateCard(SMIRKY_CARD_ERROR, runtime));
  std::puts("SmirkyCard: message box, evaluation, failure recovery, and 20 Scene switches passed.");
  return 0;
}
