#include "CardFlow.hpp"
#include "CardNodes.hpp"
#include "JsNativeClass.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cassert>
#include <cstdio>

namespace smirkycard
{
  namespace
  {
    JSClassID handleClassId = 0;
    /** Non-owning integer identity; QuickJS collection never releases payloads. */
    struct HandleIdentity
    {
      uint32_t serial;
      unsigned slot;
      JsSeatRecord::Kind kind;
    };
    void finalizeHandle(JSRuntime *runtime, JSValue value)
    {
      js_free_rt(runtime, JS_GetOpaque(value, handleClassId));
    }
  }
  bool CardFlow::Admission::installHandles(JSRuntime *runtime)
  {
    JSClassDef def;
    memset(&def, 0, sizeof(def));
    def.class_name = "SmirkyExecutionHandle";
    def.finalizer = finalizeHandle;
    return RegisterJsNativeClass(runtime, handleClassId, def) != 0;
  }
  bool CardFlow::Admission::capacity(JSContext *ctx) const
  {
    if (!this->active_)
    {
      JS_ThrowTypeError(ctx, "native handle requires an active CardFlow execution");
      return false;
    }
    return this->active_->capacity(ctx);
  }
  bool CardFlow::Admission::Execution::capacity(JSContext *ctx) const
  {
    if (this->occupied_ == 4)
    {
      JS_ThrowRangeError(ctx, "CardFlow handle slot capacity exceeded (4)");
      return false;
    }
    return true;
  }
  JSValue CardFlow::Admission::publish(JSContext *ctx, JsSeatRecord::Kind kind,
                                     const loka::core::resource::Image &image, const loka::file::File &file)
  {
    if (!this->capacity(ctx))
      return JS_EXCEPTION;
    return this->active_->publish(ctx, kind, image, file);
  }
  JSValue CardFlow::Admission::Execution::publish(JSContext *ctx, JsSeatRecord::Kind kind,
                                                const loka::core::resource::Image &image, const loka::file::File &file)
  {
    JSValue value = JS_NewObjectClass(ctx, handleClassId);
    if (JS_IsException(value))
      return value;
    HandleIdentity *identity = static_cast<HandleIdentity *>(js_malloc(ctx, sizeof(HandleIdentity)));
    if (!identity)
    {
      JS_FreeValue(ctx, value);
      return JS_EXCEPTION;
    }
    identity->serial = this->serial_;
    identity->slot = this->occupied_;
    identity->kind = kind;
    JS_SetOpaque(value, identity);
    if (JS_FreezeObject(ctx, value) < 0)
    {
      JS_FreeValue(ctx, value);
      return JS_EXCEPTION;
    }
    Execution::Slot &slot = this->slots_[this->occupied_++];
    slot.kind = kind;
    slot.image = image;
    slot.file = file;
    return value;
  }
  JSValue CardFlow::Admission::project(JSContext *ctx, const loka::core::resource::Image &image)
  {
    return this->publish(ctx, JsSeatRecord::IMAGE, image, loka::file::File());
  }
  JSValue CardFlow::Admission::project(JSContext *ctx, const loka::file::File &file)
  {
    return this->publish(ctx, JsSeatRecord::FILE_RESULT, loka::core::resource::Image::Empty(), file);
  }
  bool CardFlow::Admission::resolve(JSContext *ctx, const JsCardNode &card, JSValueConst value, JsSeatRecord::Kind kind,
                                   loka::core::resource::Image &image, loka::file::File &file)
  {
    if (!card.flowLive())
    {
      JS_ThrowTypeError(ctx, "native handle requires a Live mounted card");
      return false;
    }
    if (!this->active_)
    {
      JS_ThrowTypeError(ctx, "native handle requires an active CardFlow execution");
      return false;
    }
    return this->active_->resolve(ctx, value, kind, image, file);
  }
  bool CardFlow::Admission::Execution::resolve(JSContext *ctx, JSValueConst value, JsSeatRecord::Kind kind,
                                              loka::core::resource::Image &image, loka::file::File &file)
  {
    const HandleIdentity *identity = static_cast<HandleIdentity *>(JS_GetOpaque(value, handleClassId));
    if (!identity
        || identity->serial != this->serial_ || identity->slot >= this->occupied_
        || identity->kind != kind || this->slots_[identity->slot].kind != kind)
    {
      JS_ThrowTypeError(ctx, "native handle is stale, foreign, or has the wrong kind");
      return false;
    }
    const Execution::Slot &slot = this->slots_[identity->slot];
    image = slot.image;
    file = slot.file;
    return true;
  }

  JSClassID JsFlowDescription::classId_ = 0;
  JSClassID JsFlowDescription::skipClassId_ = 0;

