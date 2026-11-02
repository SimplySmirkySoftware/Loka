#include "support/LayoutY.hpp"
#include "support/MenuStructure.hpp"
#include "AttrDslTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>

#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/Menu.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "app/layout/GridLayout.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/layout/ZStackLayout.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "core/State.hpp"

namespace
{
  class AttrDslCustomExternalNode : public loka::app::scene::Node
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomExternalNode>();
    }
  };

  class AttrDslCustomExternalHandler : public loka::app::scene::IPlatformNodeHandler
  {
  public:
    AttrDslCustomExternalHandler()
        : ensureCalls_(0)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomExternalNode>();
    }

    virtual loka::app::scene::NodeContext *ensureContext(loka::app::scene::Node *node,
                                                         loka::app::scene::IPlatformController *controller,
                                                         const loka::app::scene::LayoutState &state)
    {
      (void)controller;
      ++ensureCalls_;
      assert(node != 0);
      assert(node->nodeTypeKey() == this->nodeTypeKey());
      lastState_ = state;
      if (!node->getContext())
      {
        node->setContext(new loka::app::scene::NodeContext(node));
      }
      return node->getContext();
    }

    int ensureCalls_;
    loka::app::scene::LayoutState lastState_;
  };

  class AttrDslInvalidExternalHandler : public loka::app::scene::IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return 0;
    }

    virtual loka::app::scene::NodeContext *ensureContext(loka::app::scene::Node *node,
                                                         loka::app::scene::IPlatformController *controller,
                                                         const loka::app::scene::LayoutState &state)
    {
      (void)node;
      (void)controller;
      (void)state;
      return 0;
    }
  };

  class AttrDslDummyRegistrationPlatformController : public loka::app::scene::IPlatformController
  {
  public:
    AttrDslDummyRegistrationPlatformController()
        : lastRoot_(0),
          prepareProjectedCalls_(0),
          prepareProjectedResult_(false)
    {
    }

    virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild)
    {
      (void)flags;
      (void)fullRebuild;
      this->lastRoot_ = rootNode;
    }

    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}

    virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler)
    {
      return this->registry_.registerHandler(handler);
    }

    virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state)
    {
      ++prepareProjectedCalls_;
      lastPreparedNode_ = node;
      lastPreparedState_ = state;
      return prepareProjectedResult_;
    }

    loka::app::scene::IPlatformNodeHandler *findHandler(const loka::app::scene::Node *node) const
    {
      return this->registry_.find(node);
    }

    loka::app::scene::PlatformNodeHandlerRegistry registry_;
    loka::app::scene::Node *lastRoot_;
    int prepareProjectedCalls_;
    bool prepareProjectedResult_;
    loka::app::scene::Node *lastPreparedNode_;
    loka::app::scene::LayoutState lastPreparedState_;
  };

  class AttrDslSecondExternalHandler : public loka::app::scene::IPlatformNodeHandler
  {
  public:
    AttrDslSecondExternalHandler()
        : ensureCalls_(0)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomExternalNode>();
    }

    virtual loka::app::scene::NodeContext *ensureContext(loka::app::scene::Node *node,
                                                         loka::app::scene::IPlatformController *controller,
                                                         const loka::app::scene::LayoutState &state)
    {
      (void)controller;
      ++ensureCalls_;
      lastState_ = state;
      if (!node->getContext())
      {
        node->setContext(new loka::app::scene::NodeContext(node));
      }
      return node->getContext();
    }

    int ensureCalls_;
    loka::app::scene::LayoutState lastState_;
  };

  class AttrDslProjectedExternalNode : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
  {
  public:
    AttrDslProjectedExternalNode()
        : projectedCalls_(0)
    {
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      ++projectedCalls_;
      const bool prepared = loka::app::scene::PrepareProjectedLayout(controller, this, state);
      return static_cast<short>(prepared ? state.y + 9 : state.y);
    }

    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }

    int projectedCalls_;
  };

  class AttrDslBoundaryAwareProjectedNode : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
  {
  public:
    AttrDslBoundaryAwareProjectedNode()
        : projectedCalls_(0)
    {
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      ++projectedCalls_;
      return static_cast<short>(loka::app::scene::PrepareProjectedLayout(controller, this, state) ? state.y : -1);
    }

    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }

    int projectedCalls_;
  };

  class AttrDslConcreteBoundaryNode : public loka::app::scene::BoundaryNode
  {
  public:
    virtual void composeWithContext(loka::app::scene::ComponentContext &context, loka::app::scene::ComposeEvent event)
    {
      (void)context;
      (void)event;
    }
  };

  class AttrDslBoundaryAwareController : public loka::app::scene::IPlatformController
  {
  public:
    AttrDslBoundaryAwareController()
        : activeBoundary_(0),
          preparedBoundary_(0),
          preparedNode_(0),
          prepareCalls_(0)
    {
    }

    virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild)
    {
      (void)rootNode;
      (void)flags;
      (void)fullRebuild;
    }

    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}

    virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state)
    {
      ++prepareCalls_;
      preparedNode_ = node;
      preparedState_ = state;
      preparedBoundary_ = activeBoundary_;
      return true;
    }

    void setActiveBoundary(loka::app::scene::BoundaryNode *boundary)
    {
      activeBoundary_ = boundary;
    }

    loka::app::scene::BoundaryNode *activeBoundary_;
    loka::app::scene::BoundaryNode *preparedBoundary_;
    loka::app::scene::Node *preparedNode_;
    loka::app::scene::LayoutState preparedState_;
    int prepareCalls_;
  };

  short simulateProjectedLayoutWithActiveBoundary(loka::app::scene::Node *node,
                                                  AttrDslBoundaryAwareController *controller,
                                                  loka::app::scene::BoundaryNode *activeBoundary,
                                                  const loka::app::scene::LayoutState &state)
  {
    assert(node != 0);
    assert(controller != 0);
    loka::app::scene::IProjectedLayoutNode *projected = node->asProjectedLayoutNode();
    assert(projected != 0);
    controller->setActiveBoundary(activeBoundary);
    loka::app::scene::LayoutState projectedState = state;
    return projected->layoutProjected(controller, projectedState);
  }

  struct AttrDslLayoutCall
  {
    loka::app::scene::Node *node;
    loka::app::scene::LayoutState state;
    int resultY;
  };

  struct AttrDslLayoutCallRecorder
  {
    AttrDslLayoutCallRecorder()
        : count(0)
    {
    }

    AttrDslLayoutCall calls[8];
    int count;
  };

  int recordLayoutChild(void *context, loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
  {
    AttrDslLayoutCallRecorder *recorder = static_cast<AttrDslLayoutCallRecorder *>(context);
    assert(recorder != 0);
    assert(recorder->count < 8);
    AttrDslLayoutCall &call = recorder->calls[recorder->count++];
    call.node = child;
    call.state = state;
    call.resultY = state.y + state.height;
    return call.resultY;
  }

  class AttrDslCustomLayoutLeafNode : public loka::app::scene::Node
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomLayoutLeafNode>();
    }
  };

  class AttrDslOffsetLayoutLeafNode : public AttrDslCustomLayoutLeafNode
  {
  public:
    explicit AttrDslOffsetLayoutLeafNode(int offset)
        : offset_(offset)
    {
    }

    int offset_;
  };

  int recordLayoutChildWithTaggedOffset(void *context,
                                        loka::app::scene::Node *child,
                                        const loka::app::scene::LayoutState &state)
  {
    AttrDslLayoutCallRecorder *recorder = static_cast<AttrDslLayoutCallRecorder *>(context);
    assert(recorder != 0);
    assert(recorder->count < 8);
    AttrDslLayoutCall &call = recorder->calls[recorder->count++];
    call.node = child;
    call.state = state;
    AttrDslOffsetLayoutLeafNode *offsetNode = static_cast<AttrDslOffsetLayoutLeafNode *>(child);
    call.resultY = state.y + state.height + offsetNode->offset_;
    return call.resultY;
  }

  class AttrDslCustomLayoutNode : public loka::app::scene::NestableNode
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomLayoutNode>();
    }
  };

  class AttrDslDummyLayoutTraversal : public loka::app::scene::IPlatformLayoutTraversal
  {
  public:
    AttrDslDummyLayoutTraversal()
        : calls_(0),
          nextResult_(0),
          lastChild_(0),
          layoutResultY_(0)
    {
    }

    virtual int layoutChild(loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
    {
      ++calls_;
      lastChild_ = child;
      lastState_ = state;
      layoutResultY_ = static_cast<short>(nextResult_);
      return nextResult_;
    }

    virtual void setLayoutResultY(short y)
    {
      layoutResultY_ = y;
    }

    virtual short layoutResultY() const
    {
      return layoutResultY_;
    }

    int calls_;
    int nextResult_;
    loka::app::scene::Node *lastChild_;
    loka::app::scene::LayoutState lastState_;
    short layoutResultY_;
  };

  class AttrDslCustomLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    AttrDslCustomLayoutHandler()
        : layoutCalls_(0),
          fallbackResult_(0)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomLayoutNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      ++layoutCalls_;
      lastState_ = state;
      assert(node != 0);
      assert(node->nodeTypeKey() == this->nodeTypeKey());
      assert(traversal != 0);

      loka::app::scene::INestable *nestable = node->asNestable();
      if (!nestable || !nestable->childrenHead())
      {
        return fallbackResult_;
      }

      loka::app::scene::LayoutState childState = state;
      childState.x += 3;
      childState.y += 5;
      childState.width -= 6;
      if (childState.width < 0)
      {
        childState.width = 0;
      }
      return traversal->layoutChild(nestable->childrenHead(), childState);
    }

    int layoutCalls_;
    int fallbackResult_;
    loka::app::scene::LayoutState lastState_;
  };

  class AttrDslInvalidLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return 0;
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      (void)node;
      (void)state;
      (void)traversal;
      return 0;
    }
  };

  class AttrDslTraversalResultYLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomLayoutNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      assert(node != 0);
      assert(traversal != 0);

      loka::app::scene::INestable *nestable = node->asNestable();
      assert(nestable != 0);
    LOKA_VERIFY(nestable->childrenHead() != 0);

      loka::app::scene::LayoutState childState = state;
      childState.y += 4;
      (void)traversal->layoutChild(nestable->childrenHead(), childState);

      const short childResultY = traversal->layoutResultY();
      traversal->setLayoutResultY(static_cast<short>(childResultY + 6));
      return traversal->layoutResultY();
    }
  };

  class AttrDslSecondLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    AttrDslSecondLayoutHandler()
        : calls_(0),
          result_(0)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<AttrDslCustomLayoutNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      (void)node;
      (void)state;
      (void)traversal;
      ++calls_;
      return result_;
    }

    int calls_;
    int result_;
  };
} // namespace

