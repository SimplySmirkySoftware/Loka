#include "app/core/DialogResultTransport.hpp"
namespace parked_dialog
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class Root;
  typedef BoundaryPropsFor<Root> Props;
  class Root : public StdCompositionBoundaryNodeBase<Props>
  {
  public:
    MutableState<bool> shown;
    EmitterState completed, clicked;
    unsigned clicks;
    explicit Root(const Props &props)
        : StdCompositionBoundaryNodeBase<Props>(props),
          shown(true),
          clicks(0)
    {
      this->completed.bind(&hide, this, false);
      this->clicked.bind(&click, this, false);
    }
    ~Root()
    {
      this->completed.unbind(&hide, this);
      this->clicked.unbind(&click, this);
    }
    static void hide(void *data)
    {
      Root &root = *static_cast<Root *>(data);
      root.shown.set(false);
    }
    static void click(void *data)
    {
      ++static_cast<Root *>(data)->clicks;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Show(this->shown) << Button("park").onClick(&this->clicked));
    }
  };
  class DialogWindow : public Window
  {
    ToolboxWindow &native_;

  public:
    DialogResultTransport results;
    DialogWindow(PlatformContext *platform, ToolboxScenePlatformController &controller)
        : Window(platform, WindowProps().scene(new Scene(Boundary<Root>(Props())))),
          native_(*controller.window_)
    {
      this->results.open(*this);
      this->scene()->mount(&controller);
      this->native_.scenePlatformController_ = &controller;
    }
    ~DialogWindow()
    {
      this->results.close();
      this->unmountSceneForTeardown(*this->scene());
      this->native_.scenePlatformController_ = 0;
    }
    virtual DialogResultDelivery *dialogResultDelivery()
    {
      return &this->results;
    }
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
    virtual ToolboxWindow *asToolboxWindow()
    {
      return &this->native_;
    }
  };
  /** Native modal neighbor seals a real result; App delivers it at admission. */
  class Presenter : public ToolboxDialogPresenter
  {
    DialogResultTransport::Registration *registration_;

  public:
    ToolboxDialogEnrollment enrollment;
    Presenter(Node *node, DialogResultTransport &transport, EmitterState &completed)
        : ToolboxDialogPresenter(node),
          registration_(transport.reserve(OpenFileDialogProps().onResult(&completed))),
          enrollment(this)
    {
      LOKA_VERIFY(this->registration_ != 0);
    }
    ~Presenter()
    {
      delete this->registration_;
    }
    virtual void present(ToolboxApp &)
    {
      LOKA_VERIFY(this->owner()->lifecycleFact() == NODE_FACT_ATTACHED);
      DialogResultTransport::ReturnPort port(this->registration_);
      LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) != 0);
      LOKA_VERIFY(this->owner()->lifecycleFact() == NODE_FACT_ATTACHED);
    }
  };
  void pin()
  {
    ToolboxWindow native;
    ToolboxScenePlatformController controller(&native);
    loka::testing::StandaloneMountTestPlatformContext platform;
    DialogWindow window(&platform, controller);
    ToolboxApp app(window);
    app.flush();
    Root *root = static_cast<Root *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
    Node *button = findField(root, BUTTON_INPUT);
    LOKA_VERIFY(button != 0);
    ToolboxButtonContext *context = new ToolboxButtonContext();
    button->setContext(context);
    const Rect rect = {0, 0, 40, 100};
    context->rect_ = rect;
    context->emitter_ = &root->clicked;
    ToolboxHitLedger::ButtonHit hit;
    hit.rect = rect;
    hit.context = context;
    hit.emitter = &root->clicked;
    hit.enabled = 0;
    hit.boundary = root;
    controller.installHit(hit);
    MutableState<String> text(String("focused"));
    EditTextNode field((EditTextProps()));
    NotifySubtreeNodeAttached(&field);
    ToolboxEditTextContext *fieldContext = new ToolboxEditTextContext(&field);
    field.setContext(fieldContext);
    const Rect fieldRect = {100, 0, 120, 100};
    LOKA_VERIFY(controller.ensureEditTextControl(fieldContext, fieldRect, &text, NATIVE_HINT_DEFAULT).te);
    LOKA_VERIFY(controller.applyNativeFocus(*fieldContext));
    Presenter dialog(button, window.results, root->completed);
    controller.pendingDialogs().enroll(dialog.enrollment);
    {
      Operation turn;
      app.present(ACTIVATION_FOREGROUND, turn);
    }
    LOKA_VERIFY(!root->shown.get());
    LOKA_VERIFY(button->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    // The retained context is still the row installed before present.
    LOKA_VERIFY(button->getContext() == context);
    LOKA_VERIFY(controller.buttonHits().size() == 1);
    LOKA_VERIFY(controller.buttonHits()[0].context == context);
    const Point point = {5, 5};
    controller.publishFixturePressOrder();
    ToolboxInputDoor::mouseDown(controller, point);
    LOKA_VERIFY(root->clicks == 0);
    NodeContext *focused = 0;
    LOKA_VERIFY(controller.readNativeFocus(focused));
    LOKA_VERIFY(focused == fieldContext);
    controller.retireEditTextControl(fieldContext, NATIVE_HINT_EAGER_RELEASE);
  }
} // namespace parked_dialog