  JsFlowDescription *JsFlowDescription::get(JSValueConst value)
  {
    return static_cast<JsFlowDescription *>(JS_GetOpaque(value, classId_));
  }
  bool JsFlowDescription::isSkip(JSValueConst value)
  {
    return JS_IsObject(value) && JS_GetClassID(value) == skipClassId_;
  }
  bool JsFlowDescription::install(JSContext *ctx)
  {
    JSRuntime *rt = JS_GetRuntime(ctx);
    if (!JsCardNode::installCapability(rt))
      return false;
    JSClassDef def;
    memset(&def, 0, sizeof(def));
    def.class_name = "SmirkyFlowDescription";
    def.finalizer = finalize;
    def.gc_mark = mark;
    if (!RegisterJsNativeClass(rt, classId_, def))
      return false;
    memset(&def, 0, sizeof(def));
    def.class_name = "SmirkyFlowSkip";
    if (!RegisterJsNativeClass(rt, skipClassId_, def))
      return false;
    JSValue builder = JS_NewCFunction(ctx, build, "Flow", 0);
    JSValue skip = JS_NewObjectClass(ctx, skipClassId_);
    bool ok = !JS_IsException(builder) && !JS_IsException(skip) && JS_FreezeObject(ctx, skip) >= 0;
    if (ok)
      ok = JS_DefinePropertyValueStr(ctx, builder, "SKIP", JS_DupValue(ctx, skip), 0) >= 0;
    JS_FreeValue(ctx, skip);
    if (ok)
      ok = JS_FreezeObject(ctx, builder) >= 0;
    JSValue global = JS_GetGlobalObject(ctx);
    if (ok)
      ok = JS_SetPropertyStr(ctx, global, "Flow", JS_DupValue(ctx, builder)) >= 0;
    JS_FreeValue(ctx, builder);
    JS_FreeValue(ctx, global);
    return ok;
  }

  /** Owns the immediate subscription; withdrawal is idempotent. BindingToken's
      callback may outlive the active subscription, but never the card record. */
  class CardFlow::Subscription
  {
  public:
    struct Initial
    {
      loka::core::StateBase &state;
      loka::core::StateBase::OnChangeFn callback;
      void *user;
      Initial(loka::core::StateBase &s, loka::core::StateBase::OnChangeFn cb, void *u)
          : state(s),
            callback(cb),
            user(u)
      {
      }
    };
    explicit Subscription(const Initial &i)
        : state_(i.state),
          callback_(i.callback),
          user_(i.user)
    {
      this->state_.bind(this->callback_, this->user_, false);
    }
    ~Subscription()
    {
      this->state_.unbind(this->callback_, this->user_);
    }

  private:
    loka::core::StateBase &state_;
    const loka::core::StateBase::OnChangeFn callback_;
    void *const user_;
    Subscription(const Subscription &);
    Subscription &operator=(const Subscription &);
  };

  CardFlow::CardFlow(const Initial &i)
      : next(0),
        card_(i.card),
        context_(i.context),
        description_(JS_DupValue(i.context, i.description)),
        handle_(JS_DupValue(i.context, i.handle)),
        seat_(i.seat),
        subscription_(0)
  {
  }
  CardFlow::~CardFlow()
  {
    assert(!this->subscription_ && "CardFlow watch must be withdrawn before reclamation");
    JS_FreeValue(this->context_, this->description_);
    JS_FreeValue(this->context_, this->handle_);
  }
  bool CardFlow::matches(JSValueConst handle) const
  {
    return JS_IsStrictEqual(this->context_, this->handle_, handle);
  }
  bool CardFlow::activate(loka::app::scene::BindingToken &token)
  {
    if (!this->seat_)
      return true;
    if (!this->seat_->isMaterialized())
      return false;
    token.watch(*this, this, &CardFlow::fire, false);
    return this->subscription_ != 0;
  }
  void CardFlow::bind(loka::core::StateBase::OnChangeFn callback, void *user, bool immediate)
  {
    (void)immediate;
    assert(!immediate && !this->subscription_);
    loka::core::StateBase *state = 0;
    switch (this->seat_->kind)
    {
    case JsSeatRecord::STRING:
      state = this->seat_->string.state();
      break;
    case JsSeatRecord::INTEGER:
      state = this->seat_->integer.state();
      break;
    case JsSeatRecord::BOOLEAN:
      state = this->seat_->boolean.state();
      break;
    case JsSeatRecord::FILE_RESULT:
      state = this->seat_->file.state();
      break;
    case JsSeatRecord::IMAGE:
      state = this->seat_->image.state();
      break;
    case JsSeatRecord::VIEWPORT:
      state = this->seat_->viewport.state();
      break;
    }
    if (!state)
      return;
    this->subscription_ = loka::core::LokaNew<Subscription>(loka::core::LokaAllocationSite("CardFlow", "Subscription"),
                                                            Subscription::Initial(*state, callback, user));
  }
  void CardFlow::unbind(loka::core::StateBase::OnChangeFn, void *)
  {
    this->withdraw();
  }
  void CardFlow::withdraw()
  {
    loka::core::LokaDelete(this->subscription_, loka::core::LokaAllocationSite("CardFlow", "Subscription"));
    this->subscription_ = 0;
  }
  bool CardFlow::live() const
  {
    return this->card_.flowLive();
  }
  void CardFlow::fire()
  {
    this->run(JS_UNDEFINED, true);
  }