void testLokaAttrDslV1Core()
{
  printf("\n==== [testLokaAttrDslV1Core] start ====\n");

  // --- Text style storage and ImageView attr storage ---
  {
    loka::core::MutableState<loka::app::TextStyle> dynamicTextStyle((loka::app::FontSize<24>()));
    loka::app::TextDefinitionWithAttr text =
        (loka::app::Text("Hello") + loka::app::Bold) + &dynamicTextStyle;
    assert(text.props.textStyleState_ == &dynamicTextStyle);
    loka::app::TextWeight weight = loka::app::TEXT_WEIGHT_NORMAL;
    const bool hasWeight = text.props.textStyle_.queryWeight(weight);
    LOKA_VERIFY(hasWeight);
    LOKA_VERIFY(weight == loka::app::TEXT_WEIGHT_BOLD);
    assert(!text.props.blockStyle_.hasWrap_);
    assert(!text.props.blockStyle_.hasTruncation_);

    loka::app::ImageViewDefinitionWithAttr image =
        loka::app::ImageView().attr(loka::app::ImageViewAttr()
                                        .sizePolicy(loka::app::IMAGE_VIEW_SIZE_FILL_PARENT)
                                        .fit(loka::app::IMAGE_FIT_CONTAIN));
    assert(image.props.hasAttr_);
    assert(image.props.attr_.hasFitValue_);
    assert(image.props.attr_.fitValue_ == loka::app::IMAGE_FIT_CONTAIN);
    assert(image.props.attr_.hasSizePolicyValue_);
    assert(image.props.attr_.sizePolicyValue_ == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT);
  }

  // --- Text block style storage ---
  {
    loka::app::TextDefinitionWithAttr text = loka::app::Text("Hello")
                                                + loka::app::BlockStyle()
                                                      .wrap(loka::app::TEXT_WRAP_WORD)
                                                      .truncation(loka::app::TEXT_TRUNCATION_ELLIPSIS);
    assert(text.props.blockStyle_.hasWrap_);
    assert(text.props.blockStyle_.wrap_ == loka::app::TEXT_WRAP_WORD);
    assert(text.props.blockStyle_.hasTruncation_);
    assert(text.props.blockStyle_.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS);

    loka::app::TextDefinitionWithAttr charWrap =
        loka::app::Text("Path") + loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_CHAR);
    assert(charWrap.props.blockStyle_.hasWrap_);
    assert(charWrap.props.blockStyle_.wrap_ == loka::app::TEXT_WRAP_CHAR);
  }

  // --- Row/Column alignment props storage ---
  {
    loka::app::VStack column = loka::app::VStack().alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_CENTER);
    LOKA_VERIFY(column.props.axis_ == loka::app::STACK_AXIS_COLUMN);
    LOKA_VERIFY(column.props.hasHorizontalAlignment_);
    LOKA_VERIFY(column.props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_CENTER);

    loka::app::HStack row = loka::app::HStack().alignVertical(loka::app::VERTICAL_ALIGNMENT_BOTTOM);
    LOKA_VERIFY(row.props.axis_ == loka::app::STACK_AXIS_ROW);
    LOKA_VERIFY(row.props.hasVerticalAlignment_);
    LOKA_VERIFY(row.props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_BOTTOM);

    loka::app::Stack stack = loka::app::Stack(loka::app::STACK_AXIS_ROW)
                                  .alignVertical(loka::app::VERTICAL_ALIGNMENT_CENTER)
                                  .alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_TRAILING);
    LOKA_VERIFY(stack.props.axis_ == loka::app::STACK_AXIS_ROW);
    LOKA_VERIFY(stack.props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_CENTER);
    LOKA_VERIFY(stack.props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_TRAILING);
  }

  // Live layout props compare their source identity, not its current value.
  {
    loka::core::MutableState<loka::app::StackAxis> firstAxis(loka::app::STACK_AXIS_COLUMN);
    loka::core::MutableState<loka::app::StackAxis> secondAxis(loka::app::STACK_AXIS_COLUMN);
    loka::app::Stack first(&firstAxis);
    loka::app::Stack same(&firstAxis);
    loka::app::Stack different(&secondAxis);
    const bool sameSource = !(first.props < same.props) && !(same.props < first.props);
    const bool differentSource = !(first.props < different.props) && !(different.props < first.props);
    const loka::app::StackAxis liveAxis = first.props.effectiveAxis();
    LOKA_VERIFY(sameSource);
    LOKA_VERIFY(!differentSource);
    LOKA_VERIFY(liveAxis == loka::app::STACK_AXIS_COLUMN);
    loka::app::Stack absent(static_cast<loka::core::State<loka::app::StackAxis> *>(0));
    const loka::app::StackAxis defaultAxis = absent.props.effectiveAxis();
    LOKA_VERIFY(defaultAxis == loka::app::STACK_AXIS_ROW);

    loka::core::MutableState<int> firstWidth(200);
    loka::core::MutableState<int> secondWidth(200);
    loka::app::Box box = loka::app::Box().width(&firstWidth);
    loka::app::Box copied(box);
    loka::app::Box other = loka::app::Box().width(&secondWidth);
    const bool sameWidthSource = !(box.props < copied.props) && !(copied.props < box.props);
    const bool differentWidthSource = !(box.props < other.props) && !(other.props < box.props);
    const int liveWidth = copied.props.effectiveWidth();
    LOKA_VERIFY(sameWidthSource);
    LOKA_VERIFY(!differentWidthSource);
    LOKA_VERIFY(liveWidth == 200);
    copied.size(120, 80);
    const int constantWidth = copied.props.effectiveWidth();
    LOKA_VERIFY(copied.props.widthState_ == 0);
    LOKA_VERIFY(constantWidth == 120);
  }

  // --- Column remaining-height helper ---
  {
    assert(loka::app::layout::remainingChildHeightForColumn(400, 20, 20) == 400);
    assert(loka::app::layout::remainingChildHeightForColumn(400, 20, 120) == 300);
    assert(loka::app::layout::remainingChildHeightForColumn(400, 20, 999) == 0);
    assert(loka::app::layout::remainingChildHeightForColumn(0, 20, 50) == 0);
  }

  // --- Style copy safety: copy should stay independent ---
  {
    loka::app::TextStyle a;
    loka::app::TextStyle b = a;
    a.weight(loka::app::TEXT_WEIGHT_BOLD);
    (void)b;
    assert(!b.size_.isSet());
    loka::app::TextWeight weight = loka::app::TEXT_WEIGHT_NORMAL;
    const bool hasWeight = a.queryWeight(weight);
    LOKA_VERIFY(hasWeight);
    LOKA_VERIFY(weight == loka::app::TEXT_WEIGHT_BOLD);
  }

  // --- MenuItem attr participates in structure equality ---
  {
    loka::core::MutableState<bool> disabledState(true);
    loka::app::MenuItemDefinitionWithAttr left =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(&disabledState));

    loka::app::MenuItemDefinitionWithAttr right =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(&disabledState));
    assert(loka::app::testing::EqualsStructure(left, right));

    loka::app::MenuItemDefinitionWithAttr changed =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(false));
    assert(!loka::app::testing::EqualsStructure(left, changed));

    loka::app::MenuItemDefinitionWithAttr checkedChanged =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(&disabledState).checked(true));
    assert(!loka::app::testing::EqualsStructure(left, checkedChanged));
  }

  // --- MenuItem.attr is chain-style: returned definition carries attr ---
  {
    loka::app::MenuItemDefinitionWithAttr item =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(true).checked(true));
    assert(item.hasAttr_);
    assert(item.attr_.hasDisabledValue_);
    assert(item.attr_.disabledValue_);
    assert(item.attr_.hasCheckedValue_);
    assert(item.attr_.checkedValue_);
  }

  // --- MenuItem disabled attr -> effective enabled projection ---
  {
    loka::app::MenuItemDefinitionWithAttr disabledByValue =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(true));
    assert(!disabledByValue.isEnabledInitial());
    assert(disabledByValue.enabledBindingState() == 0);

    loka::core::MutableState<bool> disabledState(false);
    loka::app::MenuItemDefinitionWithAttr disabledByState =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().disabled(&disabledState));
    assert(disabledByState.isEnabledInitial());
    assert(disabledByState.enabledBindingState() == &disabledState);
    assert(disabledByState.enabledBindingInvert());
    disabledState.set(true);
    assert(!disabledByState.isEnabledInitial());

    loka::core::MutableState<bool> enabledState(true);
    loka::app::MenuItemDefinitionWithAttr explicitEnabled =
        loka::app::MenuItem("Open").enabled(&enabledState).attr(loka::app::MenuItemAttr().disabled(true));
    assert(explicitEnabled.enabledBindingState() == &enabledState);
    assert(!explicitEnabled.enabledBindingInvert());
    assert(explicitEnabled.isEnabledInitial());
    enabledState.set(false);
    assert(!explicitEnabled.isEnabledInitial());
  }

  // --- MenuItem visible projection ---
  {
    loka::app::MenuItemDefinitionWithAttr hiddenByValue =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().visible(false));
    assert(!hiddenByValue.isVisibleInitial());

    loka::core::MutableState<bool> visibleState(false);
    loka::app::MenuItemDefinitionWithAttr hiddenByState =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().visible(&visibleState));
    assert(!hiddenByState.isVisibleInitial());
    visibleState.set(true);
    assert(hiddenByState.isVisibleInitial());
  }

  // --- MenuItem checked projection ---
  {
    loka::app::MenuItemDefinition defaultItem = loka::app::MenuItem("Open");
    LOKA_VERIFY(!defaultItem.isCheckedInitial());
    LOKA_VERIFY(defaultItem.checkedBindingState() == 0);

    loka::app::MenuItemDefinitionWithAttr checkedByValue =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().checked(true));
    LOKA_VERIFY(checkedByValue.isCheckedInitial());
    LOKA_VERIFY(checkedByValue.checkedBindingState() == 0);

    loka::core::MutableState<bool> checkedState(false);
    loka::app::MenuItemDefinitionWithAttr checkedByState =
        loka::app::MenuItem("Open").attr(loka::app::MenuItemAttr().checked(&checkedState));
    LOKA_VERIFY(!checkedByState.isCheckedInitial());
    LOKA_VERIFY(checkedByState.checkedBindingState() == &checkedState);
  }

  printf("==== [testLokaAttrDslV1Core] end ====\n");
}

