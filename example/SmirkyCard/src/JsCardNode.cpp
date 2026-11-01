#include "app/style/Style.hpp"
#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_NAMES

#include "CardNodes.hpp"
#include "JsClickNode.hpp"
#include "JsToolbarNode.hpp"
#include <climits>
#include "app/nodes/nestable/ScrollView.hpp"
#include "JsOwnProperties.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/OpenFileDialog.hpp"
#include "core/util/OwnedDef.hpp"
#include "JsNativeClass.hpp"
#include "app/FileImageSource.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/CpuClass.hpp"
#ifdef TEST_BUILD
#include "CardScenario.hpp"
#endif
#include <new>
#include <string>

namespace smirkycard
{
  const int kMaximumTreeDepth = 11;

  namespace
  {
    const char *cpuClassName(loka::platform::CpuClass cpuClass)
    {
      switch (cpuClass)
      {
      case loka::platform::CPU_CLASS_68K:
        return "68k";
      case loka::platform::CPU_CLASS_PPC_60X:
        return "ppc60x";
      case loka::platform::CPU_CLASS_G3_OR_FASTER:
        return "g3+";
      case loka::platform::CPU_CLASS_COUNT:
        break;
      }
      return 0;
    }

    enum ContextOperation
    {
      ContextState,
      ContextFlow,
      ContextGo,
      ContextOpen,
      ContextReload,
      ContextOperation_COUNT
    };
    enum SeatOperation
    {
      SeatGet,
      SeatSet,
      ErrorGet
    };
    JSClassID capabilityClassId = 0;
    int ensureCapabilityClass(JSRuntime *runtime)
    {
      JSClassDef def;
      memset(&def, 0, sizeof(def));
      def.class_name = "SmirkyCardCapability";
      return RegisterJsNativeClass(runtime, capabilityClassId, def);
    }
    bool installFunction(JSContext *context, JSValueConst object, const char *name, JSValue function)
    {
      if (JS_IsException(function))
        return false;
      return JS_SetPropertyStr(context, object, name, function) >= 0;
    }
    JsCardNode *capabilityNode(JSValueConst capability)
    {
      return static_cast<JsCardNode *>(JS_GetOpaque(capability, capabilityClassId));
    }
    JSValue
    seatNative(JSContext *ctx, JSValueConst receiver, int argc, JSValueConst *argv, int operation, JSValue *data)
    {
      JsCardNode *node = capabilityNode(data[0]);
      if (!node)
        return JS_ThrowTypeError(ctx, "seat belongs to a revoked card");
      if (operation == SeatGet)
        return node->seatGet(ctx, receiver);
      if (operation == ErrorGet)
        return node->errorSeatGet(ctx);
      if (argc != 1)
        return JS_ThrowTypeError(ctx, "seat.set(value) requires one value");
      return node->seatSet(ctx, receiver, argv[0]);
    }
    JSValue declareNative(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
    {
      JsCardNode *node = capabilityNode(data[0]);
      if (!node || !JS_GetOpaque(data[1], capabilityClassId) || argc != 1)
        return JS_ThrowTypeError(ctx, "declare(tree) is only valid during its compose call");
      return node->setComposeTree(ctx, argv[0]) ? JS_UNDEFINED : JS_EXCEPTION;
    }
    /** Owns the one-call declaration window; retained methods keep only a closed token. */
    class ComposeDelegate
    {
    public:
      ComposeDelegate(JSContext *context, JSValueConst capability)
          : context_(context),
            scope_(JS_NewObjectClass(context, capabilityClassId)),
            value_(JS_UNDEFINED)
      {
        if (JS_IsException(this->scope_))
        {
          this->value_ = JS_EXCEPTION;
          return;
        }
        JS_SetOpaque(this->scope_, this);
        this->value_ = JS_NewObject(context);
        JSValue data[] = {capability, this->scope_};
        if (!JS_IsException(this->value_)
            && (!installFunction(
                    context, this->value_, "declare", JS_NewCFunctionData(context, declareNative, 1, 0, 2, data))
                || JS_FreezeObject(context, this->value_) < 0))
        {
          JS_FreeValue(context, this->value_);
          this->value_ = JS_EXCEPTION;
        }
      }
      ~ComposeDelegate()
      {
        this->close();
        JS_FreeValue(this->context_, this->value_);
        JS_FreeValue(this->context_, this->scope_);
      }
      void close()
      {
        if (JS_IsObject(this->scope_))
          JS_SetOpaque(this->scope_, 0);
      }
      JSValue value() const
      {
        return this->value_;
      }

    private:
      JSContext *const context_;
      JSValue scope_;
      JSValue value_;
      ComposeDelegate(const ComposeDelegate &);
      ComposeDelegate &operator=(const ComposeDelegate &);
    };
    JSValue jsString(JSContext *ctx, const loka::core::String &value)
    {
      const loka::core::StringBuffer buffer = value.bufferWithEncoding(loka::core::StringEncodingUtf8);
      return JS_NewStringLen(ctx, static_cast<const char *>(buffer.data()), buffer.length());
    }
    bool integerNumber(JSContext *ctx, JSValueConst value, int32_t &out)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || number != number || number < -2147483648.0
          || number > 2147483647.0 || static_cast<double>(static_cast<int32_t>(number)) != number)
        return false;
      out = static_cast<int32_t>(number);
      return true;
    }
    class FormatIntEval : public loka::core::DerivedState<loka::core::String>::EvalFn
    {
    public:
      explicit FormatIntEval(loka::app::scene::NodeState<int> *value)
          : value_(value)
      {
      }
      virtual loka::core::String operator()()
      {
        return loka::core::String::FromInt(value_->get());
      }

    private:
      loka::app::scene::NodeState<int> *value_;
    };
    class FormatBoolEval : public loka::core::DerivedState<loka::core::String>::EvalFn
    {
    public:
      explicit FormatBoolEval(loka::app::scene::NodeState<bool> *value)
          : value_(value)
      {
      }
      virtual loka::core::String operator()()
      {
        return loka::core::String::Literal(value_->get() ? "true" : "false");
      }

    private:
      loka::app::scene::NodeState<bool> *value_;
    };
  } // namespace

#ifdef TEST_BUILD
  namespace testing
  {
    const char *machineCpuClassName(loka::platform::CpuClass cpuClass)
    {
      return cpuClassName(cpuClass);
    }
  } // namespace testing
#endif
  void CardScene::replaceWith(CardScene *next)
  {
    assert(next && this->getWindow());
    this->getWindow()->sceneManager()->commitTransaction(this, next);
  }
  bool JsCardProps::operator<(const loka::app::scene::PropsBase &rhs) const
  {
    const JsCardProps &o = static_cast<const JsCardProps &>(rhs);
    return runtime != o.runtime ? runtime < o.runtime : card != o.card ? card < o.card : carry < o.carry;
  }
  JsCardNode::JsCardNode(const JsCardProps &p)
      : loka::app::scene::StdCompositionBoundaryNodeBase<JsCardProps>(p),
#ifdef TEST_BUILD
        scenario_(0),
