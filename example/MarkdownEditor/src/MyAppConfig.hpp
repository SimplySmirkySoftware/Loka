#ifndef LOKA_MARKDOWN_EDITOR_APP_CONFIG_HPP
#define LOKA_MARKDOWN_EDITOR_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowSeat.hpp"
#include "MainNode.hpp"

class MarkdownEditorAppConfig : public AppConfigurable
{
public:
  explicit MarkdownEditorAppConfig(PlatformContext *context)
      : AppConfigurable(context)
  {
    // A refused seed is reported by App::bootstrapWindowless after launch admission.
    this->documents_.open(markdowneditor::Document());
  }
  loka::app::DocumentRoster<markdowneditor::Document, markdowneditor::kMaxDocuments> &documents()
  {
    return this->documents_;
  }
  const loka::app::DocumentRoster<markdowneditor::Document, markdowneditor::kMaxDocuments> &documents() const
  {
    return this->documents_;
  }
  virtual void compose(AppComposition &c)
  {
    c << loka::app::DocumentWindows(this->documents_, &MarkdownEditorAppConfig::documentWindow, this);
  }

private:
  loka::app::DocumentRoster<markdowneditor::Document, markdowneditor::kMaxDocuments> documents_;

  static WindowProps documentWindow(loka::core::ItemId id, const markdowneditor::Document &, void *userData)
  {
    using namespace markdowneditor;
    MarkdownEditorAppConfig *config = static_cast<MarkdownEditorAppConfig *>(userData);
    const int offset = 24 * ((id.seq - 1) % kMaxDocuments);
    return WindowProps()
        .frame(16 + offset, 16 + offset, 800, 500)
        .contentMargin(0)
        .visible(true)
        .scene(loka::app::scene::Boundary<MainNode>( //
            MainProps().platformContext(config->getPlatformContext()).documents(&config->documents_).document(id)));
  }
};
#endif