void testPlatformNodeHandlerRegistration()
{
  printf("\n==== [testPlatformNodeHandlerRegistration] start ====\n");

  AttrDslCustomExternalNode node;
  AttrDslCustomExternalHandler handler;

  {
    loka::app::scene::PlatformNodeHandlerRegistry registry;
    assert(registry.find(&node) == 0);
    LOKA_VERIFY(registry.registerHandler(&handler));
    assert(registry.find(&node) == &handler);
  }

  AttrDslDummyRegistrationPlatformController controller;
  loka::app::scene::IPlatformController &platform = controller;
  LOKA_VERIFY(platform.registerNodeHandler(&handler));
  assert(controller.findHandler(&node) == &handler);

  loka::app::scene::LayoutState state;
  state.x = 4;
  state.y = 8;
  state.width = 16;
  state.height = 32;
  loka::app::scene::IPlatformNodeHandler *resolved = controller.findHandler(&node);
  assert(resolved != 0);
  loka::app::scene::NodeContext *context = resolved->ensureContext(&node, &platform, state);
  (void)context;
  assert(context != 0);
  LOKA_VERIFY(node.getContext() == context);
  assert(handler.ensureCalls_ == 1);
  assert(handler.lastState_.x == 4);
  assert(handler.lastState_.y == 8);
  assert(handler.lastState_.width == 16);
  assert(handler.lastState_.height == 32);

  printf("==== [testPlatformNodeHandlerRegistration] end ====\n");
}