#endif
        engine_(p.runtime ? p.runtime->currentEngine() : 0),
        engineRef_(this->engine_),
        phase_(Live),
        capability_(JS_UNDEFINED),
        failed_(false),
        instance_(JS_UNDEFINED),
        seats_("Seat"),
        scrollViewports_("ScrollViewport"),
        handlers_("Handler"),
        flows_("Flow"),
        errorSeat_(JS_UNDEFINED),
        tree_(JS_UNDEFINED),
        onAttach_(JS_UNDEFINED),
        onDetach_(JS_UNDEFINED)
  {
    this->state(error_, loka::core::String::Literal(""));
    JSContext *ctx = this->engine_ ? this->engine_->context() : 0;
    JSValue ctor = ctx ? this->engine_->constructorFor(p.card) : JS_UNDEFINED;
    loka::core::String error;
    if (!ctx || JS_IsUndefined(ctor))
    {
      fail("Card is not registered.");
      return;
    }
    ScriptRuntime::InterruptWindow interrupt(*p.runtime, *this->engine_);
    if (!ensureCapabilityClass(this->engine_->jsRuntime())
        || !CardFlow::Admission::installHandles(this->engine_->jsRuntime()))
    {
      JS_FreeValue(ctx, ctor);
      fail("Could not create card context.");
      return;
    }
    this->capability_ = JS_NewObjectClass(ctx, capabilityClassId);
    if (JS_IsException(this->capability_))
    {
      JS_FreeValue(ctx, ctor);
      p.runtime->captureException(*this->engine_, error);
      fail(error);
      return;
    }
    JS_SetOpaque(this->capability_, this);
    JSValue context = JS_NewObject(ctx);
    this->errorSeat_ = JS_NewObject(ctx);
    bool ok = !JS_IsException(context) && !JS_IsException(this->errorSeat_);
    if (ok)
      ok = installFunction(
               ctx, this->errorSeat_, "get", JS_NewCFunctionData(ctx, seatNative, 0, ErrorGet, 1, &this->capability_))
           && JS_FreezeObject(ctx, this->errorSeat_) >= 0
           && JS_SetPropertyStr(ctx, context, "error", JS_DupValue(ctx, this->errorSeat_)) >= 0;
    const char *names[] = {"state", "flow", "go", "open", "reload"};
    for (int i = 0; ok && i < ContextOperation_COUNT; ++i)
      ok = installFunction(
          ctx,
          context,
          names[i],
          JS_NewCFunctionData(ctx, contextMethod, i == ContextReload ? 0 : 1, i, 1, &this->capability_));
    if (ok)
    {
      JSValue state = JS_GetPropertyStr(ctx, context, "state");
      JSValue native = JS_NewObject(ctx);
      ok = !JS_IsException(state) && !JS_IsException(native)
           && installFunction(
               ctx,
               state,
               "file",
               JS_NewCFunctionData(ctx, typedFactory, 0, JsSeatRecord::FILE_RESULT, 1, &this->capability_))
           && installFunction(ctx,
                              state,
                              "image",
                              JS_NewCFunctionData(ctx, typedFactory, 0, JsSeatRecord::IMAGE, 1, &this->capability_))
           && installFunction(ctx,
                              state,
                              "viewport",
                              JS_NewCFunctionData(ctx, typedFactory, 0, JsSeatRecord::VIEWPORT, 1, &this->capability_))
           && JS_FreezeObject(ctx, state) >= 0
           && installFunction(
               ctx, native, "loadImage", JS_NewCFunctionData(ctx, nativeLoadImage, 1, 0, 1, &this->capability_))
           && JS_FreezeObject(ctx, native) >= 0
           && JS_SetPropertyStr(ctx, context, "native", JS_DupValue(ctx, native)) >= 0;
      JS_FreeValue(ctx, state);
      JS_FreeValue(ctx, native);
    }
    if (ok)
    {
      JSValue machine = JS_NewObject(ctx);
      loka::platform::CpuClass cpuClass;
      const char *name = loka::platform::queryCpuClass(cpuClass) ? cpuClassName(cpuClass) : 0;
      ok = !JS_IsException(machine);
      if (ok)
      {
        JSValue value = name ? JS_NewString(ctx, name) : JS_NULL;
        // Own data properties, not assignment: a script-installed Object.prototype
        // accessor must not intercept the snapshot (bot P2 on #1463).
        ok = !JS_IsException(value) && JS_DefinePropertyValueStr(ctx, machine, "cpuClass", value, JS_PROP_C_W_E) >= 0;
      }
      if (ok)
      {
        PlatformContext *platform = this->props.runtime->nativeContext();
        int depth;
        JSValue value = platform && platform->queryMainDisplayDepth(depth) ? JS_NewInt32(ctx, depth) : JS_NULL;
        ok = JS_DefinePropertyValueStr(ctx, machine, "displayDepth", value, JS_PROP_C_W_E) >= 0;
      }
      ok = ok && JS_FreezeObject(ctx, machine) >= 0
           && JS_DefinePropertyValueStr(ctx, context, "machine", JS_DupValue(ctx, machine), JS_PROP_C_W_E) >= 0;
      JS_FreeValue(ctx, machine);
    }
    if (ok)
    {
      JSValue carry = p.carry.decode(ctx);
      if (JS_IsException(carry))
      {
        p.runtime->captureException(*this->engine_, error);
        fail("Could not decode carry in the destination card.");
        JS_FreeValue(ctx, context);
        JS_FreeValue(ctx, ctor);
        return;
      }
      ok = JS_DefinePropertyValueStr(ctx, context, "carry", carry, JS_PROP_C_W_E) >= 0;
    }
#ifdef TEST_BUILD
    if (ok && p.runtime->runnerEnabled())
    {
      this->scenario_ = new (std::nothrow) CardScenario(*this, context);
      ok = this->scenario_ && this->scenario_->installContext(ctx, context, this->capability_, testMethod);
    }
#endif
    if (ok)
      ok = JS_FreezeObject(ctx, context) >= 0;
    if (!ok)
    {
      p.runtime->captureException(*this->engine_, error);
      fail(error);
    }
    else
    {
      this->phase_ = Constructing;
      this->instance_ = JS_IsConstructor(ctx, ctor) ? JS_CallConstructor(ctx, ctor, 1, &context)
                                                    : JS_Call(ctx, ctor, JS_UNDEFINED, 1, &context);
      this->phase_ = Live;
      ok = !JS_IsException(this->instance_);
      if (!ok)
      {
        // Formatting a thrown object may execute JS; construction has already ended.
        p.runtime->captureException(*this->engine_, error);
        fail(loka::core::String::Literal("Card constructor failed. ") + error);
      }
    }
    this->phase_ = Live;
    JS_FreeValue(ctx, context);
    JS_FreeValue(ctx, ctor);
    if (ok && !JS_IsObject(this->instance_))
      fail("card(name, F): F(c) must return an object with compose()");
  }
  JSValue
  JsCardNode::contextMethod(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int operation, JSValue *data)
  {
    JsCardNode *node = capabilityNode(data[0]);
    if (!node)
      return JS_ThrowTypeError(ctx, "context belongs to a revoked card");
    if (operation == ContextFlow)
      return argc == 1 ? node->declareFlow(ctx, argv[0]) : JS_ThrowTypeError(ctx, "c.flow requires one description");
    if (operation == ContextState)
    {
      if (argc != 1)
        return JS_ThrowTypeError(ctx, "c.state(initial) requires one value");
      return node->mintState(ctx, argv[0]);
    }
    if (node->phase_ != Live)
      return JS_ThrowTypeError(ctx, "card navigation requires a Live card");
    const bool reload = operation == ContextReload;
    if (reload ? argc > 1 : (argc < 1 || argc > 2 || !JS_IsString(argv[0])))
      return JS_ThrowTypeError(ctx, "navigation requires a name (except reload) and optional carry");
    CardCarry carry;
    const int carryIndex = reload ? 0 : 1;
    if (!CardCarry::encode(ctx, argc > carryIndex ? argv[carryIndex] : JS_UNDEFINED, carry))
      return JS_EXCEPTION;
    if (reload)
    {
      node->reloadWithCarry(carry);
      return JS_UNDEFINED;
    }
    size_t length = 0;
    const char *name = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!name)
      return JS_EXCEPTION;
    if (operation == ContextGo)
      node->requestGo(name, length, carry);
    else
      node->requestOpen(name, length, carry);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
  }
  bool JsCardNode::installCapability(JSRuntime *runtime)
  {
    return ensureCapabilityClass(runtime) != 0;
  }

#ifdef TEST_BUILD
  JSValue JsCardNode::testMethod(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int op, JSValue *data)
  {
    JsCardNode *node = capabilityNode(data[0]);
    if (!node || !node->scenario_ || node->phase_ == Detaching || node->phase_ == Revoked)
      return JS_ThrowTypeError(ctx, "scenario operation belongs to a revoked card");
    return node->scenario_->operation(ctx, argc, argv, op);
  }
