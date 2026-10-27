#include "app/nodes/controls/TextEditorRequests.hpp"
#include "../../support/TextBufferFixture.hpp"
#include <cstdio>
#include <Processes.h>

#include "app/bootstrap/RunApp.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/Menu.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "core/util/StateTrackerGuard.hpp"

#if !defined(LOKA_RETRO68)
#error Text editor application requires a Toolbox build
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::core;

  /** The scene Boundary owns all editor boxes and their borrowed list. */
  class TextEditorContent : public scene::BoundaryNodeFor<TextEditorContent>
  {
  public:
    explicit TextEditorContent(const scene::BoundaryPropsFor<TextEditorContent> &props)
        : scene::BoundaryNodeFor<TextEditorContent>(props)
    {
      this->declareStates(3)
          .state(this->cursor_, LineCursor::None())
          .state(this->request_, LineCursor::None())
          .state(this->status_, String());
    }

    virtual void declareBindings(scene::BindingToken &token)
    {
      if (!this->hasEditorStates())
        return;
      token.watch(const_cast<State<ListRevision> &>(this->lines_.revision()), this, &TextEditorContent::refreshStatus);
      token.watch(*this->cursor_.state(), this, &TextEditorContent::refreshStatus);
    }

    virtual void attachNode(scene::NodeComposition &)
    {
      if (!this->hasEditorStates())
        return;
      StateTracker *owner = 0;
      if (this->lines_.queryMutationTracker(owner) == EDIT_OK)
        return;
      if (this->lines_.attach(this->tracker()->asPushTracker(), 256) != ATTACH_OK)
        return;
      const ListOp<String> initial[] = {ListOp<String>(INSERT, ItemId(), 0, String::Literal("first")),
                                        ListOp<String>(INSERT, ItemId(), 1, String::Literal("second")),
                                        ListOp<String>(INSERT, ItemId(), 2, String::Literal("third"))};
      ArrayListOpCursor<String> replacement(initial, 3);
      if (this->lines_.apply(replacement) != EDIT_OK)
        return;
      this->request_.set(LineCursor(this->lines_.at(0).id, 0));
    }

    virtual void composeNode(scene::NodeComposition &composition)
    {
      if (this->hasEditorStates() && this->lines_.size())
        composition.declare(
            Column() << Text("Type in the box; File > Quit to leave.")
                     << (Box().size(560, 300) << TextEditor(this->lines_, this->cursor_).moveCaretTo(this->request_))
                     << Text(this->status_.state()));
      else
        composition.declare(Text("Editor unavailable: document allocation failed"));
    }

  private:
    loka::testing::TextBufferFixture lines_;
    scene::Reported<LineCursor> cursor_;
    scene::Request<LineCursor> request_;
    scene::NodeState<String> status_;

    bool hasEditorStates() const
    {
      return this->cursor_.isValid() && this->request_.isValid() && this->status_.isValid();
    }

    void refreshStatus()
    {
      if (!this->hasEditorStates())
        return;
      std::size_t bytes = this->lines_.size() ? this->lines_.size() - 1 : 0;
      for (unsigned short i = 0; i < this->lines_.size(); ++i)
      {
        std::size_t units = 0;
        if (!this->lines_.at(i).value.requiredUnits(StringEncodingUtf8, units))
        {
          StateTrackerGuard transaction(this->tracker());
          this->status_.set(String::Literal("Document byte count unavailable"));
          return;
        }
        bytes += units;
      }
      const LineCursor cursor = this->cursor_.state()->get();
      char text[96];
      std::sprintf(text, "Lines: %u  Cursor: %d:%d  Bytes: %lu",
                   static_cast<unsigned>(this->lines_.size()),
                   this->lines_.find(cursor.line), cursor.column,
                   static_cast<unsigned long>(bytes));
      StateTrackerGuard transaction(this->tracker());
      this->status_.set(String::Literal(text));
    }
  };

  class TextEditorConfig : public AppConfigurable
  {
  public:
    explicit TextEditorConfig(PlatformContext *context)
        : AppConfigurable(context)
    {
    }
    virtual void compose(AppComposition &composition)
    {
      composition << WindowDef(WindowProps()
                                   .frame(20, 40, 600, 400)
                                   .title("Loka TextEditor")
                                   .visible(true)
                                   .scene(scene::Boundary<TextEditorContent>()));
    }
    virtual void composeDefaultMenu(MenuComposition &composition)
    {
      composition.declare(Menu("File") << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
    }
  };
} // namespace

int main(int, char **)
{
  const int result = loka::platform::RunApp<TextEditorConfig>();
  ExitToShell();
  return result;
}