void testPlatformNodeHandlerReplacement()
{
  printf("\n==== [testPlatformNodeHandlerReplacement] start ====\n");

  AttrDslCustomExternalNode node;
  AttrDslCustomExternalHandler first;
  AttrDslSecondExternalHandler second;

  loka::app::scene::PlatformNodeHandlerRegistry registry;
  LOKA_VERIFY(registry.registerHandler(&first));
  assert(registry.find(&node) == &first);
  LOKA_VERIFY(registry.registerHandler(&second));
  assert(registry.find(&node) == &second);

  printf("==== [testPlatformNodeHandlerReplacement] end ====\n");
}

void testPlatformNodeHandlerRejectsInvalidTypeKey()
{
  printf("\n==== [testPlatformNodeHandlerRejectsInvalidTypeKey] start ====\n");

  AttrDslCustomExternalNode node;
  AttrDslInvalidExternalHandler invalid;
  loka::app::scene::PlatformNodeHandlerRegistry registry;

  LOKA_VERIFY(!registry.registerHandler(&invalid));
  assert(registry.find(&node) == 0);

  printf("==== [testPlatformNodeHandlerRejectsInvalidTypeKey] end ====\n");
}

void testPlatformLayoutHandlerRegistration()
{
  printf("\n==== [testPlatformLayoutHandlerRegistration] start ====\n");

  AttrDslCustomLayoutNode node;
  AttrDslCustomLayoutLeafNode *child = new AttrDslCustomLayoutLeafNode();
  node.addChild(child);

  AttrDslDummyLayoutTraversal traversal;
  traversal.nextResult_ = 73;

  {
    loka::app::scene::PlatformLayoutHandlerRegistry registry;
    AttrDslCustomLayoutHandler *handler = new AttrDslCustomLayoutHandler();
    assert(handler != 0);
    assert(registry.find(&node) == 0);
    LOKA_VERIFY(registry.registerHandler(handler));
    assert(registry.find(&node) == handler);

    loka::app::scene::LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 40;
    state.height = 50;

    loka::app::scene::IPlatformLayoutHandler *resolved = registry.find(&node);
    assert(resolved != 0);
    const int resultY = resolved->layoutNode(&node, state, &traversal);
    (void)resultY;
    assert(resultY == 73);
    assert(handler->layoutCalls_ == 1);
    assert(handler->lastState_.x == 10);
    assert(handler->lastState_.y == 20);
    assert(traversal.calls_ == 1);
    assert(traversal.lastChild_ == child);
    assert(traversal.lastState_.x == 13);
    assert(traversal.lastState_.y == 25);
    assert(traversal.lastState_.width == 34);
    assert(traversal.lastState_.height == 50);
  }

  printf("==== [testPlatformLayoutHandlerRegistration] end ====\n");
}

