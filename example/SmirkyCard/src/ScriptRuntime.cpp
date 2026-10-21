#include "ScriptRuntime.hpp"
#include "JsOwnProperties.hpp"
#include "CardNodes.hpp"
#include "CardFlowDescription.hpp"
#include "app/PlatformContext.hpp"
#include "core/io/File.hpp"
#include "platform/file/FileHandle.hpp"
#if defined(LOKA_RETRO68)
#include "ToolboxByteSource.hpp"
#else
#include "core/resource/lrpk/LrpkStdioByteSource.hpp"
#endif
#include <cassert>
#include <new>
#ifdef TEST_BUILD
#include "CardScenario.hpp"
#endif

namespace smirkycard
{
  const char *BuiltinMainJs()
  {
#include "BuiltinMainJs.inc"
  }
  namespace
  {
    JSValue copyTreeWithId(JSContext *ctx, JSValueConst source, JSValueConst id)
    {
      // Every modifier copies the whole node, including the other modifiers'
      // results ("testId", "enabledSeat", "rowWidth") and the modifier functions
      // themselves, so modifiers chain in either order.
      JsOwnProperties names(ctx);
      if (!names.read(source, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY))
        return JS_EXCEPTION;
      JSValue copy = JS_NewObject(ctx);
      if (JS_IsException(copy))
        return copy;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSValue value = JS_GetProperty(ctx, source, names.atom(i));
        if (JS_IsException(value) || JS_DefinePropertyValue(ctx, copy, names.atom(i), value, JS_PROP_C_W_E) < 0)
        {
          JS_FreeValue(ctx, copy);
          return JS_EXCEPTION;
        }
      }
      if (JS_SetPropertyStr(ctx, copy, "TEST_ID", JS_NewCFunction(ctx, &ScriptRuntime::testId, "TEST_ID", 1)) < 0
          || (!JS_IsUndefined(id) && JS_SetPropertyStr(ctx, copy, "testId", JS_DupValue(ctx, id)) < 0))
      {
        JS_FreeValue(ctx, copy);
        return JS_EXCEPTION;
      }
      return copy;
    }
  } // namespace
  JsEngine::JsEngine(ScriptRuntime &runtime, const JsCardBindingRegistry &registry, const std::string &sourceName)
      : runtime_(&runtime),
        sourceName_(sourceName),
        script_(SmirkyScriptCreate()),
        first_(JS_UNDEFINED),
        second_(JS_UNDEFINED),
#ifdef TEST_BUILD
        scenarioFirst_(JS_UNDEFINED),
        scenarioSecond_(JS_UNDEFINED),
        scenarioLaunch_(JS_UNDEFINED),
