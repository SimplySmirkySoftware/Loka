#ifndef LOKA_LAZYLIST_MAIN_NODE_HPP
#define LOKA_LAZYLIST_MAIN_NODE_HPP

#include "LazyListModel.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

namespace lazylist
{
  class MainNode;
  /** Borrowed app model: the app config outlives this boundary and its descendants. */
  struct MainProps : loka::app::scene::NodePropsBase<MainProps>
  {
    typedef MainProps TypeTag;
    typedef MainNode NodeType;
    explicit MainProps(LazyListModel *value = 0)
        : model(value)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() && this->model < static_cast<const MainProps &>(rhs).model;
    }
    LazyListModel *model;
  };

  /** Fixed cells and a native scroll fact keep viewport ownership explicit. */
  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
  public:
    explicit MainNode(const MainProps &p)
        : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(p),
          result_(),
          viewport_(),
          rename_(),
          remove_(),
          insert_(),
          move_()
    {
      assert(p.model);
      this->state(this->viewport_, loka::core::Frame(0, p.model->viewport().get().y, 0, 0));
      this->state(this->result_,
                  loka::core::String::Literal(p.model->attachment() != loka::core::ATTACH_OK ? "List allocation refused"
                                              : p.model->cards.capacity() > LOKA_LAZYFLEX_MAX_ITEMS
                                                  ? "Lazy list capacity refused"
                                                  : "Ready"));
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Box().padding(kMargin)
                << (Column() << (Box().size(kCellWidth, 40)
                                 << (Row() << Button("Rename #3", &this->rename_).TEST_ID("LazyList.Rename")
                                           << Button("Remove first", &this->remove_).TEST_ID("LazyList.Remove")
                                           << Button("Insert top", &this->insert_).TEST_ID("LazyList.Insert")
                                           << Button("Move first to end", &this->move_).TEST_ID("LazyList.Move")))
                             << (Box().size(kCellWidth, kViewportHeight)
                                 << (ScrollView(this->viewport_) << LazyColumn(this->props.model->cards, reservation::SeatNodes<reservation::Nodes<CardNode, 1,
                                      reservation::Nodes<StackNode, 1, reservation::Nodes<TextNode, 1,
                                      reservation::Nodes<ButtonNode, 1> > > > >(), 12)
                                                                            .cells(kCellWidth, kCellHeight)
                                                                            .wrap(1)
                                                                            .viewport(this->props.model->viewport())))
                             << Text(this->result_.state()).TEST_ID("LazyList.Result")));
    }

  private:
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->viewport_.state(), this, &MainNode::reportScrollOffset, true);
      token.action(this->rename_, this, &MainNode::rename);
      token.action(this->remove_, this, &MainNode::remove);
      token.action(this->insert_, this, &MainNode::insert);
      token.action(this->move_, this, &MainNode::move);
    }
    void reportScrollOffset()
    {
      this->props.model->reportScrollOffset(this->viewport_.get().y);
    }
    void rename()
    {
      this->showResult(this->props.model->renameCard(3));
    }
    void remove()
    {
      this->showResult(this->props.model->removeFirst());
    }
    void insert()
    {
      this->showResult(this->props.model->insertAtTop());
    }
    void move()
    {
      this->showResult(this->props.model->moveFirstToEnd());
    }
    void showResult(loka::core::ListEditResult result)
    {
      using namespace loka::core;
      const char *text = "Ready";
      switch (result)
      {
      case EDIT_OK:
        text = "Ready";
        break;
      case EDIT_CAPACITY_EXCEEDED:
        text = "List full: remove a card before inserting";
        break;
      case EDIT_ID_NOT_FOUND:
        text = "No card to edit";
        break;
      case EDIT_INDEX_OUT_OF_RANGE:
        text = "Card index out of range";
        break;
      case EDIT_SEQ_EXHAUSTED:
        text = "Card numbering exhausted";
        break;
      case EDIT_NOT_ATTACHED:
        text = "List is not attached";
        break;
      case EDIT_REENTRANT:
        text = "List is publishing; try again";
        break;
      case EDIT_GENERATION_EXHAUSTED:
        text = "List generation exhausted";
        break;
      }
      const String label = String::Literal(text);
      if (!this->result_.get().equals(label))
        this->result_.set(label);
    }

    loka::app::scene::NodeState<loka::core::String> result_;
    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::core::EmitterState rename_, remove_, insert_, move_;
  };
} // namespace lazylist
#endif