void testPlatformLayoutTraversalResultY()
{
  printf("\n==== [testPlatformLayoutTraversalResultY] start ====\n");

  AttrDslCustomLayoutNode node;
  AttrDslCustomLayoutLeafNode *child = new AttrDslCustomLayoutLeafNode();
  node.addChild(child);

  AttrDslDummyLayoutTraversal traversal;
  traversal.nextResult_ = 31;
  traversal.setLayoutResultY(0);

  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  AttrDslTraversalResultYLayoutHandler *handler = new AttrDslTraversalResultYLayoutHandler();
  assert(handler != 0);
  LOKA_VERIFY(registry.registerHandler(handler));

  loka::app::scene::LayoutState state;
  state.x = 1;
  state.y = 2;
  state.width = 30;
  state.height = 40;

  loka::app::scene::IPlatformLayoutHandler *resolved = registry.find(&node);
  assert(resolved != 0);
  const int resultY = resolved->layoutNode(&node, state, &traversal);
  assert(traversal.calls_ == 1);
  assert(traversal.lastChild_ == child);
  assert(traversal.lastState_.y == 6);
  LOKA_VERIFY(traversal.layoutResultY() == 37);
  (void)resultY;
  assert(resultY == 37);

  printf("==== [testPlatformLayoutTraversalResultY] end ====\n");
}

void testPlatformLayoutHandlerReplacement()
{
  printf("\n==== [testPlatformLayoutHandlerReplacement] start ====\n");

  AttrDslCustomLayoutNode node;

  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  AttrDslCustomLayoutHandler *first = new AttrDslCustomLayoutHandler();
  AttrDslSecondLayoutHandler *second = new AttrDslSecondLayoutHandler();
  assert(first != 0);
  assert(second != 0);
  second->result_ = 91;

  LOKA_VERIFY(registry.registerHandler(first));
  assert(registry.find(&node) == first);
  LOKA_VERIFY(registry.registerHandler(second));
  assert(registry.find(&node) == second);

  AttrDslDummyLayoutTraversal traversal;
  loka::app::scene::LayoutState state;
  state.x = 5;
  state.y = 7;
  state.width = 9;
  state.height = 11;

  loka::app::scene::IPlatformLayoutHandler *resolved = registry.find(&node);
  (void)resolved;
  assert(resolved != 0);
  assert(resolved->layoutNode(&node, state, &traversal) == 91);
  assert(second->calls_ == 1);

  printf("==== [testPlatformLayoutHandlerReplacement] end ====\n");
}

