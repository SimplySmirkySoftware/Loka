#ifndef LOKA_APP_NODES_NESTABLE_LAZY_VIEW_HPP
#define LOKA_APP_NODES_NESTABLE_LAZY_VIEW_HPP

#include <new>
#include <functional>
#include "app/reservation/SeatNodes.hpp"
#include "app/nodes/nestable/Canvas.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/boundary/LazyScopeDefinition.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "app/scene/Scene.hpp"
#include "core/ObservableList.hpp"
#include "core/util/StateTrackerGuard.hpp"

#ifndef LOKA_LAZYFLEX_MAX_ITEMS
#define LOKA_LAZYFLEX_MAX_ITEMS 256
#endif

namespace loka
{
  namespace testing
  {
    class LazyViewAccess;
  }
  namespace app
  {
    /** Value-less arm: the list and its attachment outlive the enclosing
        generation. Every materialization reads the current item value. */
    template <class T> class LazyItem : public scene::NodeDefinitionBase
    {
      typedef scene::NodeDefinition<T, typename T::NodeType> Factory;

    public:
      LazyItem(const loka::core::ObservableList<T> &list, unsigned short index)
          : list_(&list),
            index_(index)
      {
        scene::ComponentNodeWithProps<T> *required = static_cast<typename T::NodeType *>(0);
        (void)required;
      }
      loka::core::ItemId itemId() const
      {
        return this->list_->at(this->index_).id;
      }
      virtual scene::Node *create() const
      {
        return this->factory().create();
      }
      virtual scene::Node *createInPlace(void *mem) const
      {
        return this->factory().createInPlace(mem);
      }
      virtual size_t nodeSize() const
      {
        return this->factory().nodeSize();
      }
      virtual size_t nodeAlign() const
      {
        return this->factory().nodeAlign();
      }
      virtual scene::NodeKind nodeKind() const
      {
        return this->factory().nodeKind();
      }
      typedef LazyItem CloneType;
      virtual scene::NodeDefinitionBase *clone() const
      {
        return new (std::nothrow) LazyItem(*this);
      }
      virtual const scene::PropsBase *propsBase() const
      {
        // A temporary factory's props pointer would dangle on return.
        return &this->list_->at(this->index_).value;
      }
      virtual bool applyPropsToNode(scene::Node *node) const
      {
        return this->factory().applyPropsToNode(node);
      }
      virtual bool isCompatibleWithNode(const scene::Node *node) const
      {
        return this->factory().isCompatibleWithNode(node);
      }

    private:
      Factory factory() const
      {
        Factory value(this->list_->at(this->index_).value);
        value.copyTestIdPolicyFrom(*this);
        return value;
      }
      const loka::core::ObservableList<T> *const list_;
      const unsigned short index_;
    };

    /** The complete identity of a window declaration, published by one owner. */
    struct LazyViewKey
    {
      layout::LazyWindow window;
      unsigned long structure;
      LazyViewKey()
          : window(),
            structure(0)
      {
      }
      LazyViewKey(const layout::LazyWindow &value, unsigned long revision)
          : window(value),
            structure(revision)
      {
      }
      bool operator==(const LazyViewKey &other) const
      {
        return this->window.first == other.window.first && this->window.count == other.window.count
               && this->structure == other.structure;
      }
      bool operator!=(const LazyViewKey &other) const
      {
        return !(*this == other);
      }
    };