  CardFlow::CallResult CardFlow::invoke(JSValueConst fn, JSValueConst input, JSValue &output)
  {
    if (!this->live())
      return Canceled;
    output = this->card_.props.runtime->rawCall(*this->card_.engine_, fn, 1, &input);
    // This gate must precede captureException: formatting can execute arbitrary JS.
    if (!this->live())
    {
      if (JS_IsException(output))
        JS_FreeValue(this->context_, JS_GetException(this->context_));
      JS_FreeValue(this->context_, output);
      output = JS_UNDEFINED;
      return Canceled;
    }
    if (!JS_IsException(output))
      return Returned;
    loka::core::String error;
    this->card_.props.runtime->captureException(*this->card_.engine_, error);
    output = JS_UNDEFINED;
    // A thrown formatter can leave a secondary exception. Discard it without JS.
    if (JS_HasException(this->context_))
      JS_FreeValue(this->context_, JS_GetException(this->context_));
    if (!this->live())
      return Canceled;
    this->card_.error_.set(error);
    const loka::core::StringBuffer bytes = error.bufferWithEncoding(loka::core::StringEncodingUtf8);
    output = JS_NewStringLen(this->context_, static_cast<const char *>(bytes.data()), bytes.length());
    if (JS_IsException(output))
    {
      JS_FreeValue(this->context_, JS_GetException(this->context_));
      output = JS_UNDEFINED;
    }
    return Threw;
  }

  CardFlow::Admission::Execution::Execution(Admission &door, CardFlow &flow, bool watching)
      : door_(0), flow_(flow),
        serial_(door.active_ ? 0 : CardFlow::issueExecutionSerial(flow)), occupied_(0)
  {
    if (door.active_)
    {
      if (watching && &door.active_->flow_ != &flow)
      {
        std::fputs("CardFlow: nested watch firing dropped (stage 2a)\n", stderr);
        assert(false && "CardFlow: nested watch firing dropped (stage 2a)");
      }
      return;
    }
    if (!this->serial_)
    {
      std::fputs("CardFlow: execution serial exhausted; admission refused\n", stderr);
      CardFlow::refuseExhaustedSerial(flow);
      return;
    }
    this->door_ = &door;
    this->door_->active_ = this;
  }
  uint32_t CardFlow::issueExecutionSerial(CardFlow &flow)
  {
    return flow.card_.props.runtime->executionSerial_.issue();
  }

  void CardFlow::refuseExhaustedSerial(CardFlow &flow)
  {
    loka::core::StateTrackerGuard transaction(flow.card_.tracker());
    flow.card_.error_.set(loka::core::String::Literal("CardFlow execution serial exhausted"));
  }

  CardFlow::Admission::Execution::~Execution()
  {
    if (this->door_)
      this->door_->active_ = 0;
  }

  bool CardFlow::run(JSValueConst input, bool watching)
  {
    if (!this->live())
      return false;
    Admission::Execution execution(this->card_.flowAdmission_, *this, watching);
    if (!execution.accepted())
      return false;
    loka::core::StateTrackerGuard transaction(this->card_.tracker());
    ScriptRuntime::InterruptWindow interrupt(*this->card_.props.runtime, *this->card_.engine_);
    JsFlowDescription *description = JsFlowDescription::get(this->description_);
    const JsFlowDescription::StepRecord *step = description->executionSteps();
    JSValue value =
        watching ? this->card_.seatGet(this->context_, this->seat_->value) : JS_DupValue(this->context_, input);
    if (!watching && this->seat_)
      step = step->next;
    CallResult result = Returned;
    if (JS_IsException(value))
    {
      JS_FreeValue(this->context_, JS_GetException(this->context_));
      value = JS_UNDEFINED;
      this->card_.error_.set(loka::core::String::Literal("Flow input could not be read (out of memory)"));
      result = Threw;
    }
    for (; result == Returned && step; step = step->next)
    {
      JSValue output = JS_UNDEFINED;
      result = this->invoke(step->fn, value, output);
      JS_FreeValue(this->context_, value);
      value = output;
      if (result != Returned)
        break;
      if (watching && step == description->executionSteps() && JsFlowDescription::isSkip(value))
      {
        JS_FreeValue(this->context_, value);
        return true;
      }
    }
    if (result != Canceled && this->live())
    {
      JSValue callback = description->callback(result == Returned);
      if (JS_IsFunction(this->context_, callback))
      {
        JSValue output = JS_UNDEFINED;
        // Terminal exceptions are diagnostic only: never notify a second time.
        this->invoke(callback, value, output);
        JS_FreeValue(this->context_, output);
      }
    }
    JS_FreeValue(this->context_, value);
    return true;
  }
} // namespace smirkycard
