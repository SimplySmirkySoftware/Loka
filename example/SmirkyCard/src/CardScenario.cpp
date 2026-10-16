#include "CardScenario.hpp"
#ifdef TEST_BUILD
#include "CardNodes.hpp"
#include "CardFlowDescription.hpp"
#include "JsNativeClass.hpp"
#ifdef LOKA_RETRO68
#include "ToolboxFileChoice.hpp"
#include "app/PlatformContext.hpp"
#endif
#include "app/nodes/AttributedText.hpp"
#include <new>
#include <stdint.h>
#include <cfloat>

namespace smirkycard
{
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  namespace
  {
    enum TestMethod
    {
      Run,
      Click,
      Enabled,
      Text,
      Log,
      Random,
      DeliverChosenFile,
      ImageFacts
    };
    std::string utf8(const loka::core::String &value)
    {
      const loka::core::StringBuffer b = value.bufferWithEncoding(loka::core::StringEncodingUtf8);
      return std::string(static_cast<const char *>(b.data()), b.length());
    }
    JSValue jsText(JSContext *ctx, const loka::core::String &value)
    {
      const std::string text = utf8(value);
      return JS_NewStringLen(ctx, text.data(), text.size());
    }
  } // namespace

  CardScenario::CardScenario(JsCardNode &card, JSValueConst context)
      : card_(card),
        runtime_(*card.props.runtime),
        engine_(*card.engine_),
        sink_(*this->runtime_.runner_->sink),
        clock_(*this->runtime_.runner_->clock),
        input_(0),
        context_(JS_DupValue(this->engine_.context(), context)),
        scenario_(this->engine_.takeScenario()),
        description_(JS_UNDEFINED),
        previous_(JS_UNDEFINED),
        chain_(0),
        phase_(JS_IsUndefined(this->scenario_) ? Done : Waiting),
        outcome_(FLOW_RUN_PENDING),
        randomState_(this->runtime_.runner_->seed & 0xffffffffUL),
        terminal_(&this->sink_)
  {
  }