void testPlatformLayoutHandlerSamePointerReregister()
{
  printf("\n==== [testPlatformLayoutHandlerSamePointerReregister] start ====\n");

  AttrDslCustomLayoutNode node;
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  AttrDslSecondLayoutHandler *handler = new AttrDslSecondLayoutHandler();
  assert(handler != 0);
  handler->result_ = 41;

  LOKA_VERIFY(registry.registerHandler(handler));
  assert(registry.find(&node) == handler);
  LOKA_VERIFY(registry.registerHandler(handler));
  assert(registry.find(&node) == handler);

  AttrDslDummyLayoutTraversal traversal;
  loka::app::scene::LayoutState state;
  const int resultY = handler->layoutNode(&node, state, &traversal);
  (void)resultY;
  assert(resultY == 41);
  assert(handler->calls_ == 1);

  printf("==== [testPlatformLayoutHandlerSamePointerReregister] end ====\n");
}

void testPlatformLayoutHandlerRejectsInvalidTypeKey()
{
  printf("\n==== [testPlatformLayoutHandlerRejectsInvalidTypeKey] start ====\n");

  AttrDslCustomLayoutNode node;
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  AttrDslInvalidLayoutHandler *invalid = new AttrDslInvalidLayoutHandler();
  assert(invalid != 0);

  LOKA_VERIFY(!registry.registerHandler(invalid));
  assert(registry.find(&node) == 0);

  printf("==== [testPlatformLayoutHandlerRejectsInvalidTypeKey] end ====\n");
}

void testPrepareProjectedLayoutDelegation()
{
  printf("\n==== [testPrepareProjectedLayoutDelegation] start ====\n");

  AttrDslDummyRegistrationPlatformController controller;
  AttrDslProjectedExternalNode node;
  controller.prepareProjectedResult_ = true;

  loka::app::scene::LayoutState state;
  state.x = 3;
  state.y = 12;
  state.width = 20;
  state.height = 24;

  const short resultY = node.layoutProjected(&controller, state);
  (void)resultY;
  assert(resultY == 21);
  assert(node.projectedCalls_ == 1);
  assert(controller.prepareProjectedCalls_ == 1);
  assert(controller.lastPreparedNode_ == &node);
  assert(controller.lastPreparedState_.x == 3);
  assert(controller.lastPreparedState_.y == 12);
  assert(controller.lastPreparedState_.width == 20);
  assert(controller.lastPreparedState_.height == 24);

  controller.prepareProjectedResult_ = false;
  LOKA_VERIFY(loka::app::scene::PrepareProjectedLayout(&controller, &node, state) == false);
  assert(controller.prepareProjectedCalls_ == 2);

  printf("==== [testPrepareProjectedLayoutDelegation] end ====\n");
}

void testProjectedLayoutUsesActiveBoundaryModel()
{
  printf("\n==== [testProjectedLayoutUsesActiveBoundaryModel] start ====\n");

  AttrDslBoundaryAwareController controller;
  AttrDslBoundaryAwareProjectedNode node;
  AttrDslConcreteBoundaryNode activeBoundary;

  loka::app::scene::LayoutState state;
  state.x = 2;
  state.y = 14;
  state.width = 18;
  state.height = 22;

  const short resultY = simulateProjectedLayoutWithActiveBoundary(&node, &controller, &activeBoundary, state);
  (void)resultY;
  assert(resultY == 14);
  assert(node.projectedCalls_ == 1);
  assert(controller.prepareCalls_ == 1);
  assert(controller.preparedNode_ == &node);
  assert(controller.preparedBoundary_ == &activeBoundary);
  assert(controller.preparedState_.x == 2);
  assert(controller.preparedState_.y == 14);
  assert(controller.preparedState_.width == 18);
  assert(controller.preparedState_.height == 22);

  printf("==== [testProjectedLayoutUsesActiveBoundaryModel] end ====\n");
}

void testPrepareProjectedLayoutRejectsNullController()
{
  printf("\n==== [testPrepareProjectedLayoutRejectsNullController] start ====\n");

  AttrDslProjectedExternalNode node;
  loka::app::scene::LayoutState state;
  state.x = 1;
  state.y = 2;
  state.width = 3;
  state.height = 4;

  LOKA_VERIFY(!loka::app::scene::PrepareProjectedLayout(0, &node, state));

  printf("==== [testPrepareProjectedLayoutRejectsNullController] end ====\n");
}