#endif
  JSValue JsCardNode::declareFlow(JSContext *ctx, JSValueConst value)
  {
    if (this->phase_ != Constructing)
      return JS_ThrowTypeError(ctx, "c.flow is only valid in a card constructor");
    JsFlowDescription *description = JsFlowDescription::get(value);
    if (!description || !description->steps())
      return JS_ThrowTypeError(ctx, "c.flow requires an unconsumed nonempty Flow");
    unsigned steps = 0;
    for (const JsFlowDescription::StepRecord *step = description->steps(); step; step = step->next)
      if (++steps > kCardFlowStepBudget)
        return JS_ThrowRangeError(ctx, "Card Flow step budget exceeded (128)");
    JsSeatRecord *seat = 0;
    if (description->hasWatch())
    {
      seat = this->findSeat(ctx, description->watched());
      if (!seat)
        return JS_ThrowTypeError(ctx, "Flow watch requires this card's own state seat");
    }
    if (this->flows_.count() >= kCardFlowBudget)
      return JS_ThrowRangeError(ctx, "Card Flow budget exceeded (32)");
    JSValue handle = JS_NewObject(ctx);
    if (JS_IsException(handle))
      return handle;
    JSValue data[] = {this->capability_, handle};
    if (!installFunction(ctx, handle, "run", JS_NewCFunctionData(ctx, runFlow, 1, 0, 2, data))
        || JS_FreezeObject(ctx, handle) < 0)
    {
      JS_FreeValue(ctx, handle);
      return JS_EXCEPTION;
    }
    if (!this->flows_.add(CardFlow::Initial(*this, ctx, value, handle, seat)))
    {
      JS_FreeValue(ctx, handle);
      return JS_ThrowOutOfMemory(ctx);
    }
    description->consume();
    return handle;
  }
  JSValue JsCardNode::runFlow(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
  {
    JsCardNode *card = capabilityNode(data[0]);
    if (!card)
      return JS_FALSE;
    for (CardFlow *flow = card->flows_.head(); flow; flow = flow->next)
      if (flow->matches(data[1]))
        return JS_NewBool(ctx, flow->run(argc ? argv[0] : JS_UNDEFINED));
    return JS_FALSE;
  }
  void JsCardNode::withdrawFlows()
  {
    for (CardFlow *flow = this->flows_.head(); flow; flow = flow->next)
      flow->withdraw();
  }

  void JsCardNode::onLifecycleFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                          loka::app::scene::NodeLifecycleFact next)
  {
    loka::app::scene::StdCompositionBoundaryNodeBase<JsCardProps>::onLifecycleFactChanged(previous, next);
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->phase_ = Detaching;
      this->withdrawFlows();
    }
  }

  void JsCardNode::revoke()
  {
    this->phase_ = Revoked;
    if (JS_IsObject(this->capability_))
      JS_SetOpaque(this->capability_, 0);
  }

  JsCardNode::~JsCardNode()
  {
    this->revoke();
#ifdef TEST_BUILD
    delete this->scenario_;
#endif
    if (this->engine_)
    {
      JSContext *ctx = this->engine_->context();
      JS_FreeValue(ctx, this->capability_);
      JS_FreeValue(ctx, instance_);
      JS_FreeValue(ctx, tree_);
      JS_FreeValue(ctx, onAttach_);
      JS_FreeValue(ctx, onDetach_);
      JS_FreeValue(ctx, errorSeat_);
    }
  }
  bool JsCardNode::flowLive() const
  {
    return this->phase_ == Live && !this->failed_ && this->scene();
  }
  JSValue JsCardNode::typedFactory(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int kind, JSValue *data)
  {
    JsCardNode *card = capabilityNode(data[0]);
    if (kind == JsSeatRecord::IMAGE && (argc > 1 || (argc == 1 && !JS_IsString(argv[0]))))
      return JS_ThrowTypeError(ctx, "c.state.image(name?) takes an optional picture name");
    if (!card || (kind != JsSeatRecord::IMAGE && argc))
      return JS_ThrowTypeError(ctx, "typed state factory requires a card constructor and no arguments");
    return card->mintTypedState(ctx, static_cast<JsSeatRecord::Kind>(kind), argc ? argv[0] : JS_UNDEFINED);
  }
  JSValue JsCardNode::mintTypedState(JSContext *ctx, JsSeatRecord::Kind kind, JSValueConst name)
  {
    if (this->phase_ != Constructing)
      return JS_ThrowTypeError(ctx, "typed state is only valid in a card constructor");
    if (this->seats_.count() >= kCardSeatBudget)
      return JS_ThrowRangeError(ctx, "kCardSeatBudget exceeded (128 seats)");
    loka::core::resource::Image image;
    if (!JS_IsUndefined(name))
    {
      std::size_t length = 0;
      const char *bytes = JS_ToCStringLen(ctx, &length, name);
      if (!bytes)
        return JS_EXCEPTION;
      const std::string stem(bytes, length);
      JS_FreeCString(ctx, bytes);
      if (!ScriptRuntime::IsPictureName(stem))
        return JS_ThrowTypeError(ctx, "c.state.image: %s: invalid picture name", stem.c_str());
      PlatformContext *platform = this->props.runtime->nativeContext();
      if (!platform)
        return JS_ThrowTypeError(ctx, "c.state.image: %s: missing PlatformContext", stem.c_str());
      const loka::file::File file = loka::file::File::Application()
                                    << loka::file::File("Pictures")
                                    << loka::file::File(ScriptRuntime::PictureFileName(stem));
      loka::core::resource::Blob blob;
      const loka::platform::file::ReadResult read = loka::app::ReadFileImageBlob(platform, file, blob);
      if (read != loka::platform::file::READ_OK)
        return JS_ThrowTypeError(
            ctx, "c.state.image: Pictures/%s: %s", stem.c_str(), loka::app::FileImageReadResultName(read));
      if (!loka::app::DecodeFileImageBlob(platform, blob, image))
        return JS_ThrowTypeError(ctx, "c.state.image: Pictures/%s: image decode failure", stem.c_str());
    }
    JsSeatRecord *record = this->seats_.add(JsSeatRecord::Initial(ctx, kind));
    if (!record)
      return JS_ThrowOutOfMemory(ctx);
    if (kind == JsSeatRecord::FILE_RESULT)
      this->state(record->file, loka::app::FileChooserResult());
    else if (kind == JsSeatRecord::VIEWPORT)
      this->state(record->viewport, loka::app::ScrollViewProps::topOrigin());
    else
      this->state(record->image, image);
    return this->finishSeat(ctx, record);
  }
  JSValue JsCardNode::nativeLoadImage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
  {
    JsCardNode *card = capabilityNode(data[0]);
    if (!card || argc != 1)
      return JS_ThrowTypeError(ctx, "loadImage requires a bound card and one File handle");
    return card->loadImage(ctx, argv[0]);
  }
  JSValue JsCardNode::loadImage(JSContext *ctx, JSValueConst value)
  {
    loka::core::resource::Image unused;
    loka::file::File file;
    if (!this->flowAdmission_.resolve(ctx, *this, value, JsSeatRecord::FILE_RESULT, unused, file)
        || !this->flowAdmission_.capacity(ctx))
      return JS_EXCEPTION;
    PlatformContext *platform = this->props.runtime->nativeContext();
    if (!platform)
      return JS_ThrowTypeError(ctx, "loadImage requires ScriptRuntime PlatformContext");
    loka::core::resource::Blob blob;
    const loka::platform::file::ReadResult read = loka::app::ReadFileImageBlob(platform, file, blob);
    loka::core::resource::Image image;
    const bool decoded = read == loka::platform::file::READ_OK
                         && loka::app::DecodeFileImageBlob(platform, blob, image);
    // Gate before creating a JS result or exception, including native failure.
    if (capabilityNode(this->capability_) != this || !this->flowLive())
      return JS_ThrowTypeError(ctx, "loadImage card is no longer Live");
    if (read != loka::platform::file::READ_OK)
      return JS_ThrowTypeError(ctx, "loadImage: %s", loka::app::FileImageReadResultName(read));
    if (!decoded)
      return JS_ThrowTypeError(ctx, "loadImage: image decode failure");
    return this->flowAdmission_.project(ctx, image);
  }

  JSValue JsCardNode::mintState(JSContext *ctx, JSValueConst initial)
  {
    if (this->phase_ != Constructing)
    {
      fail("c.state() is only valid in a constructor; the card has failed and undefined is returned.");
      return JS_UNDEFINED;
    }
    if (this->seats_.count() >= kCardSeatBudget)
    {
      fail("kCardSeatBudget exceeded (128 seats).");
      return JS_UNDEFINED;
    }
    if (!JS_IsString(initial) && !JS_IsNumber(initial) && !JS_IsBool(initial))
    {
      fail("state() initial value is unsupported.");
      return JS_UNDEFINED;
    }
    JsSeatRecord *record = this->seats_.add(JsSeatRecord::Initial(ctx,
                                                                  JS_IsString(initial)   ? JsSeatRecord::STRING
                                                                  : JS_IsNumber(initial) ? JsSeatRecord::INTEGER
                                                                                         : JsSeatRecord::BOOLEAN));
    if (!record)
    {
      fail("Could not allocate state seat.");
      return JS_UNDEFINED;
    }
    if (JS_IsString(initial))
    {
      size_t length = 0;
      const char *text = JS_ToCStringLen(ctx, &length, initial);
      if (!text)
      {
        fail("state() initial value is unsupported.");
        return JS_UNDEFINED;
      }
      this->state(record->string, loka::core::String::Utf8(text, length));
      JS_FreeCString(ctx, text);
    }
    else if (JS_IsNumber(initial))
    {
      int32_t value = 0;
      if (!integerNumber(ctx, initial, value))
      {
        fail("state(number) requires an integer");
        return JS_UNDEFINED;
      }
      this->state(record->integer, static_cast<int>(value));
      this->derived(record->formatted, record->integer, new (std::nothrow) FormatIntEval(&record->integer));
    }
    else if (JS_IsBool(initial))
    {
      this->state(record->boolean, JS_ToBool(ctx, initial) != 0);
      this->derived(record->formatted, record->boolean, new (std::nothrow) FormatBoolEval(&record->boolean));
    }
    else
    {
      fail("state() initial value is unsupported.");
      return JS_UNDEFINED;
    }
    return this->finishSeat(ctx, record);
  }
  JSValue JsCardNode::finishSeat(JSContext *ctx, JsSeatRecord *record)
  {
    JSValue seat = JS_NewObject(ctx);
    if (JS_IsException(seat))
      return seat;
    if (!installFunction(ctx, seat, "get", JS_NewCFunctionData(ctx, seatNative, 0, SeatGet, 1, &this->capability_))
        || !installFunction(ctx, seat, "set", JS_NewCFunctionData(ctx, seatNative, 1, SeatSet, 1, &this->capability_)))
    {
      JS_FreeValue(ctx, seat);
      return JS_EXCEPTION;
    }
    record->value = JS_DupValue(ctx, seat);
    if (JS_FreezeObject(ctx, seat) < 0)
    {
      JS_FreeValue(ctx, seat);
      return JS_EXCEPTION;
    }
    return seat;
  }
  JSValue JsCardNode::seatGet(JSContext *ctx, JSValueConst seat)
  {
    JsSeatRecord *record = this->findSeat(ctx, seat);
    if (record && !record->isMaterialized())
      return JS_ThrowTypeError(ctx, "state seat is not materialized");
    if (record)
    {
      if (record->kind == JsSeatRecord::STRING)
        return jsString(ctx, record->string.get());
      if (record->kind == JsSeatRecord::INTEGER)
        return JS_NewInt32(ctx, record->integer.get());
      if (record->kind == JsSeatRecord::BOOLEAN)
        return JS_NewBool(ctx, record->boolean.get());
      if (record->kind == JsSeatRecord::VIEWPORT)
      {
        const loka::core::Frame value = record->viewport.get();
        const loka::core::Frame fact =
            value == loka::app::ScrollViewProps::topOrigin() ? loka::core::Frame(0, 0, 0, 0) : value;
        JSValue snapshot = JS_NewObject(ctx);
        if (JS_IsException(snapshot))
          return snapshot;
        if (JS_DefinePropertyValueStr(ctx, snapshot, "x", JS_NewInt32(ctx, fact.x), JS_PROP_C_W_E) < 0
            || JS_DefinePropertyValueStr(ctx, snapshot, "y", JS_NewInt32(ctx, fact.y), JS_PROP_C_W_E) < 0
            || JS_DefinePropertyValueStr(ctx, snapshot, "width", JS_NewInt32(ctx, fact.width), JS_PROP_C_W_E) < 0
            || JS_DefinePropertyValueStr(ctx, snapshot, "height", JS_NewInt32(ctx, fact.height), JS_PROP_C_W_E) < 0
            || JS_FreezeObject(ctx, snapshot) < 0)
        {
          JS_FreeValue(ctx, snapshot);
          return JS_EXCEPTION;
        }
        return snapshot;
      }
      if (!this->flowLive())
        return JS_ThrowTypeError(ctx, "typed seat requires a Live mounted card");
      if (record->kind == JsSeatRecord::IMAGE)
      {
        if (!this->flowAdmission_.hasExecution())
          return JS_ThrowTypeError(ctx, "image.get requires a CardFlow execution");
        const loka::core::resource::Image image = record->image.get();
        return image.isValid() ? this->flowAdmission_.project(ctx, image) : JS_NULL;
      }
      const loka::app::FileChooserResult result = record->file.get();
      JSValue file = result.kind == loka::app::FileChooserResult::RESULT_FILE
                             && this->flowAdmission_.hasExecution()
                         ? this->flowAdmission_.project(ctx, result.item) : JS_NULL;
      if (JS_IsException(file))
        return file;
      JSValue facts = JS_NewObject(ctx);
      if (JS_IsException(facts))
      {
        JS_FreeValue(ctx, file);
        return facts;
      }
      bool ok = JS_SetPropertyStr(ctx, facts, "file", file) >= 0
                && JS_SetPropertyStr(ctx, facts, "kind", JS_NewInt32(ctx, result.kind)) >= 0
                && JS_SetPropertyStr(ctx, facts, "isFile", JS_NewBool(ctx, result.kind == loka::app::FileChooserResult::RESULT_FILE)) >= 0
                && JS_SetPropertyStr(ctx, facts, "isCanceled", JS_NewBool(ctx, result.kind == loka::app::FileChooserResult::RESULT_CANCELED)) >= 0
                && JS_SetPropertyStr(ctx, facts, "isError", JS_NewBool(ctx, result.kind == loka::app::FileChooserResult::RESULT_ERROR)) >= 0
                && JS_FreezeObject(ctx, facts) >= 0;
      if (!ok)
      {
        JS_FreeValue(ctx, facts);
        return JS_EXCEPTION;
      }
      return facts;
    }
    return JS_ThrowTypeError(ctx, "unknown state seat");
  }
  JSValue JsCardNode::seatSet(JSContext *ctx, JSValueConst seat, JSValueConst value)
  {
    JsSeatRecord *record = this->findSeat(ctx, seat);
    if (record && record->writeRefusal())
      return JS_ThrowTypeError(ctx, "%s", record->writeRefusal());
    if (record && !record->isMaterialized())
      return JS_ThrowTypeError(ctx, "state seat is not materialized");
    if (record)
    {
      if (record->kind == JsSeatRecord::IMAGE)
      {
        if (!this->flowLive())
          return JS_ThrowTypeError(ctx, "image.set requires a Live mounted card");
        loka::core::resource::Image image;
        loka::file::File file;
        if (!JS_IsNull(value) && !this->flowAdmission_.resolve(ctx, *this, value, JsSeatRecord::IMAGE, image, file))
          return JS_EXCEPTION;
        loka::core::StateTrackerGuard transaction(this->tracker());
        record->image.set(image, true);
        return JS_UNDEFINED;
      }
      // Same-value writes are silent. An unchanged write would still commit the
      // seat (State::set marks it), so Show seats and watches over it would
      // re-run a layout for nothing; a Flow that republishes a derived fact
      // (LAPTOP.JS's narrow/wide pair) drew SmirkyTracker's list twice.
      if (record->kind == JsSeatRecord::STRING && JS_IsString(value))
      {
        size_t n = 0;
        const char *s = JS_ToCStringLen(ctx, &n, value);
        if (!s)
          return JS_EXCEPTION;
        const loka::core::String next = loka::core::String::Utf8(s, n);
        JS_FreeCString(ctx, s);
        if (!loka::core::stateValuesEqual(record->string.get(), next))
          record->string.writeSeat().set(next);
        return JS_UNDEFINED;
      }
      if (record->kind == JsSeatRecord::INTEGER && JS_IsNumber(value))
      {
        int32_t v = 0;
        if (!integerNumber(ctx, value, v))
        {
          error_.set(loka::core::String::Literal("state(number) requires an integer"));
          return JS_UNDEFINED;
        }
        if (record->integer.get() != static_cast<int>(v))
          record->integer.writeSeat().set(static_cast<int>(v));
        return JS_UNDEFINED;
      }
      if (record->kind == JsSeatRecord::BOOLEAN && JS_IsBool(value))
      {
        const bool next = JS_ToBool(ctx, value) != 0;
        if (record->boolean.get() != next)
          record->boolean.writeSeat().set(next);
        return JS_UNDEFINED;
      }
      fail("state seat type mismatch.");
      return JS_UNDEFINED;
    }
    return JS_ThrowTypeError(ctx, "unknown state seat");
  }
  JSValue JsCardNode::errorSeatGet(JSContext *ctx)
  {
    if (!this->error_.isValid())
      return JS_ThrowTypeError(ctx, "state seat is not materialized");
    return jsString(ctx, this->error_.get());
  }
  void JsCardNode::requestGo(const char *name, size_t length, const CardCarry &carry)
  {
    SmirkyCardId card = length == 5 && !memcmp(name, "first", 5)    ? SMIRKY_CARD_FIRST
                        : length == 6 && !memcmp(name, "second", 6) ? SMIRKY_CARD_SECOND
                                                                    : SMIRKY_CARD_ERROR;
    if (card == SMIRKY_CARD_ERROR)
    {
      fail("go() requires first or second.");
      return;
    }
    requestGo(card, carry);
  }
  void JsCardNode::requestGo(SmirkyCardId card, const CardCarry &carry)
  {
    if (this->phase_ != Live)
      return;
    CardScene *scene = static_cast<CardScene *>(this->scene());
    if (!scene)
    {
      // The detach line clears the scene before detachNode runs, so go()
      // from onDetach has nowhere to hand the next card.
      fail("go() is unavailable while the card is detaching.");
      return;
    }
    CardScene *next = CreateCard(card, *props.runtime, carry);
    if (next)
    {
      this->phase_ = TransitionPending;
      scene->replaceWith(next);
    }
    else
      fail("Could not create the next card.");
  }
  void JsCardNode::requestReload()
  {
    this->reloadWithCarry(CardCarry());
  }
  void JsCardNode::reloadWithCarry(const CardCarry &carry)
  {
    if (this->phase_ != Live)
      return;
    CardScene *scene = static_cast<CardScene *>(this->scene());
    if (!scene)
    {
      fail("reload() is unavailable while the card is detaching.");
      return;
    }
    loka::core::String error;
    JsEngine *candidate = props.runtime->prepareReload(props.card, error);
    if (!candidate)
    {
      fail(error);
      return;
    }
    // Build the replacement Scene first; the engine swap and the visible card
    // then change together, or neither does.
    CardScene *next = CreateCard(props.card, *props.runtime, carry);
    if (!next)
    {
      props.runtime->discardReload(candidate);
      fail("Could not create the reloaded card.");
      return;
    }
    props.runtime->commitReload(candidate);
    this->phase_ = TransitionPending;
    scene->replaceWith(next);
  }
  void JsCardNode::requestOpen(const char *name, size_t length, const CardCarry &carry)
  {
    if (this->phase_ != Live)
      return;
    const loka::core::String prefix = loka::core::String::Utf8(name, length) + loka::core::String::Literal(": ");
    CardScene *scene = static_cast<CardScene *>(this->scene());
    if (!scene)
    {
      fail(prefix + loka::core::String::Literal("open() is unavailable while the card is detaching."));
      return;
    }
    loka::core::String error;
    JsEngine *candidate = props.runtime->prepareOpen(std::string(name, length), error);
    if (!candidate)
    {
      fail(error);
      return;
    }
    // As in requestReload above, admission constructs the node after the commit.
    CardScene *next = CreateCard(SMIRKY_CARD_FIRST, *props.runtime, carry);
    if (!next)
    {
      props.runtime->discardReload(candidate);
      fail(prefix + loka::core::String::Literal("Could not create the opened card."));
      return;
    }
    props.runtime->commitReload(candidate);
    this->phase_ = TransitionPending;
    scene->replaceWith(next);
  }
  void JsCardNode::declareBindings(loka::app::scene::BindingToken &t)
  {
    t.action(reloadEmitter_, this, &JsCardNode::requestReload);
    for (CardFlow *flow = this->flows_.head(); !this->failed_ && flow; flow = flow->next)
      if (!flow->activate(t))
        this->fail("Could not activate card Flow.");
    if (this->failed_)
      this->withdrawFlows();
  }
  // The card hooks ride the root boundary's own attach/detach doors
  // (ComponentNode::composeWithContext -> attachNode/detachNode), which every
  // path reaches: first mount, Scene::updateAttached, unmount and the
  // SceneManager switch. The lifecycle fact is not that door: a root starts
  // ATTACHED and teardown marks it RETIRED directly.
  JSValue JsCardNode::callHook(JSValueConst hook)
  {
    JSContext *context = this->engine_->context();
    return JS_IsFunction(context, hook) ? JS_Call(context, hook, this->instance_, 0, 0) : JS_UNDEFINED;
  }
  void JsCardNode::finishCall(JSValue result)
  {
    if (JS_IsException(result))
    {
      loka::core::String error;
      props.runtime->captureException(*this->engine_, error);
      this->error_.set(error);
    }
    JS_FreeValue(this->engine_->context(), result);
  }
  void JsCardNode::attachNode(loka::app::scene::NodeComposition &composition)
  {
    loka::app::scene::StdCompositionBoundaryNodeBase<JsCardProps>::attachNode(composition);
#ifdef TEST_BUILD
    if (this->scenario_)
      this->scenario_->attach(static_cast<CardScene *>(this->scene()));
#endif
    if (!this->failed_ && this->phase_ == Live && this->engine_ && this->engine_->context())
    {
      ScriptRuntime::InterruptWindow interrupt(*props.runtime, *this->engine_);
      JSContext *ctx = this->engine_->context();
      // Hook getters may read declared seats, just like the compose getter.
      const char *properties[] = {"onAttach", "onDetach"};
      JSValue *slots[] = {&this->onAttach_, &this->onDetach_};
      for (int i = 0; !this->failed_ && i < 2; ++i)
      {
        *slots[i] = JS_GetPropertyStr(ctx, this->instance_, properties[i]);
        if (JS_IsException(*slots[i]))
        {
          loka::core::String error;
          props.runtime->captureException(*this->engine_, error);
          this->fail(error);
        }
        else if (!JS_IsFunction(ctx, *slots[i]) && !JS_IsUndefined(*slots[i]))
          this->fail("card(name, F): F(c) must return an object with compose()");
      }
      if (!this->failed_)
        this->finishCall(this->callHook(this->onAttach_));
    }
  }
  void JsCardNode::detachNode(loka::app::scene::NodeComposition &composition)
  {
    // Before the base drops the owner slots, so the hook can still write seats.
    this->phase_ = Detaching;
    this->withdrawFlows();
#ifdef TEST_BUILD
    if (this->scenario_)
      this->scenario_->cancel();
#endif
    if (this->engine_ && this->engine_->context())
    {
      ScriptRuntime::InterruptWindow interrupt(*props.runtime, *this->engine_);
      JSValue result = this->callHook(this->onDetach_);
      this->revoke();
      // Exception formatting can reenter JS, so it runs after synchronous revocation.
      this->finishCall(result);
    }
    else
      this->revoke();
    loka::app::scene::StdCompositionBoundaryNodeBase<JsCardProps>::detachNode(composition);
#ifdef TEST_BUILD
    if (this->scenario_)
      this->scenario_->finishDetach();
#endif
  }
  void JsCardNode::composeNode(loka::app::scene::NodeComposition &c)
  {
    using namespace loka::app;
    // A MAIN.JS startup failure is the runtime's fact; the card only shows it.
    {
      const loka::core::String mainError = props.runtime->mainErrorFor(props.card);
      if (!mainError.empty())
        error_.set(mainError);
    }
    if (!failed_ && JS_IsUndefined(tree_))
    {
      JSContext *ctx = this->engine_->context();
      ScriptRuntime::InterruptWindow interrupt(*props.runtime, *this->engine_);
      JSValue compose = JS_GetPropertyStr(ctx, this->instance_, "compose");
      loka::core::String error;
      if (JS_IsException(compose))
      {
        props.runtime->captureException(*this->engine_, error);
        this->fail(error);
      }
      else if (!JS_IsFunction(ctx, compose))
        this->fail("card(name, F): F(c) must return an object with compose()");
      else
      {
        ComposeDelegate delegate(ctx, this->capability_);
        JSValue argument = delegate.value();
        JSValue result = JS_IsException(argument) ? JS_EXCEPTION : JS_Call(ctx, compose, this->instance_, 1, &argument);
        delegate.close();
        if (JS_IsException(result))
        {
          props.runtime->captureException(*this->engine_, error);
          fail(error.empty() ? loka::core::String::Literal("JavaScript compose() failed.") : error);
        }
        else if (JS_IsObject(result))
          setComposeTree(ctx, result);
        else if (JS_IsUndefined(tree_))
          fail("JavaScript compose() must return or declare a tree.");
        JS_FreeValue(ctx, result);
      }
      JS_FreeValue(ctx, compose);
    }
    if (failed_)
    {
      error_.set(failure_);
      this->declareRefusal(c);
      return;
    }
    ScriptRuntime::InterruptWindow interrupt(*props.runtime, *this->engine_);
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> definition(lower(this->engine_->context(), tree_, 0));
    if (!definition.isSet())
    {
      this->declareRefusal(c);
      return;
    }
    c.declare(*definition.get());
  }
  void JsCardNode::fail(const char *message)
  {
    fail(loka::core::String::Literal(message));
  }
  void JsCardNode::declareRefusal(loka::app::scene::NodeComposition &c)
  {
    using namespace loka::app;
    // The card window has no outer margin and a refused card cannot pad itself.
    c.declare(Box().padding(StandardWindowMargin())
              << (VStack() << Text(error_.state()).TEST_ID("SmirkyCard.Status")
                           << Button("Reload MAIN.JS", &reloadEmitter_).TEST_ID("SmirkyCard.Reload")));
  }
  void JsCardNode::fail(const loka::core::String &message)
  {
    failed_ = true;
    failure_ = message;
    if (error_.isValid())
      error_.set(failure_);
  }
  bool JsCardNode::setComposeTree(JSContext *ctx, JSValueConst value)
  {
    if (!JS_IsObject(value) || JS_IsArray(value))
    {
      fail("JavaScript compose tree must be an object.");
      return false;
    }
    if (!JS_IsUndefined(tree_))
    {
      fail("JavaScript compose() declared more than one tree.");
      return false;
    }
    tree_ = JS_DupValue(ctx, value);
    return true;
  }
  namespace
  {
    // Descriptor payloads are data, not getters that can change between validation and use.
    JSValue toolbarValue(JSContext *ctx, JSValueConst tree, const char *name)
    {
      JSAtom atom = JS_NewAtom(ctx, name);
      if (atom == JS_ATOM_NULL)
        return JS_EXCEPTION;
      JSPropertyDescriptor property;
      const int found = JS_GetOwnProperty(ctx, &property, tree, atom);
      JS_FreeAtom(ctx, atom);
      if (found <= 0)
        return found < 0 ? JS_EXCEPTION : JS_UNDEFINED;
      const bool accessor = !JS_IsUndefined(property.getter) || !JS_IsUndefined(property.setter);
      JS_FreeValue(ctx, property.getter);
      JS_FreeValue(ctx, property.setter);
      if (accessor)
      {
        JS_FreeValue(ctx, property.value);
        return JS_ThrowTypeError(ctx, "Toolbar descriptor requires data properties");
      }
      return property.value;
    }
    JSValue snapshotToolbarDescriptor(JSContext *ctx, JSValueConst tree)
    {
      if (!requirePlainObject(ctx, tree, "Toolbar descriptor"))
        return JS_EXCEPTION;
      JsOwnProperties names(ctx);
      if (!names.read(tree, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return JS_EXCEPTION;
      JSValue copy = JS_NewObjectProto(ctx, JS_NULL);
      if (JS_IsException(copy))
        return copy;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSPropertyDescriptor property;
        const int found = JS_GetOwnProperty(ctx, &property, tree, names.atom(i));
        if (found <= 0)
        {
          JS_FreeValue(ctx, copy);
          return JS_EXCEPTION;
        }
        const bool accessor = !JS_IsUndefined(property.getter) || !JS_IsUndefined(property.setter);
        JS_FreeValue(ctx, property.getter);
        JS_FreeValue(ctx, property.setter);
        if (accessor)
        {
          JS_FreeValue(ctx, property.value);
          JS_FreeValue(ctx, copy);
          return JS_ThrowTypeError(ctx, "Toolbar descriptor requires data properties");
        }
        if (JS_DefinePropertyValue(ctx, copy, names.atom(i), property.value, JS_PROP_ENUMERABLE) < 0)
        {
          JS_FreeValue(ctx, copy);
          return JS_EXCEPTION;
        }
      }
      return copy;
    }
    bool treeString(JSContext *ctx,
                    JSValueConst tree,
                    const char *name,
                    loka::core::String &out,
                    bool rejectNul = false,
                    bool own = false)
    {
      JSValue value = own ? toolbarValue(ctx, tree, name) : JS_GetPropertyStr(ctx, tree, name);
      size_t length = 0;
      const char *text = JS_IsString(value) ? JS_ToCStringLen(ctx, &length, value) : 0;
      const bool accepted = text && (!rejectNul || strlen(text) == length);
      if (text)
      {
        if (accepted)
          out = loka::core::String::Utf8(text, length);
        JS_FreeCString(ctx, text);
      }
      JS_FreeValue(ctx, value);
      return accepted;
    }
  } // namespace
  loka::app::scene::NodeDefinitionBase *JsCardNode::lower(JSContext *ctx, JSValueConst tree, int depth)
  {
    using namespace loka::app;
    if (depth > kMaximumTreeDepth || !JS_IsObject(tree) || JS_IsArray(tree))
    {
      fail(depth > kMaximumTreeDepth ? "JavaScript tree exceeds depth 11." : "JavaScript tree has an invalid node.");
      return 0;
    }
    JSValue kindValue = JS_GetPropertyStr(ctx, tree, "kind");
    int32_t kind = 0;
    int converted = JS_ToInt32(ctx, &kind, kindValue);
    JS_FreeValue(ctx, kindValue);
    if (converted)
    {
      fail("JavaScript tree node has an unknown kind.");
      return 0;
    }
    IJsNodeLowering *lowering = props.runtime->registry_.lookup(kind);
    if (!lowering)
    {
      fail("JavaScript tree node has an unknown kind.");
      return 0;
    }
    loka::app::scene::NodeDefinitionBase *out = lowering->lower(*this, ctx, tree, depth);
    if (!out)
    {
      if (!failed_)
        fail("Could not allocate JavaScript tree.");
      return 0;
    }
    loka::core::String id;
    if (treeString(ctx, tree, "testId", id)
        && (!out->propsBase() || out->propsBase()->propsTypeId() != JsClickProps::staticTypeId()))
    {
      const loka::core::StringBuffer b = id.bufferWithEncoding(loka::core::StringEncodingUtf8);
      const std::string utf8(static_cast<const char *>(b.data()), b.length());
      out->setTestId(utf8.c_str());
    }
    return out;
  }
  bool requirePlainObject(JSContext *ctx, JSValueConst dict, const char *label)
  {
    // Compare to an ordinary object without consulting replaceable JS globals.
    JSValue plain = JS_NewObject(ctx);
    if (JS_IsException(plain))
      return false;
    JSValue prototype = JS_IsObject(dict) ? JS_GetPrototype(ctx, dict) : JS_UNDEFINED;
    JSValue plainPrototype = JS_GetPrototype(ctx, plain);
    const bool valid = JS_IsObject(dict) && JS_GetClassID(dict) == JS_GetClassID(plain)
                       && (JS_IsNull(prototype) || JS_IsStrictEqual(ctx, prototype, plainPrototype));
    const bool exception = JS_IsException(prototype) || JS_IsException(plainPrototype);
    JS_FreeValue(ctx, plainPrototype);
    JS_FreeValue(ctx, prototype);
    JS_FreeValue(ctx, plain);
    if (exception)
      return false;
    if (!valid)
    {
      JS_ThrowTypeError(ctx, "%s requires a plain object", label);
      return false;
    }
    return true;
  }

  template <typename Row, size_t N> const Row *findStyleName(const char *name, size_t length, const Row (&values)[N])
  {
    for (size_t i = 0; name && i < N; ++i)
      if (length == strlen(values[i].name) && !memcmp(name, values[i].name, length))
        return &values[i];
    return 0;
  }

  bool readTextStyle(JSContext *ctx, JSValueConst dict, loka::app::TextStyle &out)
  {
    using namespace loka::app;
    if (!requirePlainObject(ctx, dict, "Text style"))
      return false;
    JsOwnProperties names(ctx);
    if (!names.read(dict, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
      return false;
    loka::app::TextStyle result;
    for (uint32_t i = 0; i < names.count(); ++i)
    {
      JSValue key = JS_AtomToValue(ctx, names.atom(i));
      size_t length = 0;
      const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
      JS_FreeValue(ctx, key);
      if (!name)
      {
        JS_ThrowTypeError(ctx, "Unknown Text style key");
        return false;
      }
      const bool size = length == 4 && !memcmp(name, "size", 4);
      const bool weight = length == 6 && !memcmp(name, "weight", 6);
      const bool family = length == 6 && !memcmp(name, "family", 6);
      const bool color = length == 5 && !memcmp(name, "color", 5);
      const bool italic = length == 6 && !memcmp(name, "italic", 6);
      const bool underline = length == 9 && !memcmp(name, "underline", 9);
      JS_FreeCString(ctx, name);
      if (!size && !weight && !italic && !underline && !color && !family)
      {
        JS_ThrowTypeError(ctx, "Unknown Text style key");
        return false;
      }
      JSValue value = JS_GetProperty(ctx, dict, names.atom(i));
      if (JS_IsException(value))
        return false;
      bool accepted = false;
      if (size && !JS_IsString(value))
      {
        int32_t number = 0;
        accepted = integerNumber(ctx, value, number);
        if (accepted)
          result = result + SizeOf(number);
      }
      else if (italic)
      {
        accepted = JS_IsBool(value);
        if (accepted)
          result.italic(JS_ToBool(ctx, value) != 0);
      }
      else if (underline)
      {
        accepted = JS_IsBool(value);
        if (accepted)
          result.underline(JS_ToBool(ctx, value) != 0);
      }
      else if (JS_IsString(value))
      {
        size_t valueLength = 0;
        const char *valueName = JS_ToCStringLen(ctx, &valueLength, value);
        if (!valueName)
        {
          JS_FreeValue(ctx, value);
          return false;
        }
        if (size)
        {
          const detail::TextSizeRoleName *entry =
              findStyleName(valueName, valueLength, detail::StyleVocabularySizeRoles);
          if (entry)
          {
            result.size_ = TextSizeValue::role(entry->value);
            accepted = true;
          }
        }
        else if (weight)
        {
          const detail::TextWeightName *entry = findStyleName(valueName, valueLength, detail::StyleVocabularyWeights);
          if (entry)
          {
            result.weight(entry->value);
            accepted = true;
          }
        }
        else if (family)
        {
          const detail::TextFamilyName *entry = findStyleName(valueName, valueLength, detail::StyleVocabularyFamily);
          if (entry)
          {
            result.family(entry->value);
            accepted = result.family_.isSet();
          }
        }
        else if (color)
        {
          const detail::TextColorName *entry = findStyleName(valueName, valueLength, detail::StyleVocabularyColors);
          if (entry)
          {
            result.color(entry->value);
            accepted = true;
          }
        }
        JS_FreeCString(ctx, valueName);
      }
      JS_FreeValue(ctx, value);
      if (!accepted)
      {
        JS_ThrowTypeError(ctx, "Invalid Text style value");
        return false;
      }
    }
    out = result;
    return true;
  }
  bool readBlockStyle(JSContext *ctx, JSValueConst dict, loka::app::BlockStyle &out)
  {
    using namespace loka::app;
    if (!requirePlainObject(ctx, dict, "Block style"))
      return false;
    JsOwnProperties names(ctx);
    if (!names.read(dict, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
      return false;
    BlockStyle result;
    for (uint32_t i = 0; i < names.count(); ++i)
    {
      JSValue key = JS_AtomToValue(ctx, names.atom(i));
      size_t keyLength = 0;
      const char *keyName = JS_IsString(key) ? JS_ToCStringLen(ctx, &keyLength, key) : 0;
      JS_FreeValue(ctx, key);
      const bool isAlign = keyName && keyLength == 5 && !memcmp(keyName, "align", 5);
      const bool isWrap = keyName && keyLength == 4 && !memcmp(keyName, "wrap", 4);
      const bool isTruncation = keyName && keyLength == 10 && !memcmp(keyName, "truncation", 10);
      const bool known = isAlign || isWrap || isTruncation;
      if (!known)
      {
        JS_FreeCString(ctx, keyName);
        JS_ThrowTypeError(ctx, "Unknown Block style key");
        return false;
      }
      JSValue value = JS_GetProperty(ctx, dict, names.atom(i));
      if (JS_IsException(value))
      {
        JS_FreeCString(ctx, keyName);
        return false;
      }
      size_t length = 0;
      const char *name = JS_IsString(value) ? JS_ToCStringLen(ctx, &length, value) : 0;
      bool accepted = false;
      if (isAlign)
      {
        const detail::TextAlignName *entry = findStyleName(name, length, detail::StyleVocabularyAlign);
        if (entry)
        {
          result.align(entry->value);
          accepted = true;
        }
      }
      else if (isWrap)
      {
        const detail::TextWrapName *entry = findStyleName(name, length, detail::StyleVocabularyWrap);
        if (entry)
        {
          result.wrap(entry->value);
          accepted = true;
        }
      }
      else if (isTruncation)
      {
        const detail::TextTruncationName *entry = findStyleName(name, length, detail::StyleVocabularyTruncation);
        if (entry)
        {
          result.truncation(entry->value);
          accepted = true;
        }
      }
      JS_FreeCString(ctx, name);
      JS_FreeCString(ctx, keyName);
      JS_FreeValue(ctx, value);
      if (!accepted)
      {
        JS_ThrowTypeError(ctx, "Invalid Block style value");
        return false;
      }
    }
    out = result;
    return true;
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerText(JSContext *ctx, JSValueConst tree)
  {
    using namespace loka::app;
    loka::app::TextStyle style;
    JSValue dict = JS_GetPropertyStr(ctx, tree, "style");
    bool valid = !JS_IsException(dict) && (JS_IsUndefined(dict) || readTextStyle(ctx, dict, style));
    JS_FreeValue(ctx, dict);
    BlockStyle block;
    if (valid)
    {
      dict = JS_GetPropertyStr(ctx, tree, "block");
      valid = !JS_IsException(dict) && (JS_IsUndefined(dict) || readBlockStyle(ctx, dict, block));
      JS_FreeValue(ctx, dict);
    }
    if (!valid)
    {
      loka::core::String error;
      props.runtime->captureException(*this->engine_, error);
      fail(error);
      return 0;
    }
    JSValue value = JS_GetPropertyStr(ctx, tree, "text");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    loka::app::scene::NodeDefinitionBase *out = 0;
    if (JS_IsStrictEqual(ctx, value, errorSeat_))
      out = new (std::nothrow) TextDefinitionWithAttr(Text(error_.state()) + style + block);
    else if (seat && (seat->kind == JsSeatRecord::STRING || seat->kind == JsSeatRecord::INTEGER
                      || seat->kind == JsSeatRecord::BOOLEAN))
      out = new (std::nothrow) TextDefinitionWithAttr(
          Text(seat->kind == JsSeatRecord::STRING ? seat->string.state() : seat->formatted.state()) + style + block);
    if (!out && JS_IsString(value))
    {
      loka::core::String text;
      treeString(ctx, tree, "text", text);
      out = new (std::nothrow) TextDefinitionWithAttr(Text(text) + style + block);
    }
    if (!out && !JS_IsString(value))
      fail("JavaScript Text requires a literal string or state seat.");
    JS_FreeValue(ctx, value);
    return out;
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerEditText(JSContext *ctx, JSValueConst tree)
  {
    JSValue value = JS_GetPropertyStr(ctx, tree, "seat");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    JS_FreeValue(ctx, value);
    if (!seat || seat->kind != JsSeatRecord::STRING)
    {
      fail("JavaScript EditText requires a String state seat.");
      return 0;
    }
    return new (std::nothrow) loka::app::EditText(seat->string);
  }
  loka::app::ScrollView *
  JsCardNode::createScrollView(JSContext *ctx, JSValueConst tree, const loka::core::Frame &initialViewport)
  {
    JSAtom key = JS_NewAtom(ctx, "viewport");
    const int supplied = JS_GetOwnProperty(ctx, 0, tree, key);
    JS_FreeAtom(ctx, key);
    if (supplied < 0)
    {
      this->fail("JavaScript ScrollView viewport lookup refused.");
      return 0;
    }
    JsSeatRecord *seat = 0;
    if (supplied)
    {
      JSValue value = JS_GetPropertyStr(ctx, tree, "viewport");
      seat = this->findSeat(ctx, value);
      JS_FreeValue(ctx, value);
      if (!seat || seat->kind != JsSeatRecord::VIEWPORT)
      {
        this->fail("JavaScript ScrollView viewport must be a c.state.viewport() seat of this card");
        return 0;
      }
      // A supplied seat needs no record: the seat owns the handle. Binding the
      // same seat twice is the app's choice, as with the C++ ScrollView(viewport);
      // a per-card ledger would outlive a refused declaration capture and turn
      // its retry into a false "already bound" (bot P2 on #1475).
      loka::app::ScrollView *bound = new (std::nothrow) loka::app::ScrollView(seat->viewport);
      if (!bound)
        this->fail("JavaScript ScrollView allocation refused.");
      return bound;
    }
    JsScrollViewport *record = this->scrollViewports_.add();
    if (!record)
    {
      this->fail("JavaScript ScrollView allocation refused.");
      return 0;
    }
    this->state(record->viewport, initialViewport);
    loka::app::ScrollView *scroll =
        record->viewport.isValid() ? new (std::nothrow) loka::app::ScrollView(record->viewport) : 0;
    if (!scroll)
      this->fail("JavaScript ScrollView allocation refused.");
    return scroll;
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerShow(JSContext *ctx, JSValueConst tree, int depth)
  {
    JSValue value = JS_GetPropertyStr(ctx, tree, "seat");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    JS_FreeValue(ctx, value);
    if (!seat || seat->kind != JsSeatRecord::BOOLEAN)
    {
      this->fail("JavaScript Show requires an own Bool state seat.");
      return 0;
    }
    JSValue childTree = JS_GetPropertyStr(ctx, tree, "child");
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> child(this->lowerChild(ctx, childTree, depth + 1));
    JS_FreeValue(ctx, childTree);
    if (!child.isSet())
      return 0;
    loka::app::ShowDefinition *show = new (std::nothrow) loka::app::ShowDefinition(seat->boolean.state());
    if (!show)
      return 0;
    *show << child.take();
    return show;
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerOpenFileDialog(JSContext *ctx, JSValueConst tree)
  {
    JSValue value = JS_GetPropertyStr(ctx, tree, "seat");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    JS_FreeValue(ctx, value);
    if (!seat || seat->kind != JsSeatRecord::FILE_RESULT)
    {
      this->fail("JavaScript OpenFileDialog requires an own File state seat.");
      return 0;
    }
    return new (std::nothrow) loka::app::OpenFileDialog(loka::app::OpenFileDialog().result(seat->file));
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerImageView(JSContext *ctx, JSValueConst tree)
  {
    JSValue value = JS_GetPropertyStr(ctx, tree, "seat");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    JS_FreeValue(ctx, value);
    if (!seat || seat->kind != JsSeatRecord::IMAGE)
    {
      this->fail("JavaScript ImageView requires an own Image state seat.");
      return 0;
    }
    loka::app::ImageViewProps props;
    props.image(seat->image.state());
    JSValue width = JS_GetPropertyStr(ctx, tree, "width");
    JSValue height = JS_GetPropertyStr(ctx, tree, "height");
    JSValue fit = JS_GetPropertyStr(ctx, tree, "fit");
    int32_t w = 0, h = 0, f = 0;
    const bool hasSize = !JS_IsUndefined(width) || !JS_IsUndefined(height);
    const bool hasFit = !JS_IsUndefined(fit);
    const bool valid =
        (!hasSize
         || (integerNumber(ctx, width, w) && integerNumber(ctx, height, h) && w >= 1 && w <= 4096 && h >= 1
             && h <= 4096))
        && (!hasFit
            || (integerNumber(ctx, fit, f) && f >= loka::app::IMAGE_FIT_CONTAIN && f <= loka::app::IMAGE_FIT_STRETCH));
    JS_FreeValue(ctx, width);
    JS_FreeValue(ctx, height);
    JS_FreeValue(ctx, fit);
    if (!valid)
    {
      this->fail("JavaScript ImageView has invalid options.");
      return 0;
    }
    if (hasSize)
      props.size(w, h);
    if (hasFit)
      props.attr(loka::app::ImageViewAttr().fit(static_cast<loka::app::ImageFit>(f)));
    return new (std::nothrow) loka::app::ImageView(props);
  }
  bool JsCardBindingRegistry::readToolbarDescriptor(JSContext *ctx, JSValueConst tree, JsToolbarRow &row)
  {
    ScriptRuntime *runtime = static_cast<ScriptRuntime *>(JS_GetContextOpaque(ctx));
    if (!runtime || !requirePlainObject(ctx, tree, "Toolbar descriptor"))
      return false;
    JSValue kindValue = toolbarValue(ctx, tree, "kind");
    int32_t kind = 0;
    bool ok = JS_IsNumber(kindValue) && JS_ToInt32(ctx, &kind, kindValue) == 0;
    JS_FreeValue(ctx, kindValue);
    IJsNodeLowering *lowering = ok ? runtime->registry_.lookup(kind) : 0;
    if (!lowering)
      ok = false;
    else if (!strcmp(lowering->name, "ToolbarItem"))
      row.kind = JsToolbarRow::BUTTON;
    else if (!strcmp(lowering->name, "label"))
      row.kind = JsToolbarRow::LABEL;
    else if (!strcmp(lowering->name, "flexibleSpace"))
      row.kind = JsToolbarRow::FLEXIBLE_SPACE;
    else if (!strcmp(lowering->name, "fixedSpace"))
      row.kind = JsToolbarRow::FIXED_SPACE;
    else
      ok = false;
    if (!ok)
    {
      JS_ThrowTypeError(ctx, "Toolbar requires ToolbarItem descriptors");
      return false;
    }
    JsOwnProperties keys(ctx);
    if (!keys.read(tree, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
      return false;
    for (uint32_t i = 0; i < keys.count(); ++i)
    {
      JSValue key = JS_AtomToValue(ctx, keys.atom(i));
      size_t length = 0;
      const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
      JS_FreeValue(ctx, key);
      const bool text = row.kind == JsToolbarRow::BUTTON || row.kind == JsToolbarRow::LABEL;
      const bool known =
          name && strlen(name) == length
          && (!strcmp(name, "kind")
              || (text && (!strcmp(name, "label") || !strcmp(name, "testId") || !strcmp(name, "TEST_ID")))
              || (row.kind == JsToolbarRow::BUTTON
                  && (!strcmp(name, "handler") || !strcmp(name, "enabled") || !strcmp(name, "enabledSeat")))
              || (row.kind == JsToolbarRow::LABEL && !strcmp(name, "style"))
              || (row.kind == JsToolbarRow::FIXED_SPACE && !strcmp(name, "width")));
      if (name)
        JS_FreeCString(ctx, name);
      if (!known)
      {
        JS_ThrowTypeError(ctx, "Unknown ToolbarItem field");
        return false;
      }
    }
    if (row.kind == JsToolbarRow::BUTTON || row.kind == JsToolbarRow::LABEL)
    {
      JSValue label = toolbarValue(ctx, tree, "label");
      const bool literal = JS_IsString(label);
      ok = literal || (row.kind == JsToolbarRow::LABEL && JS_IsObject(label));
      JS_FreeValue(ctx, label);
      if (literal)
        ok = treeString(ctx, tree, "label", row.label, true, true);
      JSValue id = toolbarValue(ctx, tree, "testId");
      ok = ok && (JS_IsUndefined(id) || JS_IsString(id));
      JS_FreeValue(ctx, id);
      treeString(ctx, tree, "testId", row.testId, false, true);
    }
    switch (row.kind)
    {
    case JsToolbarRow::BUTTON:
    {
      JSValue handler = toolbarValue(ctx, tree, "handler");
      ok = ok && JS_IsFunction(ctx, handler);
      JS_FreeValue(ctx, handler);
      break;
    }
    case JsToolbarRow::LABEL:
    {
      JSValue style = toolbarValue(ctx, tree, "style");
      ok = ok && (JS_IsUndefined(style) || readTextStyle(ctx, style, row.style));
      JS_FreeValue(ctx, style);
      break;
    }
    case JsToolbarRow::FLEXIBLE_SPACE:
      break;
    case JsToolbarRow::FIXED_SPACE:
    {
      JSValue width = toolbarValue(ctx, tree, "width");
      double value = 0;
      ok = JS_IsNumber(width) && JS_ToFloat64(ctx, &value, width) == 0 && value >= 1 && value <= SHRT_MAX
           && value == static_cast<short>(value);
      JS_FreeValue(ctx, width);
      if (ok)
        row.width = static_cast<short>(value);
      break;
    }
    }
    if (!ok)
      JS_ThrowTypeError(ctx, "Invalid ToolbarItem payload");
    return ok;
  }

  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerToolbar(JSContext *ctx, JSValueConst tree, int depth)
  {
    JSValue children = toolbarValue(ctx, tree, "children");
    int64_t length = 0;
    bool ok = JS_IsArray(children) && JS_GetLength(ctx, children, &length) == 0 && length >= 0
              && length <= kMaximumTreeChildren && (!length || depth + 1 <= kMaximumTreeDepth);
    JsToolbarProps toolbar;
    unsigned buttons = 0;
    for (int64_t i = 0; ok && i < length; ++i)
    {
      JSValue original = JS_GetPropertyUint32(ctx, children, static_cast<uint32_t>(i));
      // Style getters may mutate the source descriptor. Hold its data properties
      // once so validation and seat/handler admission consume the same facts.
      JSValue child = snapshotToolbarDescriptor(ctx, original);
      JS_FreeValue(ctx, original);
      JsToolbarRow &row = toolbar.rows[toolbar.count];
      ok = !JS_IsException(child) && JsCardBindingRegistry::readToolbarDescriptor(ctx, child, row);
      if (ok && row.kind == JsToolbarRow::LABEL)
      {
        JSValue value = toolbarValue(ctx, child, "label");
        if (!JS_IsString(value))
        {
          JsSeatRecord *seat = this->findSeat(ctx, value);
          ok = seat && seat->kind == JsSeatRecord::STRING;
          if (ok)
            row.text = seat->string.state();
        }
        JS_FreeValue(ctx, value);
      }
      if (ok && row.kind == JsToolbarRow::BUTTON)
      {
        ok = ++buttons <= JS_TOOLBAR_SLOT_COUNT;
        JSValue enabled = toolbarValue(ctx, child, "enabledSeat");
        if (ok && !JS_IsUndefined(enabled))
        {
          JsSeatRecord *seat = this->findSeat(ctx, enabled);
          ok = seat && seat->kind == JsSeatRecord::BOOLEAN;
          if (ok)
            row.enabled = seat->boolean.state();
        }
        JS_FreeValue(ctx, enabled);
        JSValue handler = toolbarValue(ctx, child, "handler");
        if (ok && JS_IsFunction(ctx, handler))
          row.handler = this->addHandler(ctx, handler);
        ok = ok && row.handler;
        JS_FreeValue(ctx, handler);
      }
      JS_FreeValue(ctx, child);
      if (ok)
        ++toolbar.count;
    }
    JS_FreeValue(ctx, children);
    if (!ok)
    {
      JS_FreeValue(ctx, JS_GetException(ctx));
      if (!this->failed_)
        this->fail("JavaScript Toolbar has invalid descriptors, seats, count or depth.");
      return 0;
    }
    return loka::app::scene::Component(toolbar).clone();
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerClickable(JSContext *ctx, JSValueConst tree, bool cell)
  {
    JSValue value = JS_GetPropertyStr(ctx, tree, "label");
    JsSeatRecord *seat = this->findSeat(ctx, value);
    loka::core::String label;
    const bool validLabel = (seat && seat->kind == JsSeatRecord::STRING) || treeString(ctx, tree, "label", label);
    JS_FreeValue(ctx, value);
    if (!validLabel)
    {
      fail("JavaScript clickable requires a literal string or String state seat.");
      return 0;
    }
    loka::core::State<bool> *enabledState = 0;
    JSValue enabled = JS_GetPropertyStr(ctx, tree, "enabledSeat");
    if (!JS_IsUndefined(enabled))
    {
      JsSeatRecord *enabledSeat = this->findSeat(ctx, enabled);
      if (cell || !enabledSeat || enabledSeat->kind != JsSeatRecord::BOOLEAN)
      {
        JS_FreeValue(ctx, enabled);
        fail("JavaScript Button enabled() requires a Bool state seat.");
        return 0;
      }
      enabledState = enabledSeat->boolean.state();
    }
    JS_FreeValue(ctx, enabled);
    JSValue handler = JS_GetPropertyStr(ctx, tree, "handler");
    JsHandlerRecord *record = JS_IsFunction(ctx, handler) ? this->addHandler(ctx, handler) : 0;
    JS_FreeValue(ctx, handler);
    if (!record)
    {
      if (!failed_)
        fail("JavaScript clickable requires a handler.");
      return 0;
    }
    loka::core::String testId;
    treeString(ctx, tree, "testId", testId);
    return loka::app::scene::Component(JsClickProps(label,
                                                    seat ? seat->string.state() : 0,
                                                    enabledState,
                                                    cell ? JsClickProps::CELL : JsClickProps::BUTTON,
                                                    record,
                                                    testId))
        .clone();
  }
  loka::app::scene::NodeDefinitionBase *JsCardNode::lowerChild(JSContext *ctx, JSValueConst tree, int depth)
  {
    return lower(ctx, tree, depth);
  }
  JsSeatRecord *JsCardNode::findSeat(JSContext *ctx, JSValueConst value) const
  {
    for (JsSeatRecord *seat = this->seats_.head(); seat; seat = seat->next)
      if (JS_IsStrictEqual(ctx, value, seat->value))
        return seat;
    return 0;
  }
  JsHandlerRecord *JsCardNode::addHandler(JSContext *ctx, JSValueConst handler)
  {
    if (this->handlers_.count() >= kCardClickableBudget)
    {
      fail("kCardClickableBudget exceeded (128 clickables).");
      return 0;
    }
    JsHandlerRecord *record = this->handlers_.add(JsHandlerRecord::Initial(this, ctx, handler));
    if (!record)
      fail("Could not allocate clickable handler.");
    return record;
  }
  void JsCardNode::fire(const JsHandlerRecord &handler)
  {
    if (this->phase_ != Live)
      return;
    ScriptRuntime::InterruptWindow interrupt(*props.runtime, *this->engine_);
    this->finishCall(JS_Call(this->engine_->context(), handler.value, this->instance_, 0, 0));
  }
  CardScene *CreateCard(SmirkyCardId card, ScriptRuntime &runtime, const CardCarry &carry)
  {
    switch (card)
    {
    case SMIRKY_CARD_ERROR:
      return 0;
    case SMIRKY_CARD_FIRST:
    case SMIRKY_CARD_SECOND:
      break;
    }
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root;
    root.reset(loka::app::scene::Boundary<JsCardNode>(JsCardProps(&runtime, card, carry)).clone());
    if (!root.isSet())
      return 0;
    CardScene *scene = new (std::nothrow) CardScene(root.get());
    if (scene)
      root.take();
    return scene;
  }
} // namespace smirkycard