    template <class T> class LazyGenerationNode;
    namespace lazy_view_detail
    {
      template <class T> class WindowCanvasNode;
      /** Immutable recipe value: library-static emitter, no captured application context. */
      template <class T> class WindowRecipe
      {
      public:
        template <class List> WindowRecipe(reservation::SeatNodes<List>, unsigned maximum)
            : emit_(&WindowRecipe::template emitList<List>), maximum_(maximum)
        {
          typedef reservation::Nodes<LazyGenerationNode<T>, 1,
              reservation::Nodes<FragmentNode, 1, reservation::Nodes<WindowCanvasNode<T>, 1, List> > > Full;
          reservation::detail::validate<Full>();
          assert(maximum && "LazyView needs a positive maximum window");
        }
        bool accepts(unsigned count) const
        {
          const bool accepted = this->maximum_ && count <= this->maximum_;
          assert(accepted && "LazyView window exceeds its declared maximum");
          return accepted;
        }
        bool emit(scene::detail::SeatLayoutTable &table) const
        { return this->maximum_ && this->emit_(table, this->maximum_); }
        bool operator==(const WindowRecipe &other) const
        { return this->emit_ == other.emit_ && this->maximum_ == other.maximum_; }
        bool operator<(const WindowRecipe &other) const
        { return this->emit_ != other.emit_ ? std::less<Emitter>()(this->emit_, other.emit_) : this->maximum_ < other.maximum_; }
      private:
        typedef bool (*Emitter)(scene::detail::SeatLayoutTable &, unsigned);
        template <class List> static bool emitList(scene::detail::SeatLayoutTable &table, unsigned maximum)
        {
          // createRoot, completeWindow and declareScope insert these scaffolds.
          return reservation::detail::Emitter<List>::emit(table) && table.scale(maximum)
              && table.append(scene::detail::NodeSlotLayout::of<LazyGenerationNode<T> >(1))
              && table.append(scene::detail::NodeSlotLayout::of<FragmentNode>(1))
              && table.append(scene::detail::NodeSlotLayout::of<WindowCanvasNode<T> >(1));
        }
        Emitter emit_;
        unsigned maximum_;
      };
      /** Definition-side installation borrow, like Keyed's MemberDeclarer.
          An uncommitted clone never inherits another instruction's installation. */
      template <class T> class WindowReservation
      {
      public:
        WindowReservation(scene::BoundaryNode &owner, const WindowRecipe<T> &recipe)
            : owner_(owner), recipe_(recipe), reservation_(0) {}
        WindowReservation(const WindowReservation &other)
            : owner_(other.owner_), recipe_(other.recipe_), reservation_(0) {}
        bool prepare()
        {
          if (this->reservation_) return true;
          scene::detail::SeatLayoutTable table;
          if (!this->recipe_.emit(table)) return false;
          this->reservation_ = this->owner_.installSeatReservation(table, scene::detail::PRESERVE_INSTALLED);
          return this->reservation_ != 0;
        }
        const scene::detail::SeatReservation *reservation() const { return this->reservation_; }
        bool accepts(const LazyViewKey &key) const { return this->recipe_.accepts(key.window.count); }
      private:
        WindowReservation &operator=(const WindowReservation &);
        scene::BoundaryNode &owner_;
        const WindowRecipe<T> recipe_;
        const scene::detail::SeatReservation *reservation_;
      };
    }
    template <class T> class LazyViewNode;
    /** Props own policy; list and viewport are read-only borrows from an ancestor. */
    template <class T> struct LazyViewProps : scene::NodePropsBase<LazyViewProps<T> >
    {
      typedef LazyViewProps<T> TypeTag;
      typedef LazyViewNode<T> NodeType;
      template <class List>
      LazyViewProps(const loka::core::ObservableList<T> &source,
                    reservation::SeatNodes<List> nodes, unsigned maximum,
                    const layout::LazyLayout &policy = layout::FixedGrid(1, 1))
          : recipe(nodes, maximum), list(&source),
            layout(policy),
            viewport(0)
      {
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const LazyViewProps &other = static_cast<const LazyViewProps &>(rhs);
        if (!(this->recipe == other.recipe)) return this->recipe < other.recipe;
        if (this->list != other.list)
          return this->list < other.list;
        if (this->viewport != other.viewport)
          return this->viewport < other.viewport;
        if (this->layout.kind != other.layout.kind)
          return this->layout.kind < other.layout.kind;
        if (this->layout.axis != other.layout.axis)
          return this->layout.axis < other.layout.axis;
        if (this->layout.cellWidth != other.layout.cellWidth)
          return this->layout.cellWidth < other.layout.cellWidth;
        if (this->layout.cellHeight != other.layout.cellHeight)
          return this->layout.cellHeight < other.layout.cellHeight;
        if (this->layout.wrap != other.layout.wrap)
          return this->layout.wrap < other.layout.wrap;
        return this->layout.margin < other.layout.margin;
      }
      lazy_view_detail::WindowRecipe<T> recipe;
      const loka::core::ObservableList<T> *list;
      layout::LazyLayout layout;
      loka::core::State<loka::core::Frame> *viewport;
    };

