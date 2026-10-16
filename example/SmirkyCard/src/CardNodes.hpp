#ifndef SMIRKYCARD_CARD_NODES_HPP
#define SMIRKYCARD_CARD_NODES_HPP

#include "ScriptRuntime.hpp"
#include "CardRecords.hpp"
#include "CardFlow.hpp"
#include "CardCarry.hpp"
#include "app/core/Window.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"

namespace smirkycard
{
  class JsCardNode;
  class CardScene : public loka::app::scene::Scene
  {
  public:
    explicit CardScene(loka::app::scene::NodeDefinitionBase *root)
        : Scene(root)
#ifdef TEST_BUILD
          ,
          runnerCard_(0)
#endif
    {
    }
    void replaceWith(CardScene *next);
#ifdef TEST_BUILD
    /** Runner calls once per tick after mount; no execution discovery walk. */
    void tickScenario();

  private:
    friend class CardScenario;
    JsCardNode *runnerCard_;
#endif
  };

  class JsCardNode;
  struct JsCardProps : loka::app::scene::NodePropsBase<JsCardProps>
  {
    typedef JsCardProps TypeTag;
    typedef JsCardNode NodeType;
    ScriptRuntime *runtime;
    SmirkyCardId card;
    CardCarry carry;
    JsCardProps(ScriptRuntime *value = 0,
                SmirkyCardId id = SMIRKY_CARD_FIRST,
                const CardCarry &valueCarry = CardCarry())
        : runtime(value),
          card(id),
          carry(valueCarry)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const;
  };

  /** The sole JS card boundary; it owns all duplicated JS values. */
  class JsCardNode : public loka::app::scene::StdCompositionBoundaryNodeBase<JsCardProps>
  {
  public:
    explicit JsCardNode(const JsCardProps &props);
    virtual ~JsCardNode();
    JSValue mintState(JSContext *context, JSValueConst initial);
    JSValue mintTypedState(JSContext *, JsSeatRecord::Kind, JSValueConst);
    bool flowLive() const;
    JSValue loadImage(JSContext *, JSValueConst);
    JSValue seatGet(JSContext *context, JSValueConst seat);
    JSValue seatSet(JSContext *context, JSValueConst seat, JSValueConst value);
    JSValue errorSeatGet(JSContext *context);
    void requestGo(const char *name, size_t length, const CardCarry &carry);
    void requestGo(SmirkyCardId card, const CardCarry &carry);
    void requestReload();
    void requestOpen(const char *name, size_t length, const CardCarry &carry);
    bool setComposeTree(JSContext *context, JSValueConst tree);
    loka::app::scene::NodeDefinitionBase *lowerText(JSContext *context, JSValueConst tree);
    loka::app::scene::NodeDefinitionBase *lowerEditText(JSContext *context, JSValueConst tree);
    loka::app::scene::NodeDefinitionBase *lowerClickable(JSContext *context, JSValueConst tree, bool cell);
    loka::app::scene::NodeDefinitionBase *lowerShow(JSContext *context, JSValueConst tree, int depth);
    loka::app::scene::NodeDefinitionBase *lowerOpenFileDialog(JSContext *context, JSValueConst tree);
    loka::app::scene::NodeDefinitionBase *lowerImageView(JSContext *context, JSValueConst tree);
    loka::app::scene::NodeDefinitionBase *createScrollView();
    void fail(const char *message);
    loka::app::scene::NodeDefinitionBase *lowerChild(JSContext *context, JSValueConst tree, int depth);
    virtual void declareBindings(loka::app::scene::BindingToken &token);
    virtual void composeNode(loka::app::scene::NodeComposition &composition);
    virtual void attachNode(loka::app::scene::NodeComposition &composition);
    virtual void detachNode(loka::app::scene::NodeComposition &composition);

  private:
    friend struct IJsNodeLowering;
    friend class ScriptRuntime;
    friend class JsClickNode;
    friend class CardFlow;
#ifdef TEST_BUILD
    friend class testing::CardFlowAccess;
#endif
    friend class JsFlowDescription;
    static bool installCapability(JSRuntime *);
    JSValue declareFlow(JSContext *, JSValueConst);
    static JSValue runFlow(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
    void withdrawFlows();
    virtual void onLifecycleFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                        loka::app::scene::NodeLifecycleFact next);
#ifdef TEST_BUILD
    friend class CardScenario;
    friend class CardScene;
    CardScenario *scenario_;
    static JSValue testMethod(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
#endif
    void fail(const loka::core::String &message);
    loka::app::scene::NodeDefinitionBase *lower(JSContext *context, JSValueConst tree, int depth);
    JsSeatRecord *findSeat(JSContext *context, JSValueConst value) const;
    JsHandlerRecord *addHandler(JSContext *context, JSValueConst handler);
    void fire(const JsHandlerRecord &handler);
    JSValue callHook(JSValueConst hook);
    void finishCall(JSValue result);
    void declareRefusal(loka::app::scene::NodeComposition &composition);
    JsEngine *engine_;
    JsEngineRef engineRef_;
    enum Phase
    {
      Constructing,
      Live,
      TransitionPending,
      Detaching,
      Revoked
    };
    Phase phase_;
    /** QuickJS owns the shared revocable capability captured by native methods. */
    JSValue capability_;
    static JSValue contextMethod(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
    void revoke();
    JSValue finishSeat(JSContext *, JsSeatRecord *);
    static JSValue typedFactory(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
    static JSValue nativeLoadImage(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
    void reloadWithCarry(const CardCarry &carry);
    bool failed_;
    JSValue instance_;
    loka::core::String failure_;
    loka::core::EmitterState reloadEmitter_;
    CardRecords<JsSeatRecord> seats_;
    CardRecords<JsScrollViewport> scrollViewports_;
    CardRecords<JsHandlerRecord> handlers_;
    CardRecords<CardFlow> flows_;
    CardFlow::Admission flowAdmission_;
    JSValue errorSeat_, tree_, onAttach_, onDetach_;
    loka::app::scene::NodeState<loka::core::String> error_;
  };
  CardScene *CreateCard(SmirkyCardId card, ScriptRuntime &runtime, const CardCarry &carry = CardCarry());
} // namespace smirkycard
#endif
