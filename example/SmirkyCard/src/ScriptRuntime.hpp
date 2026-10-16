#ifndef SMIRKYCARD_SCRIPT_RUNTIME_HPP
#define SMIRKYCARD_SCRIPT_RUNTIME_HPP

#include "ScriptEngine.h"
#include "ExecutionSerial.hpp"
#include "JsCardBindingRegistry.hpp"
#include "quickjs.h"
#include "core/String.hpp"
#include <cstddef>
#include <cstring>
#include <string>

class PlatformContext;

#ifdef TEST_BUILD
namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class ScenarioAuditSink;
      class ScenarioClock;
    } // namespace testing
  } // namespace dsl
} // namespace loka
#endif

namespace smirkycard
{
  class JsCardNode;
  class ScriptRuntime;
#ifdef TEST_BUILD
  class CardScenario;
#endif

  /** One independently evaluated QuickJS generation. Cards retain the engine
      they were born in; reload overlap lasts at most the admission cycle in
      which the old Scene is retired. */
  class JsEngine
  {
  public:
    JSContext *context() const;
    JSRuntime *jsRuntime() const;
    JSValue constructorFor(SmirkyCardId id) const;
    bool hasConstructor(SmirkyCardId id) const;
#ifdef TEST_BUILD
    /** Transfers the selected scenario once; navigation cannot mint another ticket. */
    JSValue takeScenario();
#endif

  private:
    friend class JsEngineRef;
    friend class ScriptRuntime;
    JsEngine(ScriptRuntime &runtime, const JsCardBindingRegistry &registry, const std::string &sourceName);
    ~JsEngine();
    void markRetired();
    ScriptRuntime *runtime_;
    /** Immutable origin filename; empty for the built-in fallback. */
    const std::string sourceName_;
    SmirkyScript *script_;
    JSValue first_;
    JSValue second_;
#ifdef TEST_BUILD
    JSValue scenarioFirst_, scenarioSecond_, scenarioLaunch_;
#endif
    unsigned int cardCount_;
    JsEngine *nextRetired_;
    JsEngine(const JsEngine &);
    JsEngine &operator=(const JsEngine &);
  };

  /** The only writer of JsEngine's live-card ledger. */
  class JsEngineRef
  {
  public:
    explicit JsEngineRef(JsEngine *engine = 0);
    ~JsEngineRef();

  private:
    void release();
    JsEngine *engine_;
    JsEngineRef(const JsEngineRef &);
    JsEngineRef &operator=(const JsEngineRef &);
  };

  /** Built-in card definitions shared by the app bootstrap and tests. */
  const char *BuiltinMainJs();
  /** App-owned runtime; card boundaries borrow it while retaining their birth
      engine through JsEngineRef. */
  class ScriptRuntime
  {
  public:
    enum MainSource
    {
      MAIN_SOURCE_BUILTIN,
      MAIN_SOURCE_FILE
    };
    /** Keeps QuickJS interruption enabled across a complete JS-facing operation. */
    class InterruptWindow
    {
    public:
      InterruptWindow(ScriptRuntime &runtime, JsEngine &engine);
      ~InterruptWindow();

    private:
      friend class ScriptRuntime;
      ScriptRuntime &runtime_;
      JsEngine &engine_;
      InterruptWindow *previous_;
      bool installed_;
      InterruptWindow(const InterruptWindow &);
      InterruptWindow &operator=(const InterruptWindow &);
    };

    /** Borrows the application context, including for baked runner sources. */
    explicit ScriptRuntime(PlatformContext *context = 0);
    ~ScriptRuntime();

    JsEngine *currentEngine() const
    {
      return this->currentEngine_;
    }
    PlatformContext *nativeContext() const { return this->mainContext_; }
    /** A picture stem is 1..26 ASCII letters, digits, underscores or hyphens.
        No suffix, separator, dot or embedded NUL is admitted (31-byte HFS limit). */
    static bool IsPictureName(const std::string &stem);
    /** Appends the native rail's picture suffix to a validated stem. */
    static loka::core::String PictureFileName(const std::string &stem);
    JSContext *context() const;
    JSRuntime *jsRuntime() const;
    bool loadBuiltin(const char *source, loka::core::String &error);
    /** Selects MAIN.JS once through the application's portable file door. */
    void loadMain(PlatformContext *context);
    /** Reload is two doors so the runtime truth and the visible card change
        together: prepareReload evaluates the engine's source file (MAIN.JS for
        built-ins) in a new engine and returns it uncommitted (0 with a message on any failure, including a candidate
        that does not define the requested card); the caller builds the
        replacement Scene from it and then either commitReload (the candidate
        becomes current, the previous engine retires) or discardReload. */
    JsEngine *prepareReload(SmirkyCardId card, loka::core::String &error);
    /** Prepares a sibling file whose entry card must be first. */
    JsEngine *prepareOpen(const std::string &name, loka::core::String &error);
    void commitReload(JsEngine *candidate);
    void discardReload(JsEngine *candidate);
    MainSource mainSource() const
    {
      return this->mainSource_;
    }
    /** A startup MAIN.JS failure belongs on the first card, or every card
        when the file reached QuickJS but could not evaluate. */
    loka::core::String mainErrorFor(SmirkyCardId card) const;
    bool
    evalMain(JsEngine &engine, const char *source, std::size_t length, const char *name, loka::core::String &error);
    /* Helper callback is public only so the local tree factory can install it. */
    static JSValue naturalWidth(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv);
    static JSValue testId(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv);
    static JSValue enabled(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv);