#endif
        cardCount_(0),
        nextRetired_(0)
  {
    if (!this->script_)
      return;
    JS_SetContextOpaque(this->context(), &runtime);
    JS_SetRuntimeOpaque(this->jsRuntime(), this);
    if (!registry.install(this->context()) || !JsFlowDescription::install(this->context())
#ifdef TEST_BUILD
        || (runtime.runnerEnabled() && !runtime.installRunner(*this))
#endif
    )
    {
      SmirkyScriptDestroy(this->script_);
      this->script_ = 0;
    }
  }

  JsEngine::~JsEngine()
  {
    assert(!this->cardCount_);
    if (this->script_)
    {
      JS_FreeValue(this->context(), this->first_);
      JS_FreeValue(this->context(), this->second_);
#ifdef TEST_BUILD
      JS_FreeValue(this->context(), this->scenarioFirst_);
      JS_FreeValue(this->context(), this->scenarioSecond_);
      JS_FreeValue(this->context(), this->scenarioLaunch_);
#endif
    }
    SmirkyScriptDestroy(this->script_);
  }

  JSContext *JsEngine::context() const
  {
    return SmirkyScriptContext(this->script_);
  }

  JSRuntime *JsEngine::jsRuntime() const
  {
    return SmirkyScriptRuntime(this->script_);
  }

  JSValue JsEngine::constructorFor(SmirkyCardId id) const
  {
    switch (id)
    {
    case SMIRKY_CARD_ERROR:
      return JS_UNDEFINED;
    case SMIRKY_CARD_FIRST:
      return JS_DupValue(this->context(), this->first_);
    case SMIRKY_CARD_SECOND:
      return JS_DupValue(this->context(), this->second_);
    }
    return JS_UNDEFINED;
  }

  bool JsEngine::hasConstructor(SmirkyCardId id) const
  {
    switch (id)
    {
    case SMIRKY_CARD_ERROR:
      return false;
    case SMIRKY_CARD_FIRST:
      return !JS_IsUndefined(this->first_);
    case SMIRKY_CARD_SECOND:
      return !JS_IsUndefined(this->second_);
    }
    return false;
  }

  void JsEngine::markRetired()
  {
    assert(this->runtime_ && this->runtime_->currentEngine_ != this && !this->nextRetired_);
    this->nextRetired_ = this->runtime_->retiredEngines_;
    this->runtime_->retiredEngines_ = this;
    if (!this->cardCount_)
      this->runtime_->destroyRetiredEngine(this);
  }

  JsEngineRef::JsEngineRef(JsEngine *engine)
      : engine_(engine)
  {
    if (this->engine_)
      ++this->engine_->cardCount_;
  }

  JsEngineRef::~JsEngineRef()
  {
    this->release();
  }

  void JsEngineRef::release()
  {
    if (!this->engine_)
      return;
    JsEngine *engine = this->engine_;
    this->engine_ = 0;
    assert(engine->cardCount_);
    --engine->cardCount_;
    if (!engine->cardCount_ && engine->runtime_->currentEngine_ != engine)
      engine->runtime_->destroyRetiredEngine(engine);
  }

  ScriptRuntime::ScriptRuntime(PlatformContext *context)
      :
#ifdef TEST_BUILD
        runner_(0),