    namespace lazy_view_detail
    {
      /** Non-owning index into this generation's fixed child chain. Storage is
          owned here and reclaimed silently with the generation, never shared. */
      class ItemIndex
      {
      public:
        explicit ItemIndex(unsigned count)
            : nodes_(count
                         ? static_cast<scene::Node **>(loka::core::LokaAllocRaw(count * sizeof(scene::Node *), site()))
                         : 0)
        {
        }
        ~ItemIndex()
        {
          loka::core::LokaFreeRaw(this->nodes_, site());
        }
        bool allocated() const
        {
          return this->nodes_ != 0;
        }
        void capture(scene::Node *first)
        {
          for (unsigned i = 0; first; ++i, first = first->nextInComposition)
            this->nodes_[i] = first;
        }
        scene::Node *at(unsigned local) const
        {
          return this->nodes_[local];
        }

      private:
        static loka::core::LokaAllocationSite site()
        {
          return loka::core::LokaAllocationSite("LazyView", "ItemIndex");
        }
        scene::Node **const nodes_;
        ItemIndex(const ItemIndex &);
        ItemIndex &operator=(const ItemIndex &);
      };

      template <class T> class WindowCanvasNode;
      template <class T> struct WindowCanvasProps : scene::NodePropsBase<WindowCanvasProps<T> >
      {
        typedef WindowCanvasProps<T> TypeTag;
        typedef WindowCanvasNode<T> NodeType;
        WindowCanvasProps()
            : inputs(0),
              window(),
              viewport(0)
        {
        }
        WindowCanvasProps(const LazyViewProps<T> *source,
                          const layout::LazyWindow &range,
                          loka::core::State<loka::core::Frame> *view)
            : inputs(source),
              window(range),
              viewport(view)
        {
        }
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
        const LazyViewProps<T> *inputs;
        layout::LazyWindow window;
        loka::core::State<loka::core::Frame> *viewport;
      };
      /** The Canvas projects absolute content coordinates. ScrollView alone
          translates them; viewport origin is used only by window selection. */
      template <class T> class WindowCanvasNode : public CanvasNode
      {
      public:
        typedef WindowCanvasProps<T> Props;
        typedef typename Props::TypeTag TypeTag;
        Props props;
        explicit WindowCanvasNode(const Props &p)
            : CanvasNode(CanvasProps()),
              props(p)
        {
        }
        virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
        {
          registrar.markDirtyOnChange(this->props.viewport, scene::NODE_DIRTY_LAYOUT);
        }
        virtual CanvasPlacement placement() const
        {
          layout::LazyLayout policy = this->props.inputs->layout;
          policy.itemCount = this->props.inputs->list->size();
          const loka::core::Frame viewport = this->props.viewport->get();
          CanvasPlacement result = {policy,
                                    loka::core::Frame(0, 0, viewport.width, viewport.height),
                                    policy.extent(policy.itemCount),
                                    this->props.window.first,
                                    CanvasPlacement::PLACE_RESIDENTS};
          return result;
        }
      };
      template <class T>
      struct WindowCanvas : scene::NestableNodeDefinition<WindowCanvasProps<T>, WindowCanvasNode<T>, WindowCanvas<T> >
      {
        typedef scene::NestableNodeDefinition<WindowCanvasProps<T>, WindowCanvasNode<T>, WindowCanvas<T> > Base;
        WindowCanvas()
            : Base(WindowCanvasProps<T>())
        {
        }
        explicit WindowCanvas(const WindowCanvasProps<T> &p)
            : Base(p)
        {
        }
      };
    } // namespace lazy_view_detail

    template <class T> class LazyGenerationNode;
    /** Copied declaration instruction borrowing only the enclosing view owner. */
    template <class T> struct LazyGenerationProps : scene::NodePropsBase<LazyGenerationProps<T> >
    {
      typedef LazyGenerationProps<T> TypeTag;
      typedef LazyGenerationNode<T> NodeType;
      LazyGenerationProps(const LazyViewProps<T> *source,
                          loka::core::State<LazyViewKey> *selection,
                          loka::core::State<loka::core::Frame> *view)
          : inputs(source),
            key(selection),
            viewport(view)
      {
      }
      bool operator<(const scene::PropsBase &) const
      {
        return false;
      }
      const LazyViewProps<T> *inputs;
      loka::core::State<LazyViewKey> *key;
      loka::core::State<loka::core::Frame> *viewport;
    };