void testContainerLayoutHelpersAdvanceResultY()
{
  printf("\n==== [testContainerLayoutHelpersAdvanceResultY] start ====\n");

  loka::app::scene::LayoutState state;
  state.x = 10;
  state.y = 20;
  state.width = 100;
  state.height = 60;

  {
    loka::app::BoxProps props;
    props.setPadding(6);
    loka::app::BoxNode box(props);

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeBoxLayoutResultY(&box,
                                                                   loka::app::layout::RailSpacing(12, 4, 9),
                                                                   state,
                                                                   &recorder,
                                                                   &recordLayoutChild,
                                                                   0,
                                                                   &loka::testing::acceptShortLayoutY);
    assert(recorder.count == 0);
    (void)resultY;
    assert(resultY == 32);
  }

  {
    loka::app::BoxProps props;
    props.setPadding(4);
    loka::app::BoxNode box(props);
    AttrDslCustomLayoutLeafNode *first = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *second = new AttrDslCustomLayoutLeafNode();
    box.addChild(first);
    box.addChild(second);

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeBoxLayoutResultY(&box,
                                                                   loka::app::layout::RailSpacing(12, 4, 9),
                                                                   state,
                                                                   &recorder,
                                                                   &recordLayoutChild,
                                                                   0,
                                                                   &loka::testing::acceptShortLayoutY);
    assert(recorder.count == 2);
    assert(recorder.calls[0].state.x == 14);
    assert(recorder.calls[0].state.y == 24);
    assert(recorder.calls[0].state.width == 92);
    assert(recorder.calls[0].state.height == 52);
    (void)resultY;
    assert(recorder.calls[1].state.y == recorder.calls[0].resultY);
    assert(resultY == recorder.calls[1].resultY + 4);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_COLUMN);
    props.alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_CENTER);
    loka::app::StackNode column(props);
    AttrDslCustomLayoutLeafNode *first = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *second = new AttrDslCustomLayoutLeafNode();
    column.addChild(first);
    column.addChild(second);

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeColumnLayoutResultY(
        &column, state, &recorder, &recordLayoutChild, 0, &loka::testing::acceptShortLayoutY);
    assert(recorder.count == 2);
    assert(recorder.calls[0].state.y == 20);
    (void)resultY;
    assert(recorder.calls[1].state.y == recorder.calls[0].resultY);
    assert(resultY == recorder.calls[1].resultY);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_COLUMN);
    props.alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_CENTER);
    loka::app::StackNode column(props);
    loka::app::ImageViewProps imageProps;
    imageProps.size(30, 12);
    loka::app::ImageViewNode *image = new loka::app::ImageViewNode(imageProps);
    column.addChild(image);

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeColumnLayoutResultY(
        &column, state, &recorder, &recordLayoutChild, 0, &loka::testing::acceptShortLayoutY);
    assert(recorder.count == 1);
    assert(recorder.calls[0].state.x == 45);
    assert(recorder.calls[0].state.width == 30);
    (void)resultY;
    assert(resultY == recorder.calls[0].resultY);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_COLUMN);
    props.alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_TRAILING);
    loka::app::StackNode column(props);
    loka::app::ImageViewProps imageProps;
    imageProps.size(30, 12);
    loka::app::ImageViewNode *image = new loka::app::ImageViewNode(imageProps);
    column.addChild(image);

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeColumnLayoutResultY(
        &column, state, &recorder, &recordLayoutChild, 0, &loka::testing::acceptShortLayoutY);
    assert(recorder.count == 1);
    assert(recorder.calls[0].state.x == 80);
    assert(recorder.calls[0].state.width == 30);
    (void)resultY;
    assert(resultY == recorder.calls[0].resultY);
  }

  {
    loka::app::ZStackProps props;
    loka::app::ZStackNode stack(props);
    AttrDslOffsetLayoutLeafNode *first = new AttrDslOffsetLayoutLeafNode(1);
    AttrDslOffsetLayoutLeafNode *second = new AttrDslOffsetLayoutLeafNode(7);
    stack.addChild(first);
    stack.addChild(second);

    AttrDslLayoutCallRecorder recorder;
    const int resultY =
        loka::app::layout::computeZStackLayoutResultY(&stack, state, &recorder, &recordLayoutChildWithTaggedOffset);
    assert(recorder.count == 2);
    assert(recorder.calls[0].state.y == 20);
    assert(recorder.calls[1].state.y == 20);
    (void)resultY;
    assert(recorder.calls[0].resultY == 81);
    assert(recorder.calls[1].resultY == 87);
    assert(resultY == recorder.calls[1].resultY);
  }

  {
    loka::app::GridProps props;
    props.rows = 2;
    props.cols = 2;
    loka::app::GridNode grid(props);
    AttrDslCustomLayoutLeafNode *first = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *second = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *third = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *fourth = new AttrDslCustomLayoutLeafNode();
    grid.addChild(first);
    grid.addChild(second);
    grid.addChild(third);
    grid.addChild(fourth);

    loka::app::layout::GridLayoutMetrics metrics;
    metrics.gapX = 3;
    metrics.gapY = 5;
    AttrDslLayoutCallRecorder recorder;
    const int resultY =
        loka::app::layout::computeGridLayoutResultY(&grid, state, metrics, &recorder, &recordLayoutChild);
    assert(recorder.count == 4);
    assert(recorder.calls[0].state.x == 10);
    assert(recorder.calls[0].state.y == 20);
    assert(recorder.calls[1].state.x == 10 + recorder.calls[0].state.width + 3);
    assert(recorder.calls[1].state.y == 20);
    assert(recorder.calls[2].state.x == 10);
    assert(recorder.calls[2].state.y == 20 + recorder.calls[0].state.height + 5);
    assert(recorder.calls[3].state.x == 10 + recorder.calls[2].state.width + 3);
    assert(recorder.calls[3].state.y == recorder.calls[2].state.y);
    (void)resultY;
    assert(resultY == recorder.calls[2].resultY);
  }

  {
    loka::app::GridProps props;
    props.rows = 1;
    props.cols = 2;
    loka::app::GridNode grid(props);
    AttrDslCustomLayoutLeafNode *first = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *second = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *third = new AttrDslCustomLayoutLeafNode();
    grid.addChild(first);
    grid.addChild(second);
    grid.addChild(third);

    loka::app::layout::GridLayoutMetrics metrics;
    metrics.gapX = 4;
    metrics.gapY = 6;
    AttrDslLayoutCallRecorder recorder;
    const int resultY =
        loka::app::layout::computeGridLayoutResultY(&grid, state, metrics, &recorder, &recordLayoutChild);
    assert(recorder.count == 2);
    assert(recorder.calls[0].node == first);
    assert(recorder.calls[1].node == second);
    (void)resultY;
    assert(resultY == recorder.calls[0].resultY);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_ROW);
    loka::app::StackNode row(props);
    loka::app::TextProps textProps;
    textProps.text("Row");
    loka::app::TextNode *text = new loka::app::TextNode(textProps);
    loka::app::ButtonProps buttonProps;
    buttonProps.text("Run");
    loka::app::ButtonNode *button = new loka::app::ButtonNode(buttonProps);
    AttrDslCustomLayoutLeafNode *third = new AttrDslCustomLayoutLeafNode();
    row.addChild(text);
    row.addChild(button);
    row.addChild(third);

    loka::app::layout::RowLayoutMetrics metrics;

    metrics.fallbackHeight = 18;
    metrics.buttonHeight = 24;
    metrics.editTextHeight = 22;
    metrics.popupMenuHeight = 20;
    metrics.textHeight = 16;
    metrics.imageFallbackHeight = 30;

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeRowLayoutResultY(&row,
                                                                   state,
                                                                   metrics,
                                                                   loka::app::layout::RailSpacing(12, 2, 9),
                                                                   0, // Synthetic allocation probe has no rail source.
                                                                   &recorder,
                                                                   &recordLayoutChild,
                                                                   2,
                                                                   0);
    assert(recorder.count == 3);
    assert(recorder.calls[0].state.width == 32);
    assert(recorder.calls[1].state.width == 32);
    assert(recorder.calls[2].state.width == 32);
    assert(recorder.calls[0].state.x == 10);
    assert(recorder.calls[1].state.x == 44);
    assert(recorder.calls[2].state.x == 78);
    (void)resultY;
    assert(resultY == recorder.calls[2].resultY);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_ROW);
    loka::app::StackNode row(props);
    AttrDslCustomLayoutLeafNode *first = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *second = new AttrDslCustomLayoutLeafNode();
    AttrDslCustomLayoutLeafNode *third = new AttrDslCustomLayoutLeafNode();
    row.addChild(first);
    row.addChild(second);
    row.addChild(third);

    loka::app::layout::RowLayoutMetrics metrics;

    metrics.fallbackHeight = 18;
    metrics.buttonHeight = 24;
    metrics.editTextHeight = 22;
    metrics.popupMenuHeight = 20;
    metrics.textHeight = 16;
    metrics.imageFallbackHeight = 30;

    loka::app::scene::LayoutState unevenState = state;
    unevenState.width = 101;
    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeRowLayoutResultY(&row,
                                                                   unevenState,
                                                                   metrics,
                                                                   loka::app::layout::RailSpacing(12, 2, 9),
                                                                   0, // Synthetic allocation probe has no rail source.
                                                                   &recorder,
                                                                   &recordLayoutChild,
                                                                   2,
                                                                   0);
    assert(recorder.count == 3);
    assert(recorder.calls[0].state.width == 33);
    assert(recorder.calls[1].state.width == 32);
    assert(recorder.calls[2].state.width == 32);
    assert(recorder.calls[0].state.x == 10);
    assert(recorder.calls[1].state.x == 45);
    assert(recorder.calls[2].state.x == 79);
    (void)resultY;
    assert(resultY == recorder.calls[2].resultY);
  }

  {
    loka::app::StackProps props(loka::app::STACK_AXIS_ROW);
    props.alignVertical(loka::app::VERTICAL_ALIGNMENT_BOTTOM);
    loka::app::StackNode row(props);
    loka::app::TextProps textProps;
    textProps.text("Row");
    loka::app::TextNode *text = new loka::app::TextNode(textProps);
    loka::app::ButtonProps buttonProps;
    buttonProps.text("Run");
    loka::app::ButtonNode *button = new loka::app::ButtonNode(buttonProps);
    row.addChild(text);
    row.addChild(button);

    loka::app::layout::RowLayoutMetrics metrics;

    metrics.fallbackHeight = 18;
    metrics.buttonHeight = 24;
    metrics.editTextHeight = 22;
    metrics.popupMenuHeight = 20;
    metrics.textHeight = 16;
    metrics.imageFallbackHeight = 30;

    AttrDslLayoutCallRecorder recorder;
    const int resultY = loka::app::layout::computeRowLayoutResultY(&row,
                                                                   state,
                                                                   metrics,
                                                                   loka::app::layout::RailSpacing(12, 2, 9),
                                                                   0, // Synthetic allocation probe has no rail source.
                                                                   &recorder,
                                                                   &recordLayoutChild,
                                                                   2,
                                                                   0);
    assert(recorder.count == 2);
    assert(recorder.calls[0].state.height == 16);
    assert(recorder.calls[0].state.y == 28);
    assert(recorder.calls[1].state.height == 24);
    assert(recorder.calls[1].state.y == 20);
    (void)resultY;
    assert(resultY == recorder.calls[1].resultY);
  }

  printf("==== [testContainerLayoutHelpersAdvanceResultY] end ====\n");
}
