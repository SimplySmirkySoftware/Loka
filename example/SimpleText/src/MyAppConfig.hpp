#ifndef LOKA_SIMPLE_TEXT_APP_CONFIG_HPP
#define LOKA_SIMPLE_TEXT_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowSeat.hpp"
#include "MainNode.hpp"

class SimpleTextAppConfig : public AppConfigurable
{
public:
  explicit SimpleTextAppConfig(PlatformContext *context)
      : AppConfigurable(context)
  {
    // A refused seed is reported by App::bootstrapWindowless after launch admission.
    this->documents_.open(simpletext::Document());
  }
  loka::app::DocumentRoster<simpletext::Document, simpletext::kMaxDocuments> &documents()
  {
    return this->documents_;
  }
  const loka::app::DocumentRoster<simpletext::Document, simpletext::kMaxDocuments> &documents() const
  {
    return this->documents_;
  }
  virtual void compose(AppComposition &c)
  {
    c << loka::app::DocumentWindows(this->documents_, &SimpleTextAppConfig::documentWindow, this);
  }

private:
  loka::app::DocumentRoster<simpletext::Document, simpletext::kMaxDocuments> documents_;

  static WindowProps documentWindow(loka::core::ItemId id, const simpletext::Document &, void *userData)
  {
    using namespace simpletext;
    SimpleTextAppConfig *config = static_cast<SimpleTextAppConfig *>(userData);
    const int offset = 24 * ((id.seq - 1) % kMaxDocuments);
    return WindowProps()
        .frame(16 + offset, 16 + offset, 480, 320)
        .contentMargin(0)
        .visible(true)
        .scene(loka::app::scene::Boundary<MainNode>( //
            MainProps().platformContext(config->getPlatformContext()).documents(&config->documents_).document(id)));
  }
};
#endif