  CardScenario::~CardScenario()
  {
    // No callbacks, audit writes, or cancellation delivery from reclamation.
    delete this->chain_;
    JSContext *ctx = this->engine_.context();
    JS_FreeValue(ctx, this->previous_);
    JS_FreeValue(ctx, this->description_);
    JS_FreeValue(ctx, this->scenario_);
    JS_FreeValue(ctx, this->context_);
  }
  bool CardScenario::installContext(JSContext *ctx, JSValueConst context, JSValue capability, JSCFunctionData *method)
  {
    if (JS_SetPropertyStr(ctx, context, "run", JS_NewCFunctionData(ctx, method, 1, Run, 1, &capability)) < 0)
      return false;
    JSValue test = JS_NewObject(ctx);
    const char *names[] = {"click", "enabled", "text", "log", "random", "deliverChosenFile", "imageFacts"};
    bool ok = !JS_IsException(test);
    for (int i = 0; ok && i < static_cast<int>(sizeof(names) / sizeof(names[0])); ++i)
      ok = JS_SetPropertyStr(
               ctx, test, names[i], JS_NewCFunctionData(ctx, method, Click + i == Random ? 0 : Click + i == DeliverChosenFile ? 2 : 1, Click + i, 1, &capability))
           >= 0;
    ok = ok && JS_FreezeObject(ctx, test) >= 0;
    if (!ok)
    {
      JS_FreeValue(ctx, test);
      return false;
    }
    return JS_SetPropertyStr(ctx, context, "test", test) >= 0;
  }
  void CardScenario::attach(CardScene *scene)
  {
    if (this->card_.phase_ != JsCardNode::Live)
      return;
    this->input_ = scene;
    assert(!scene->runnerCard_ || scene->runnerCard_ == &this->card_);
    scene->runnerCard_ = &this->card_;
  }
  void CardScene::tickScenario()
  {
    if (this->runnerCard_ && this->runnerCard_->scenario_)
      this->runnerCard_->scenario_->tick();
  }
  bool CardScenario::canAdvance() const
  {
    return this->card_.phase_ == JsCardNode::Live && this->outcome_ != FLOW_RUN_CANCELED;
  }
  void CardScenario::cancel()
  {
    if (this->input_)
    {
      static_cast<CardScene *>(this->input_)->runnerCard_ = 0;
      this->input_ = 0;
    }
    if (this->phase_ != Done)
      this->outcome_ = FLOW_RUN_CANCELED;
  }
  void CardScenario::finishDetach()
  {
    if (this->phase_ == Waiting)
      this->phase_ = Done;
    else if (this->phase_ == Idle)
      this->complete(FLOW_RUN_CANCELED);
  }
  bool CardScenario::invoke(JSValueConst fn, int argc, JSValueConst *argv, JSValue &result, loka::core::String &error)
  {
    return this->runtime_.call(this->engine_, fn, argc, argv, result, error);
  }
  StepRunStatus CardScenario::JsStepAdapter::run(const In &in, Out &out, FlowError &error) const
  {
    out = in;
    if (!this->owner_->canAdvance())
      return FLOW_STEP_PENDING;
    JSValue result = JS_UNDEFINED;
    loka::core::String message;
    this->owner_->operationError_ = FlowError();
    const bool ok = this->owner_->invoke(this->fn_, 1, &this->owner_->previous_, result, message);
    if (ok)
    {
      JS_FreeValue(this->owner_->engine_.context(), this->owner_->previous_);
      this->owner_->previous_ = result;
      return FLOW_STEP_SUCCEEDED;
    }
    JS_FreeValue(this->owner_->engine_.context(), result);
    this->message_ = utf8(message);
    error = this->owner_->operationError_;
    if (!error.kind)
    {
      error.kind = FLOW_ERROR_KIND_FLOW;
      error.code = FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED;
    }
    return FLOW_STEP_FAILED;
  }
  StepRunStatus CardScenario::GatedSettle::run(const In &in, Out &out, FlowError &error) const
  {
    out = in;
    return this->owner_->canAdvance() ? Settle().run(in, out, error) : FLOW_STEP_PENDING;
  }
  JSValue CardScenario::accept(JSContext *ctx, JSValueConst value)
  {
    if (this->phase_ != Invoking || this->chain_ || !this->canAdvance())
      return JS_ThrowTypeError(ctx, "c.run is only accepted once synchronously inside this card's scenario invocation");
    JsFlowDescription *description = JsFlowDescription::get(value);
    if (!description || !description->steps() || description->hasWatch())
      return JS_ThrowTypeError(ctx, "c.run requires an unconsumed Flow with at least one step");
    // One JS step per scenario tick. A step's seat writes complete with the
    // rail's entry operation, not with the step: Toolbox routes them when the
    // idle operation that ran the tick completes, so a Show branch revealed by
    // step k exists only on tick k + 1. Null routes synchronously and passed
    // every step on tick 1, which hid the dependency; the due tick makes the
    // audit the same on every rail.
    const JsFlowDescription::StepRecord *s = description->steps();
    long due = 1;
    ScenarioFlowChain<ScenePtr, ScenePtr> flow =
        ScenarioFlow(this->clock_, &this->input_).auditTo(&this->sink_)
        | AtTick(due, RunOnce(JsStepAdapter(*this, s->fn))).named(s->name.c_str());
    flow = flow | Then(GatedSettle(*this));
    for (s = s->next; s; s = s->next)
    {
      flow = flow | AtTick(++due, RunOnce(JsStepAdapter(*this, s->fn))).named(s->name.c_str());
      flow = flow | Then(GatedSettle(*this));
    }
    this->chain_ = new (std::nothrow) Chain(flow.flow());
    if (!this->chain_)
      return JS_ThrowOutOfMemory(ctx);
    this->description_ = JS_DupValue(ctx, value);
    description->consume();
    return JS_UNDEFINED;
  }
  void CardScenario::tick()
  {
    if (this->phase_ != Waiting && this->phase_ != Idle)
      return;
    if (!this->canAdvance())
    {
      if (this->outcome_ == FLOW_RUN_CANCELED)
        this->complete(FLOW_RUN_CANCELED);
      return;
    }
    if (this->outcome_ != FLOW_RUN_PENDING)
    {
      this->complete(this->outcome_);
      return;
    }
    if (this->phase_ == Waiting)
    {
      this->phase_ = Invoking;
      JSValue result = JS_UNDEFINED;
      loka::core::String error;
      const bool ok = this->invoke(this->scenario_, 1, &this->context_, result, error);
      JS_FreeValue(this->engine_.context(), result);
      this->phase_ = Idle;
      if (!ok || !this->chain_)
      {
        const std::string message = ok ? "scenario did not call c.run" : utf8(error);
        this->sink_.recordLog(message);
        this->outcome_ = this->outcome_ == FLOW_RUN_CANCELED ? FLOW_RUN_CANCELED : FLOW_RUN_FAILED;
        if (this->canAdvance() || this->outcome_ == FLOW_RUN_CANCELED)
          this->complete(this->outcome_);
        return;
      }
      if (!this->canAdvance())
      {
        if (this->outcome_ == FLOW_RUN_CANCELED)
          this->complete(FLOW_RUN_CANCELED);
        return;
      }
    }
    this->phase_ = Driving;
    const FlowRunResult result = this->chain_->runResult();
    this->phase_ = Idle;
    if (this->outcome_ == FLOW_RUN_CANCELED)
      this->complete(FLOW_RUN_CANCELED);
    else if (result != FLOW_RUN_PENDING)
    {
      this->outcome_ = result;
      if (this->canAdvance())
        this->complete(result);
    }
  }
  void CardScenario::complete(FlowRunResult result)
  {
    if (this->phase_ == Done)
      return;
    if (this->outcome_ == FLOW_RUN_CANCELED)
      result = FLOW_RUN_CANCELED;
    this->phase_ = Completing;
    if (result != FLOW_RUN_CANCELED && this->canAdvance() && !JS_IsUndefined(this->description_))
    {
      JsFlowDescription *desc = JsFlowDescription::get(this->description_);
      JSValue fn = desc->callback(result == FLOW_RUN_SUCCEEDED);
      if (JS_IsFunction(this->engine_.context(), fn))
      {
        JSValue returned = JS_UNDEFINED;
        loka::core::String error;
        if (!this->invoke(fn, 0, 0, returned, error))
        {
          this->sink_.recordLog(utf8(error));
          result = FLOW_RUN_FAILED;
        }
        JS_FreeValue(this->engine_.context(), returned);
      }
    }
    if (this->outcome_ == FLOW_RUN_CANCELED)
      result = FLOW_RUN_CANCELED;
    this->outcome_ = result;
    this->phase_ = Done;
    this->terminal_.emit(result == FLOW_RUN_CANCELED    ? SCENARIO_AUDIT_CANCELED
                         : result == FLOW_RUN_SUCCEEDED ? SCENARIO_AUDIT_SUCCEEDED
                                                        : SCENARIO_AUDIT_FAILED);
  }
  double CardScenario::random()
  {
    this->randomState_ = (this->randomState_ * 1664525UL + 1013904223UL) & 0xffffffffUL;
    // Normalize the integer into IEEE binary64's significand. No floating-point
    // arithmetic or integer-to-double conversion (Classic software-float safe).
    typedef char
        RequiresBinary64[(sizeof(double) == sizeof(uint64_t) && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024) ? 1 : -1];
    (void)sizeof(RequiresBinary64);
    uint64_t bits = 0;
    if (this->randomState_)
    {
      unsigned long normalized = this->randomState_;
      unsigned int high = 31;
      while (!(normalized & 0x80000000UL))
      {
        normalized <<= 1;
        --high;
      }
      bits = (static_cast<uint64_t>(991 + high) << 52) | (static_cast<uint64_t>(normalized & 0x7fffffffUL) << 21);
    }
    double result;
    memcpy(&result, &bits, sizeof(result));
    return result;
  }
  JsSeatRecord *CardScenario::ownSeat(JSContext *ctx, JSValueConst argument)
  {
    if (!this->canAdvance() || !this->input_ || this->card_.flowAdmission_.hasExecution())
    {
      JS_ThrowTypeError(ctx, "seat operation requires a Live card outside CardFlow");
      return 0;
    }
    JSValue value = JS_DupValue(ctx, argument);
    if (JS_IsString(argument))
    {
      JSAtom name = JS_ValueToAtom(ctx, argument);
      if (name == JS_ATOM_NULL)
      {
        JS_FreeValue(ctx, value);
        return 0;
      }
      JSPropertyDescriptor property;
      const int found = JS_GetOwnProperty(ctx, &property, this->card_.instance_, name);
      JS_FreeAtom(ctx, name);
      JS_FreeValue(ctx, value);
      if (found < 0)
        return 0;
      value = found ? property.value : JS_UNDEFINED;
      if (found)
      {
        JS_FreeValue(ctx, property.getter);
        JS_FreeValue(ctx, property.setter);
      }
    }
    // A Proxy own-property trap may navigate while resolving the seat name.
    if (!this->canAdvance() || !this->input_ || this->card_.flowAdmission_.hasExecution())
    {
      JS_FreeValue(ctx, value);
      JS_ThrowTypeError(ctx, "seat operation card is no longer available");
      return 0;
    }
    JsSeatRecord *seat = this->card_.findSeat(ctx, value);
    JS_FreeValue(ctx, value);
    if (!seat || !seat->isMaterialized())
    {
      JS_ThrowTypeError(ctx, "seat operation requires an own materialized seat");
      return 0;
    }
    return seat;
  }
  JSValue CardScenario::operation(JSContext *ctx, int argc, JSValueConst *argv, int op)
  {
    if (op == Run)
      return argc == 1 ? this->accept(ctx, argv[0]) : JS_ThrowTypeError(ctx, "c.run requires one Flow");
    if (op == Random)
      return argc == 0 ? JS_NewFloat64(ctx, this->random()) : JS_ThrowTypeError(ctx, "random takes no arguments");
    if (op == ImageFacts)
    {
      if (argc != 1)
        return JS_ThrowTypeError(ctx, "imageFacts requires one IMAGE seat");
      JsSeatRecord *seat = this->ownSeat(ctx, argv[0]);
      if (!seat)
        return JS_EXCEPTION;
      if (seat->kind != JsSeatRecord::IMAGE)
        return JS_ThrowTypeError(ctx, "imageFacts requires an own IMAGE seat");
      const loka::core::resource::Image &image = seat->image.state()->getRef();
      const bool empty = !image.isValid();
      JSValue facts = JS_NewObject(ctx);
      if (JS_IsException(facts))
        return facts;
      if (JS_DefinePropertyValueStr(ctx, facts, "empty", JS_NewBool(ctx, empty), JS_PROP_C_W_E) < 0
          || JS_DefinePropertyValueStr(ctx, facts, "width", JS_NewInt32(ctx, empty ? 0 : image.width()), JS_PROP_C_W_E) < 0
          || JS_DefinePropertyValueStr(ctx, facts, "height", JS_NewInt32(ctx, empty ? 0 : image.height()), JS_PROP_C_W_E) < 0
          || JS_FreezeObject(ctx, facts) < 0)
      {
        JS_FreeValue(ctx, facts);
        return JS_EXCEPTION;
      }
      return facts;
    }
    if (op == DeliverChosenFile)
    {
      if (argc != 2)
        return JS_ThrowTypeError(ctx, "deliverChosenFile requires a seat and filename or null");
      JsSeatRecord *seat = this->ownSeat(ctx, argv[0]);
      if (!seat)
        return JS_EXCEPTION;
      if (seat->kind != JsSeatRecord::FILE_RESULT)
        return JS_ThrowTypeError(ctx, "deliverChosenFile requires an own FILE seat");
      loka::app::FileChooserResult result = loka::app::FileChooserResult::Canceled();
      if (!JS_IsNull(argv[1]))
      {
        if (!JS_IsString(argv[1]))
          return JS_ThrowTypeError(ctx, "deliverChosenFile requires a flat filename or null");
        size_t length = 0;
        const char *bytes = JS_ToCStringLen(ctx, &length, argv[1]);
        if (!bytes)
          return JS_EXCEPTION;
        const std::string name(bytes, length);
        JS_FreeCString(ctx, bytes);
        if (name.empty() || name == "." || name == ".."
            || name.find_first_of("/\\:") != std::string::npos || name.find('\0') != std::string::npos)
          return JS_ThrowTypeError(ctx, "deliverChosenFile requires a flat filename");
        loka::file::File chosen(loka::core::String::Utf8(name.data(), name.size()));
#ifdef LOKA_RETRO68
        PlatformContext *platform = this->runtime_.nativeContext();
        loka::platform::file::FileHandle handle;
        if (!platform || !platform->openFile(loka::file::File::Application() << chosen, handle) || !handle.hasSpec)
          return JS_ThrowTypeError(ctx, "deliverChosenFile could not resolve file");
        // Match SimpleViewerScenarioDriver's stand-in for the dialog rail.
        if (!ToolboxCaptureChosenFile(handle.spec, chosen))
          return JS_ThrowTypeError(ctx, "deliverChosenFile could not resolve file");
#endif
        result = loka::app::FileChooserResult::File(chosen);
      }
      seat->file.set(result, true);
      return JS_UNDEFINED;
    }
    if (argc != 1 || !JS_IsString(argv[0]))
      return JS_ThrowTypeError(ctx, "test operation requires a string");
    size_t length = 0;
    const char *bytes = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!bytes)
      return JS_EXCEPTION;
    const std::string text(bytes, length);
    JS_FreeCString(ctx, bytes);
    if (op == Log)
      return this->sink_.recordLog(text) ? JS_UNDEFINED : JS_ThrowTypeError(ctx, "scenario audit log refused");
    if (!this->canAdvance() || !this->input_)
      return JS_ThrowTypeError(ctx, "test scene operation requires a Live mounted card");
    loka::app::scene::Node *node = 0;
    FlowError error;
    if (LookupNodeById(this->input_, text, node, error) != FLOW_STEP_SUCCEEDED)
    {
      this->operationError_ = error;
      return JS_ThrowTypeError(ctx, "test id '%s' is missing or duplicated", text.c_str());
    }
    if (op == Click)
    {
      ScenePtr out = 0;
      StepRunStatus status;
      if (node->asButtonNode())
        status = ClickButton(text.c_str(), CLICK_DISABLED_FAILS).run(this->input_, out, error);
      else if (node->asCellNode())
        status = ClickCell(text.c_str()).run(this->input_, out, error);
      else
        return JS_ThrowTypeError(ctx, "click requires a Button or Cell");
      if (status == FLOW_STEP_SUCCEEDED)
        return JS_UNDEFINED;
      this->operationError_ = error;
      if (error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED)
        return JS_ThrowTypeError(ctx, "button '%s' is disabled", text.c_str());
      return JS_ThrowTypeError(ctx, "click '%s' failed (code %d)", text.c_str(), error.code);
    }
    if (op == Enabled)
    {
      if (node->asButtonNode())
        return JS_NewBool(ctx, SceneClickTraits<loka::app::ButtonNode>::enabled(node->asButtonNode()));
      if (node->asCellNode())
        return JS_TRUE;
      return JS_ThrowTypeError(ctx, "enabled requires a Button or Cell");
    }
    if (node->asTextNode())
      return jsText(ctx, node->asTextNode()->props.text_->get());
    if (node->asButtonNode())
      return jsText(ctx, node->asButtonNode()->props.text_->get());
    if (node->asCellNode())
      return jsText(ctx, node->asCellNode()->props.text_->get());
    if (node->asEditTextNode())
      return jsText(ctx, node->asEditTextNode()->props.text_.state()->get());
    if (node->asAttributedTextNode())
    {
      const loka::app::AttributedString &value = node->asAttributedTextNode()->props.text_->get();
      loka::core::String flat;
      for (size_t i = 0; i < value.segmentCount(); ++i)
        flat = flat + value.segment(i).text;
      return jsText(ctx, flat);
    }
    return JS_ThrowTypeError(ctx, "text requires Text, Markup, Button, Cell or EditText");
  }
} // namespace smirkycard
#endif