    /** One immutable window and its item-local state; no hidden-item seats. */
    template <class T> class LazyGenerationNode : public scene::LazyScopeNode
    {
    public:
      typedef LazyGenerationProps<T> Props;
      typedef typename Props::TypeTag TypeTag;
      Props props;
      explicit LazyGenerationNode(const Props &p)
          : props(p),
            window_(p.key->get().window),
            items_(this->window_.count)
      {
      }
      virtual void declareBindings(scene::BindingToken &)
      {
        if (this->window_.count && !this->items_.allocated())
          this->asStateOwner()->noteStateAllocationFailure();
      }
      virtual void declareScope(scene::NodeComposition &c)
      {
        typedef lazy_view_detail::WindowCanvasProps<T> CanvasPropsType;
        lazy_view_detail::WindowCanvas<T> canvas(
            CanvasPropsType(this->props.inputs, this->window_, this->props.viewport));
        for (unsigned i = 0; i < this->window_.count; ++i)
          canvas << LazyItem<T>(*this->props.inputs->list, static_cast<unsigned short>(this->window_.first + i));
        c.declare(canvas);
      }
      virtual void attachNode(scene::NodeComposition &)
      {
        scene::Node *fragment = this->childrenHead();
        scene::Node *canvas = fragment ? fragment->asNestable()->childrenHead() : 0;
        if (canvas)
          this->items_.capture(canvas->asNestable()->childrenHead());
      }
      void refresh(unsigned first, unsigned end)
      {
        if (first < this->window_.first)
          first = this->window_.first;
        const unsigned limit = this->window_.first + this->window_.count;
        if (end > limit)
          end = limit;
        for (unsigned i = first; i < end; ++i)
          LazyItem<T>(*this->props.inputs->list, static_cast<unsigned short>(i))
              .applyPropsToNode(this->items_.at(i - this->window_.first));
      }

    private:
      const layout::LazyWindow window_;
      lazy_view_detail::ItemIndex items_;
    };

    /** Selection is O(1); the existing LazyScope owns replacement and retry. */
    template <class T> class LazyViewNode : public scene::StdCompositionBoundaryNodeBase<LazyViewProps<T> >
    {
      typedef scene::StdCompositionBoundaryNodeBase<LazyViewProps<T> > Base;
      friend class loka::testing::LazyViewAccess;

    public:
      explicit LazyViewNode(const LazyViewProps<T> &p)
          : Base(p),
            viewport_(),
            selection_()
      {
        this->declareStates(2).state(this->viewport_, loka::core::Frame()).state(this->selection_, LazyViewKey());
      }
      virtual bool flushViewDirtyImmediately(scene::NodeDirtyFlags) const
      {
        return false;
      }
      virtual void composeWithContext(scene::ComponentContext &context, scene::ComposeEvent event)
      {
        Base::composeWithContext(context, event);
        scene::IBranchSeatDefinition *seat = this->generationSeat();
        if (event != scene::COMPOSE_EVENT_DETACH && seat && seat->seatReservation()
            && seat->seatReservation()->request().waiting() && this->getScene())
          this->getScene()->requestLayoutAfterRun();
      }

      virtual void declareBindings(scene::BindingToken &token)
      {
        if (this->props.viewport)
          token.watch(*this->props.viewport, this, &LazyViewNode::selectWindow, true);
        // Observation changes registrations, never the borrowed revision fact.
        token.watch(const_cast<loka::core::State<loka::core::ListRevision> &>(this->props.list->revision()),
                    this,
                    &LazyViewNode::revisionChanged,
                    true);
        // Props binding windows also cover changed geometry/list with an equal key.
        this->refreshContent(0, this->props.list->size());
      }
      virtual void composeNode(scene::NodeComposition &c)
      {
        if (this->props.list->capacity() > LOKA_LAZYFLEX_MAX_ITEMS)
          return;
        c.declare(
            scene::LazyScope(*this->selection_.state(),
                             LazyGenerationProps<T>(&this->props, this->selection_.state(), this->viewport_.state()),
                             lazy_view_detail::WindowReservation<T>(*this, this->props.recipe)));
      }

    private:
      scene::IBranchSeatDefinition *generationSeat()
      {
        scene::NodeDefinitionBase *definition = this->composition().root();
        return definition ? definition->asBranchSeatDefinition() : 0;
      }
      void selectWindow()
      {
        scene::IBranchSeatDefinition *seat = this->generationSeat();
        const bool wasPending = seat && seat->needsBranchDeclaration();
        layout::LazyLayout policy = this->props.layout;
        policy.itemCount = this->props.list->size();
        const loka::core::Frame viewport = this->props.viewport ? this->props.viewport->get() : loka::core::Frame();
        const layout::LazyWindow empty = {0, 0};
        const layout::LazyWindow window =
            this->props.list->capacity() > LOKA_LAZYFLEX_MAX_ITEMS ? empty : policy.indicesIn(viewport);
        loka::core::StateTrackerGuard guard(this->asStateOwner()->tracker());
        this->viewport_.set(viewport);
        this->selection_.set(LazyViewKey(window, this->props.list->revision().get().structure));
        // Returning to the installed key cancels replacement. Replay content
        // skipped while pending; the guard still refuses a different key.
        if (wasPending)
          this->refreshContent(0, this->props.list->size());
      }
      void revisionChanged()
      {
        this->selectWindow();
        const loka::core::ListChange change = this->props.list->revision().get().change;
        this->refreshContent(change.kind == loka::core::LIST_BATCH ? 0 : change.first,
                             change.kind == loka::core::LIST_BATCH ? this->props.list->size()
                                                                   : change.first + change.count);
      }
      void refreshContent(unsigned first, unsigned end)
      {
        scene::IBranchSeatDefinition *seat = this->generationSeat();
        // Mixed structure/content and refused replacement belong to the next generation.
        if (!seat || seat->needsBranchDeclaration())
          return;
        LazyGenerationNode<T> *generation = static_cast<LazyGenerationNode<T> *>(this->childrenHead());
        if (generation)
          generation->refresh(first, end);
      }
      scene::NodeState<loka::core::Frame> viewport_;
      scene::NodeState<LazyViewKey> selection_;
    };