    SmirkyCardId evaluate(const char *source, char *error, size_t capacity)
    {
      return SmirkyScriptEvaluate(this->currentEngine_ ? this->currentEngine_->script_ : 0, source, error, capacity);
    }

    /** Evaluates UTF-8 source. Result and error are capped at 511 UTF-8 bytes. */
    bool evaluateToString(const loka::core::String &source, loka::core::String &result, loka::core::String &error)
    {
      char resultBuffer[512];
      char errorBuffer[512];
      const loka::core::StringBuffer sourceBuffer = source.bufferWithEncoding(loka::core::StringEncodingUtf8);
      const char *sourceBytes = static_cast<const char *>(sourceBuffer.data());
      const std::string sourceUtf8(sourceBytes ? sourceBytes : "", sourceBuffer.length());
      size_t resultLength = 0;
      size_t errorLength = 0;
      if (SmirkyScriptEvaluateToString(this->currentEngine_ ? this->currentEngine_->script_ : 0,
                                       sourceUtf8.c_str(),
                                       resultBuffer,
                                       sizeof(resultBuffer),
                                       &resultLength,
                                       errorBuffer,
                                       sizeof(errorBuffer),
                                       &errorLength))
      {
        result = loka::core::String::Utf8(resultBuffer, resultLength);
        error = loka::core::String();
        return true;
      }
      result = loka::core::String();
      error = loka::core::String::Utf8(errorBuffer, errorLength);
      return false;
    }

#ifdef TEST_BUILD
    unsigned int retiredEngineCount() const;
    /** Call before loading sources or creating cards. Services outlive all cards.
        Optional nonempty baked text replaces companion file reads, including reload/open. */
    bool enableRunner(loka::dsl::testing::ScenarioAuditSink *sink,
                      loka::dsl::testing::ScenarioClock *clock,
                      unsigned long seed,
                      const char *companion,
                      const char *bakedCompanion = 0);
    bool runnerEnabled() const
    {
      return this->runner_ != 0;
    }
    /** Bounded call in the originating engine, including exception formatting. */
    bool
    call(JsEngine &engine, JSValueConst fn, int argc, JSValueConst *argv, JSValue &result, loka::core::String &error);
#endif

#ifdef TEST_BUILD
  private:
    friend class CardScenario;
    struct Runner
    {
      Runner(loka::dsl::testing::ScenarioAuditSink *s,
             loka::dsl::testing::ScenarioClock *c,
             unsigned long value,
             const char *name,
             const char *source)
          : sink(s),
            clock(c),
            seed(value),
            companion(name),
            bakedCompanion(source ? source : ""),
            registering(0)
      {
      }
      loka::dsl::testing::ScenarioAuditSink *const sink;
      loka::dsl::testing::ScenarioClock *const clock;
      const unsigned long seed;
      const std::string companion;
      const std::string bakedCompanion;
      JsEngine *registering;
    };
    Runner *runner_;
    bool installRunner(JsEngine &engine);
    bool evalCompanion(JsEngine &engine, SmirkyCardId selected, loka::core::String &error);
    static JSValue scenario(JSContext *, JSValueConst, int, JSValueConst *);
#endif

  private:
    friend class testing::ExecutionSerialAccess;
    friend class JsCardNode;
    friend class CardFlow;
    friend class JsEngine;
    friend class JsEngineRef;
    friend class JsCardBindingRegistry;
    friend bool RegisterSmirkyCardBindings(JsCardBindingRegistry &registry);
    JsEngine *createEngine(const std::string &sourceName = std::string());
    void replaceCurrentEngine(JsEngine *engine);
    void destroyRetiredEngine(JsEngine *engine);
    void openInterruptWindow(InterruptWindow &window);
    void closeInterruptWindow(InterruptWindow &window);
    static int interrupt(JSRuntime *, void *opaque);
    /** Raw entry: callers gate before formatting a possible exception. */
    JSValue rawCall(JsEngine &engine, JSValueConst fn, int argc, JSValueConst *argv);
    bool captureException(JsEngine &engine, loka::core::String &error);
    static JSValue card(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv);
    /** MISSING: the file cannot be opened. FAILED: it opened, but its size or
        content was refused (unreadable, over 64 KiB). */
    enum FileReadResult
    {
      FILE_READ_OK,
      FILE_READ_MISSING,
      FILE_READ_FAILED
    };
    FileReadResult readFile(const std::string &name, std::string &text, loka::core::String &error) const;
    JsEngine *prepareFile(const std::string &name, SmirkyCardId card, loka::core::String &error);
    enum MainErrorScope
    {
      MAIN_ERROR_NONE,
      MAIN_ERROR_FIRST_CARD,
      MAIN_ERROR_EVERY_CARD
    };
    JsCardBindingRegistry registry_;
    JsEngine *currentEngine_;
    JsEngine *retiredEngines_;
    MainSource mainSource_;
    PlatformContext *mainContext_;
    ExecutionSerial executionSerial_;
    loka::core::String mainError_;
    MainErrorScope mainErrorScope_;
    struct InterruptState
    {
      InterruptState()
          : top(0),
            remaining(0)
      {
      }
      InterruptWindow *top;
      unsigned int remaining;
    } interrupts_;
    ScriptRuntime(const ScriptRuntime &);
    ScriptRuntime &operator=(const ScriptRuntime &);
  };
} // namespace smirkycard
#endif