#endif
        registry_(),
        currentEngine_(0),
        retiredEngines_(0),
        mainSource_(MAIN_SOURCE_BUILTIN),
        mainContext_(context),
        mainError_(),
        mainErrorScope_(MAIN_ERROR_NONE)
  {
    if (RegisterSmirkyCardBindings(this->registry_))
      this->currentEngine_ = this->createEngine();
  }

  ScriptRuntime::~ScriptRuntime()
  {
    assert(!this->interrupts_.top);
    assert(!this->retiredEngines_ && "retired JS engines must outlive no cards at runtime teardown");
    delete this->currentEngine_;
#ifdef TEST_BUILD
    delete this->runner_;
#endif
  }

  JsEngine *ScriptRuntime::createEngine(const std::string &sourceName)
  {
    JsEngine *engine = new (std::nothrow) JsEngine(*this, this->registry_, sourceName);
    if (engine && !engine->context())
    {
      delete engine;
      engine = 0;
    }
    return engine;
  }

  void ScriptRuntime::replaceCurrentEngine(JsEngine *engine)
  {
    assert(engine && engine != this->currentEngine_);
    JsEngine *old = this->currentEngine_;
    this->currentEngine_ = engine;
    if (old)
      old->markRetired();
  }

  void ScriptRuntime::destroyRetiredEngine(JsEngine *engine)
  {
    assert(engine && engine != this->currentEngine_ && !engine->cardCount_);
    JsEngine **entry = &this->retiredEngines_;
    while (*entry && *entry != engine)
      entry = &(*entry)->nextRetired_;
    assert(*entry == engine);
    *entry = engine->nextRetired_;
    engine->nextRetired_ = 0;
    delete engine;
  }
  int ScriptRuntime::interrupt(JSRuntime *, void *opaque)
  {
    unsigned int *remaining = static_cast<unsigned int *>(opaque);
    if (!*remaining)
      return 1;
    --*remaining;
    return 0;
  }

  ScriptRuntime::InterruptWindow::InterruptWindow(ScriptRuntime &runtime, JsEngine &engine)
      : runtime_(runtime),
        engine_(engine),
        previous_(0),
        installed_(false)
  {
    this->runtime_.openInterruptWindow(*this);
  }

  ScriptRuntime::InterruptWindow::~InterruptWindow()
  {
    this->runtime_.closeInterruptWindow(*this);
  }

  void ScriptRuntime::openInterruptWindow(InterruptWindow &window)
  {
    window.previous_ = this->interrupts_.top;
    if (!window.previous_)
      this->interrupts_.remaining = 100;
    window.installed_ = true;
    for (InterruptWindow *outer = window.previous_; outer; outer = outer->previous_)
      if (&outer->engine_ == &window.engine_)
      {
        window.installed_ = false;
        break;
      }
    this->interrupts_.top = &window;
    if (window.installed_)
    {
      JS_SetInterruptHandler(window.engine_.jsRuntime(), &ScriptRuntime::interrupt, &this->interrupts_.remaining);
    }
  }

  void ScriptRuntime::closeInterruptWindow(InterruptWindow &window)
  {
    assert(this->interrupts_.top == &window);
    if (window.installed_)
      JS_SetInterruptHandler(window.engine_.jsRuntime(), 0, 0);
    this->interrupts_.top = window.previous_;
  }

  JSValue ScriptRuntime::rawCall(JsEngine &engine, JSValueConst fn, int argc, JSValueConst *argv)
  {
    return JS_Call(engine.context(), fn, JS_UNDEFINED, argc, argv);
  }

  bool ScriptRuntime::captureException(JsEngine &engine, loka::core::String &error)
  {
    JSContext *context = engine.context();
    JSValue exception = JS_GetException(context);
    size_t length = 0;
    const char *text = JS_ToCStringLen(context, &length, exception);
    if (text)
    {
      if (length > 511)
      {
        length = 511;
        while (length && (static_cast<unsigned char>(text[length]) & 0xc0u) == 0x80u)
          --length;
      }
      error = loka::core::String::Utf8(text, length);
      JS_FreeCString(context, text);
    }
    else
      error = loka::core::String::Literal("JavaScript interrupted.");
    JS_FreeValue(context, exception);
    return false;
  }

  bool ScriptRuntime::evalMain(
      JsEngine &engine, const char *source, std::size_t length, const char *name, loka::core::String &error)
  {
    InterruptWindow interrupt(*this, engine);
    JSValue result;
    result = JS_Eval(engine.context(), source, length, name, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
      JS_FreeValue(engine.context(), result);
      return this->captureException(engine, error);
    }
    JS_FreeValue(engine.context(), result);
    error = loka::core::String();
    return true;
  }
  JSContext *ScriptRuntime::context() const
  {
    return this->currentEngine_ ? this->currentEngine_->context() : 0;
  }
  JSRuntime *ScriptRuntime::jsRuntime() const
  {
    return this->currentEngine_ ? this->currentEngine_->jsRuntime() : 0;
  }

  JSValue ScriptRuntime::card(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
  {
    ScriptRuntime *self = static_cast<ScriptRuntime *>(JS_GetContextOpaque(ctx));
    JsEngine *engine = static_cast<JsEngine *>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    size_t length = 0;
    const char *name;
    if (!self || !engine || engine->runtime_ != self || argc != 2 || !JS_IsString(argv[0])
        || !JS_IsFunction(ctx, argv[1]))
      return JS_ThrowTypeError(ctx, "card(name, F) requires a name and function");
    name = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!name)
      return JS_EXCEPTION;
    JSValue *slot = 0;
    if (length == 5 && !memcmp(name, "first", 5))
      slot = &engine->first_;
    if (length == 6 && !memcmp(name, "second", 6))
      slot = &engine->second_;
    JS_FreeCString(ctx, name);
    if (!slot)
      return JS_ThrowRangeError(ctx, "unknown card name");
    JS_FreeValue(ctx, *slot);
    *slot = JS_DupValue(ctx, argv[1]);
    return JS_UNDEFINED;
  }

  JSValue ScriptRuntime::testId(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *argv)
  {
    if (argc != 1 || !JS_IsString(argv[0]))
      return JS_ThrowTypeError(ctx, "TEST_ID(id) requires a string");
    JSValue copy = copyTreeWithId(ctx, thisValue, argv[0]);
    if (JS_IsException(copy))
      return copy;
    if (JS_FreezeObject(ctx, copy) < 0)
    {
      JS_FreeValue(ctx, copy);
      return JS_EXCEPTION;
    }
    return copy;
  }
  JSValue ScriptRuntime::withTreePolicy(JSContext *ctx,
                                        JSValueConst thisValue,
                                        int argc,
                                        const char *nodeName,
                                        const char *method,
                                        const char *property,
                                        const char *replacedProperty)
  {
    ScriptRuntime *runtime = static_cast<ScriptRuntime *>(JS_GetContextOpaque(ctx));
    JSValue kind = JS_GetPropertyStr(ctx, thisValue, "kind");
    int32_t number = 0;
    const int converted = JS_ToInt32(ctx, &number, kind);
    JS_FreeValue(ctx, kind);
    if (converted < 0)
      return JS_EXCEPTION;
    IJsNodeLowering *lowering = runtime ? runtime->registry_.lookup(number) : 0;
    if (!lowering || std::strcmp(lowering->name, nodeName))
      return JS_ThrowTypeError(ctx, "%s() applies to %s", method, nodeName);
    if (argc != 0)
      return JS_ThrowTypeError(ctx, "%s() takes no arguments", method);
    JSValue policy = JS_GetPropertyStr(ctx, thisValue, property);
    const bool selected = JS_IsNumber(policy) && JS_ToInt32(ctx, &number, policy) == 0 && number == 1;
    JS_FreeValue(ctx, policy);
    if (selected)
      return JS_DupValue(ctx, thisValue);
    JSValue copy = copyTreeWithId(ctx, thisValue, JS_UNDEFINED);
    if (JS_IsException(copy))
      return copy;
    if ((replacedProperty && JS_SetPropertyStr(ctx, copy, replacedProperty, JS_NewInt32(ctx, 0)) < 0)
        || JS_SetPropertyStr(ctx, copy, property, JS_NewInt32(ctx, 1)) < 0 || JS_FreezeObject(ctx, copy) < 0)
    {
      JS_FreeValue(ctx, copy);
      return JS_EXCEPTION;
    }
    return copy;
  }
  JSValue ScriptRuntime::naturalWidth(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *)
  {
    return withTreePolicy(ctx, thisValue, argc, "Row", "naturalWidth", "rowWidth");
  }
  JSValue ScriptRuntime::naturalHeight(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *)
  {
    return withTreePolicy(ctx, thisValue, argc, "VStack", "naturalHeight", "columnHeight");
  }
  JSValue ScriptRuntime::expBlurUnit(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *)
  {
    return withTreePolicy(ctx, thisValue, argc, "ZStack", "expBlurUnit", "blurUnit", "staticBlurUnit");
  }
  JSValue ScriptRuntime::expStaticBlurUnit(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *)
  {
    return withTreePolicy(ctx, thisValue, argc, "ZStack", "expStaticBlurUnit", "staticBlurUnit", "blurUnit");
  }
  JSValue ScriptRuntime::enabled(JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *argv)
  {
    if (argc != 1)
      return JS_ThrowTypeError(ctx, "enabled(seat) requires one seat");
    JSValue copy = copyTreeWithId(ctx, thisValue, JS_UNDEFINED);
    if (JS_IsException(copy))
      return copy;
    if (JS_SetPropertyStr(ctx, copy, "enabledSeat", JS_DupValue(ctx, argv[0])) < 0 || JS_FreezeObject(ctx, copy) < 0)
    {
      JS_FreeValue(ctx, copy);
      return JS_EXCEPTION;
    }
    return copy;
  }

  bool ScriptRuntime::loadBuiltin(const char *source, loka::core::String &error)
  {
    if (!this->currentEngine_)
    {
      error = loka::core::String::Literal("JavaScript runtime is unavailable.");
      return false;
    }
#ifdef TEST_BUILD
    if (this->runner_)
    {
      JsEngine *candidate = this->createEngine();
      if (!candidate)
      {
        error = loka::core::String::Literal("Could not allocate scenario engine");
        return false;
      }
      if (!this->evalMain(*candidate, source, strlen(source), "BuiltinCards.js", error)
          || !this->evalCompanion(*candidate, SMIRKY_CARD_FIRST, error))
      {
        delete candidate;
        return false;
      }
      this->replaceCurrentEngine(candidate);
      return true;
    }
#endif
    return this->evalMain(*this->currentEngine_, source, strlen(source), "BuiltinCards.js", error);
  }

  loka::core::String ScriptRuntime::mainErrorFor(SmirkyCardId card) const
  {
    if (this->mainErrorScope_ == MAIN_ERROR_EVERY_CARD
        || (this->mainErrorScope_ == MAIN_ERROR_FIRST_CARD && card == SMIRKY_CARD_FIRST))
      return this->mainError_;
    return loka::core::String();
  }

  void ScriptRuntime::loadMain(PlatformContext *context)
  {
    this->mainContext_ = context;
    this->mainSource_ = MAIN_SOURCE_BUILTIN;
    this->mainError_ = loka::core::String();
    this->mainErrorScope_ = MAIN_ERROR_NONE;
    if (!this->context())
      return; // unavailable runtime: every later call reports it (loadBuiltin's path)
#ifdef TEST_BUILD
    if (this->runner_)
    {
      std::string text;
      loka::core::String error;
      const FileReadResult readResult = this->readFile("MAIN.JS", text, error);
      JsEngine *candidate = 0;
      if (readResult != FILE_READ_FAILED)
      {
        const bool file = readResult == FILE_READ_OK;
        candidate = this->createEngine(file ? "MAIN.JS" : "");
        if (!candidate)
          error = loka::core::String::Literal("Could not allocate scenario engine");
        else if (this->evalMain(*candidate,
                                file ? text.data() : BuiltinMainJs(),
                                file ? text.size() : strlen(BuiltinMainJs()),
                                file ? "MAIN.JS" : "BuiltinCards.js",
                                error)
                 && this->evalCompanion(*candidate, SMIRKY_CARD_FIRST, error))
        {
          this->replaceCurrentEngine(candidate);
          this->mainSource_ = file ? MAIN_SOURCE_FILE : MAIN_SOURCE_BUILTIN;
          return;
        }
      }
      delete candidate;
      this->mainError_ = loka::core::String::Literal("Runner setup failed: ") + error;
      this->mainErrorScope_ = MAIN_ERROR_EVERY_CARD;
      return;
    }
#endif
    std::string text;
    loka::core::String error;
    if (this->readFile("MAIN.JS", text, error) == FILE_READ_OK)
    {
      JsEngine *candidate = this->createEngine("MAIN.JS");
      if (!candidate)
        error = loka::core::String::Literal("JavaScript runtime is unavailable.");
      else if (this->evalMain(*candidate, text.data(), text.size(), "MAIN.JS", error))
      {
        this->replaceCurrentEngine(candidate);
        this->mainSource_ = MAIN_SOURCE_FILE;
        return;
      }
      this->mainError_ =
          loka::core::String::Literal("MAIN.JS: ") + error + loka::core::String::Literal("; using built-in cards");
      this->mainErrorScope_ = MAIN_ERROR_EVERY_CARD;
      // Only the scratch engine saw the failed file; the built-in engine is clean.
      delete candidate;
    }
    else
    {
      this->mainError_ = error + loka::core::String::Literal("; using built-in cards");
      this->mainErrorScope_ = MAIN_ERROR_FIRST_CARD;
    }
    loka::core::String ignored;
    this->loadBuiltin(BuiltinMainJs(), ignored);
  }

  bool ScriptRuntime::IsPictureName(const std::string &stem)
  {
    if (stem.empty() || stem.size() > 26)
      return false;
    for (std::size_t i = 0; i < stem.size(); ++i)
    {
      const char c = stem[i];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
        return false;
    }
    return true;
  }

  loka::core::String ScriptRuntime::PictureFileName(const std::string &stem)
  {
    // Toolbox decodes PICT bytes; Win32 (WIC) and macOS (NSImage) take PNG.
    // The repository ships twins so JS never sees the suffix. Keep this rail
    // selection parallel with readFile's byte source immediately below.
#if defined(LOKA_RETRO68)
    const char *suffix = ".PICT";
#else
    const char *suffix = ".PNG";
#endif
    return loka::core::String::Utf8(stem.data(), stem.size()) + loka::core::String::Literal(suffix);
  }

  ScriptRuntime::FileReadResult
  ScriptRuntime::readFile(const std::string &name, std::string &text, loka::core::String &error) const
  {
    const loka::core::String fileName = loka::core::String::Utf8(name.data(), name.size());
    const loka::file::File item = loka::file::File::Application() << loka::file::File(fileName);
    loka::platform::file::FileHandle handle;
    if (!this->mainContext_ || !this->mainContext_->openFile(item, handle))
    {
      error = fileName + loka::core::String::Literal(": file is missing");
      return FILE_READ_MISSING;
    }
#if defined(LOKA_RETRO68)
    loka::toolbox::ToolboxByteSource source;
    if (!handle.hasSpec || !source.open(handle.spec))
#else
    loka::core::resource::lrpk::StdioByteSource source;
    if (handle.displayPath.empty() || !source.open(handle.displayPath))
#endif
    {
      // Some platform contexts resolve a path without checking it exists, so a
      // file that cannot be opened counts as missing; only content refusals fail.
      error = fileName + loka::core::String::Literal(": file is missing or could not read");
      return FILE_READ_MISSING;
    }
    std::size_t length = 0;
    if (!source.size(length))
    {
      error = fileName + loka::core::String::Literal(": could not read");
      return FILE_READ_FAILED;
    }
    else if (length > 64u * 1024u)
    {
      error = fileName + loka::core::String::Literal(": exceeds 64 KiB");
      return FILE_READ_FAILED;
    }
    else
    {
      text.assign(length, '\0');
      if (!source.readAt(0, length ? reinterpret_cast<unsigned char *>(&text[0]) : 0, length))
      {
        error = fileName + loka::core::String::Literal(": could not read");
        return FILE_READ_FAILED;
      }
      error = loka::core::String();
      return FILE_READ_OK;
    }
  }

  JsEngine *ScriptRuntime::prepareReload(SmirkyCardId card, loka::core::String &error)
  {
    const std::string &source = this->currentEngine_->sourceName_;
    return this->prepareFile(source.empty() ? std::string("MAIN.JS") : source, card, error);
  }

  JsEngine *ScriptRuntime::prepareOpen(const std::string &name, loka::core::String &error)
  {
    const std::string flatName = name.compare(0, 2, "./") == 0 ? name.substr(2) : name;
    return this->prepareFile(flatName, SMIRKY_CARD_FIRST, error);
  }

  JsEngine *ScriptRuntime::prepareFile(const std::string &name, SmirkyCardId card, loka::core::String &error)
  {
    std::string text;
    if (this->readFile(name, text, error) != FILE_READ_OK)
      return 0;
    const loka::core::String prefix =
        loka::core::String::Utf8(name.data(), name.size()) + loka::core::String::Literal(": ");
    JsEngine *candidate = this->createEngine(name);
    if (!candidate)
    {
      error = prefix + loka::core::String::Literal("JavaScript runtime is unavailable.");
      return 0;
    }
    if (!this->evalMain(*candidate, text.data(), text.size(), name.c_str(), error))
    {
      delete candidate;
      error = prefix + error;
      return 0;
    }
    if (!candidate->hasConstructor(card))
    {
      const char *cardName = card == SMIRKY_CARD_FIRST ? "first" : card == SMIRKY_CARD_SECOND ? "second" : "unknown";
      delete candidate;
      error = prefix + loka::core::String::Literal("card '") + loka::core::String::Literal(cardName)
              + loka::core::String::Literal("' is not defined");
      return 0;
    }
#ifdef TEST_BUILD
    if (this->runner_ && !this->evalCompanion(*candidate, card, error))
    {
      delete candidate;
      return 0;
    }
#endif
    error = loka::core::String();
    return candidate;
  }
  void ScriptRuntime::commitReload(JsEngine *candidate)
  {
    this->replaceCurrentEngine(candidate);
    this->mainSource_ = MAIN_SOURCE_FILE;
    this->mainError_ = loka::core::String();
    this->mainErrorScope_ = MAIN_ERROR_NONE;
  }
  void ScriptRuntime::discardReload(JsEngine *candidate)
  {
    delete candidate;
  }