    namespace scene
    {
      template <class NodeT, class T> struct NodePropsCompatibility<NodeT, LazyViewProps<T> >
      {
        static bool accepts(const NodeT *node, const LazyViewProps<T> &props)
        { return node->props.recipe == props.recipe; }
      };
    }

    template <class T> struct LazyView : scene::BoundaryDefinition<LazyViewProps<T>, LazyViewNode<T> >
    {
      typedef scene::BoundaryDefinition<LazyViewProps<T>, LazyViewNode<T> > Base;
      template <class List>
      LazyView(const loka::core::ObservableList<T> &list, const layout::LazyLayout &policy,
               reservation::SeatNodes<List> nodes, unsigned maximum)
          : Base(LazyViewProps<T>(list, nodes, maximum, policy))
      {
      }
      LazyView &viewport(loka::core::State<loka::core::Frame> &value)
      {
        this->props.viewport = &value;
        return *this;
      }
    };
    /** Axis defaults for the fixed-grid policy, with no extra runtime box. */
    template <class T> struct LazyGrid : LazyView<T>
    {
      template <class List>
      LazyGrid(const loka::core::ObservableList<T> &list, StackAxis axis,
               reservation::SeatNodes<List> nodes, unsigned maximum)
          : LazyView<T>(list, layout::FixedGrid(1, 1, 1, 0, axis), nodes, maximum)
      {
      }
      LazyGrid &cells(short width, short height)
      {
        this->props.layout.cellWidth = width;
        this->props.layout.cellHeight = height;
        return *this;
      }
      LazyGrid &wrap(unsigned short count)
      {
        this->props.layout.wrap = count;
        return *this;
      }
      LazyGrid &margin(unsigned short count)
      {
        this->props.layout.margin = count;
        return *this;
      }
      LazyGrid &viewport(loka::core::State<loka::core::Frame> &value)
      {
        this->props.viewport = &value;
        return *this;
      }
    };
    template <class T, class List> inline LazyGrid<T> LazyColumn(const loka::core::ObservableList<T> &list,
                                                               reservation::SeatNodes<List> nodes, unsigned maximum)
    {
      return LazyGrid<T>(list, STACK_AXIS_COLUMN, nodes, maximum);
    }
    template <class T, class List> inline LazyGrid<T> LazyRow(const loka::core::ObservableList<T> &list,
                                                               reservation::SeatNodes<List> nodes, unsigned maximum)
    {
      return LazyGrid<T>(list, STACK_AXIS_ROW, nodes, maximum);
    }
  } // namespace app
} // namespace loka
#endif
