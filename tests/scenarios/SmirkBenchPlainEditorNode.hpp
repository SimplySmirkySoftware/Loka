#ifndef LOKA_TESTS_SMIRK_BENCH_PLAIN_EDITOR_NODE_HPP
#define LOKA_TESTS_SMIRK_BENCH_PLAIN_EDITOR_NODE_HPP

#if !defined(TEST_BUILD)
#error SmirkBench editor-line specimen is scenario-only
#endif

#include "../support/TextBufferFixture.hpp"
#include "../../example/SmirkBench/src/MainNode.hpp"
#include "app/nodes/controls/TextEditor.hpp"

namespace loka
{
  namespace scenario_tests
  {
    struct SmirkBenchPlainEditorTypeTag
    {
    };
    class SmirkBenchPlainEditorNode;

    /** Distinct props identity makes this declaration incompatible with ordinary MainNode. */
    struct SmirkBenchPlainEditorProps : public app::scene::NodePropsBase<SmirkBenchPlainEditorProps>
    {
      typedef SmirkBenchPlainEditorTypeTag TypeTag;
      typedef SmirkBenchPlainEditorNode NodeType;

      explicit SmirkBenchPlainEditorProps(smirkbench::SmirkModel *model = 0)
          : main_(model)
      {
      }
      smirkbench::SmirkModel *model() const
      {
        return this->main_.model();
      }
      void assertInitialized() const
      {
        this->main_.assertInitialized();
      }
      bool operator<(const app::scene::PropsBase &rhs) const
      {
        return rhs.propsTypeId() == this->propsTypeId()
               && this->main_ < static_cast<const SmirkBenchPlainEditorProps &>(rhs).main_;
      }

    private:
      smirkbench::MainProps main_;
    };

    /** The scenario type owns its editor State and always declares its specimen. */
    class SmirkBenchPlainEditorNode : public smirkbench::MainNodeBase<SmirkBenchPlainEditorProps>
    {
    public:
      explicit SmirkBenchPlainEditorNode(const SmirkBenchPlainEditorProps &props)
          : smirkbench::MainNodeBase<SmirkBenchPlainEditorProps>(props)
      {
        this->declareStates(3)
            .state(this->cursor_, app::LineCursor::None())
            .state(this->request_, app::LineCursor::None());
      }
      virtual void attachNode(app::scene::NodeComposition &)
      {
        core::StateTracker *owner = 0;
        if (this->lines_.queryMutationTracker(owner) == core::EDIT_OK)
          return;
        if (this->lines_.attach(this->tracker()->asPushTracker(), 256) != core::ATTACH_OK)
          return;
        this->lines_.insert(0, core::String("first"));
        this->lines_.insert(1, core::String("second"));
        this->lines_.insert(2, core::String("third"));
        core::StateTrackerGuard guard(this->tracker());
        (void)this->request_.post(app::LineCursor(this->lines_.at(0).id, 0));
      }

      /** One action, two posts, ending at the existing final capture's caret. */
      bool queueCaretRoundTripForTesting()
      {
        if (!this->lines_.size())
          return false;
        core::StateTrackerGuard guard(this->tracker());
        return this->request_.post(app::LineCursor(this->lines_.at(0).id, 0)) == app::scene::POST_ACCEPTED
               && this->request_.post(app::LineCursor(this->lines_.at(0).id, 2)) == app::scene::POST_ACCEPTED;
      }

    protected:
      virtual void composeNavigation(app::StackDefinition &navPane)
      {
        using namespace app;
        smirkbench::MainNodeBase<SmirkBenchPlainEditorProps>::composeNavigation(navPane);
        navPane << (Column() << Text("Plain text editor")
                             << (Box().size(130, 96) << TextEditor(this->lines_, this->cursor_)
                                                            .moveCaretTo(this->request_)
                                                            .TEST_ID("SmirkBench.PlainEditor")));
      }

    private:
      loka::testing::TextBufferFixture lines_;
      app::scene::Reported<app::LineCursor> cursor_;
      app::scene::RequestQueue<app::LineCursor, 4> request_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