#ifdef TEST_BUILD
  JSValue JsEngine::takeScenario()
  {
    JSValue result = this->scenarioLaunch_;
    this->scenarioLaunch_ = JS_UNDEFINED;
    return result;
  }

  bool ScriptRuntime::enableRunner(loka::dsl::testing::ScenarioAuditSink *sink,
                                   loka::dsl::testing::ScenarioClock *clock,
                                   unsigned long seed,
                                   const char *companion,
                                   const char *bakedCompanion)
  {
    if (!sink || !clock || !companion || !*companion || (bakedCompanion && !*bakedCompanion)
        || this->runner_ || !this->currentEngine_
        || this->currentEngine_->cardCount_ || this->currentEngine_->hasConstructor(SMIRKY_CARD_FIRST)
        || this->currentEngine_->hasConstructor(SMIRKY_CARD_SECOND))
      return false;
    this->runner_ = new (std::nothrow) Runner(sink, clock, seed, companion, bakedCompanion);
    return this->runner_ && this->installRunner(*this->currentEngine_);
  }

  bool ScriptRuntime::installRunner(JsEngine &engine)
  {
    JSContext *ctx = engine.context();
    JSValue global = JS_GetGlobalObject(ctx);
    const bool ok = JS_SetPropertyStr(ctx, global, "scenario", JS_NewCFunction(ctx, scenario, "scenario", 2)) >= 0;
    JS_FreeValue(ctx, global);
    return ok;
  }

  JSValue ScriptRuntime::scenario(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
  {
    ScriptRuntime *self = static_cast<ScriptRuntime *>(JS_GetContextOpaque(ctx));
    JsEngine *engine = static_cast<JsEngine *>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    if (!self->runner_ || self->runner_->registering != engine)
      return JS_ThrowTypeError(ctx, "scenario() is only valid during companion evaluation");
    if (argc != 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1]))
      return JS_ThrowTypeError(ctx, "scenario(cardName, fn) requires a string and function");
    size_t length = 0;
    const char *name = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!name)
      return JS_EXCEPTION;
    const SmirkyCardId id = length == 5 && !memcmp(name, "first", 5)    ? SMIRKY_CARD_FIRST
                            : length == 6 && !memcmp(name, "second", 6) ? SMIRKY_CARD_SECOND
                                                                        : SMIRKY_CARD_ERROR;
    JS_FreeCString(ctx, name);
    if (!engine->hasConstructor(id))
      return JS_ThrowTypeError(ctx, "scenario: unknown card or missing constructor");
    JSValue &slot = id == SMIRKY_CARD_FIRST ? engine->scenarioFirst_ : engine->scenarioSecond_;
    if (!JS_IsUndefined(slot))
      return JS_ThrowTypeError(ctx, "scenario: duplicate registration");
    slot = JS_DupValue(ctx, argv[1]);
    return JS_UNDEFINED;
  }

  bool ScriptRuntime::evalCompanion(JsEngine &engine, SmirkyCardId selected, loka::core::String &error)
  {
    std::string text = this->runner_->bakedCompanion;
    if (text.empty() && this->readFile(this->runner_->companion, text, error) != FILE_READ_OK)
      return false;
    this->runner_->registering = &engine;
    const bool ok = this->evalMain(engine, text.data(), text.size(), this->runner_->companion.c_str(), error);
    this->runner_->registering = 0;
    if (!ok)
      return false;
    JSValue fn = selected == SMIRKY_CARD_FIRST ? engine.scenarioFirst_ : engine.scenarioSecond_;
    if (!engine.hasConstructor(selected) || JS_IsUndefined(fn))
    {
      error = loka::core::String::Literal("scenario: no registration for selected card");
      return false;
    }
    engine.scenarioLaunch_ = JS_DupValue(engine.context(), fn);
    return true;
  }

  bool ScriptRuntime::call(
      JsEngine &engine, JSValueConst fn, int argc, JSValueConst *argv, JSValue &result, loka::core::String &error)
  {
    InterruptWindow interrupt(*this, engine);
    result = this->rawCall(engine, fn, argc, argv);
    if (JS_IsException(result))
      return this->captureException(engine, error);
    error = loka::core::String();
    return true;
  }

  unsigned int ScriptRuntime::retiredEngineCount() const
  {
    unsigned int count = 0;
    for (JsEngine *engine = this->retiredEngines_; engine; engine = engine->nextRetired_)
      ++count;
    return count;
  }
#endif
} // namespace smirkycard
