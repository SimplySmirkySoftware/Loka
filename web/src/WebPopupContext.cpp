#include "WebPopupContext.hpp"
#include "WebTextDescriptor.hpp"
#include "WebUtf8Bytes.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include <climits>
using namespace loka::app;
using namespace loka::app::scene;
int WebPopupContext::projectedCount() const
{
  const loka::Vector<loka::core::String> *items = this->owner()->asPopupMenuNode()->props.items_;
  return items ? static_cast<int>(items->size() > 32767 ? 32767 : items->size()) : 0;
}
bool WebPopupContext::active() const
{
  // Deliberate admission twin of WebElementContext::active; enabled props differ.
  if (!this->controller_.acceptsInput())
    return false;
  if (!this->element_.token() || !this->owner() || this->owner()->lifecycleFact() != NODE_FACT_ATTACHED)
    return false;
  const PopupMenuProps &props = this->owner()->asPopupMenuNode()->props;
  return !props.enabled_ || props.enabled_->get();
}
bool WebPopupContext::select(int index)
{
  if (!this->active())
    return false;
  PopupMenuNode *node = this->owner()->asPopupMenuNode();
  if (!node->props.selectedIndex_.isValid())
    return false;
  if (index < 0 || index >= this->projectedCount())
    return false;
  node->props.selectedIndex_.set(index, true);
  // A synchronous watch may have replaced props during set.
  loka::core::EmitterState *emitter = node->props.onChange_;
  if (emitter)
    emitter->emit();
  return true;
}
short WebPopupContext::layout(IPlatformController *, LayoutState &state)
{
  // Deliberate refuse-then-hide twin of WebRectSurfaceContext::place.
  short line = 0, button = 0;
  const int status = WebTextLayout::rowEstimate(line, button);
  if (status == WEB_MEASURE_REFUSED)
  {
    this->controller_.refuseTextMeasurement(this->owner(), state);
    this->controller_.refuseMeasurement();
    web_set_projected(this->token(), 0);
    return state.y;
  }
  if (!this->controller_.checkCoordinate(button) || !this->controller_.checkCoordinate(state.y + button))
  {
    web_set_projected(this->token(), 0);
    return state.y;
  }
  state.height = button;
  LayoutState projected;
  if (!this->controller_.project(state, projected))
  {
    web_set_projected(this->token(), 0);
    return state.y;
  }
  web_set_rect(this->token(), projected.x, projected.y, projected.width, projected.height);
  web_set_projected(this->token(), 1);
  const int bottom = state.y + state.height;
  return static_cast<short>(bottom > SHRT_MAX ? SHRT_MAX : bottom);
}
void WebPopupContext::render(IPlatformController *)
{
  const PopupMenuProps &props = this->owner()->asPopupMenuNode()->props;
  const WebTextDescriptor descriptor((TextStyle()));
  const bool styled = web_set_text_style(this->token(), descriptor.data(), descriptor.count());
  assert(styled && "Web popup text protocol refusal");
  (void)styled;
  web_set_enabled(this->token(), !props.enabled_ || props.enabled_->get());
  const int count = this->projectedCount();
  web_set_option_count(this->token(), count);
  for (int i = 0; i < count; ++i)
  {
    const WebUtf8Bytes bytes((*props.items_)[i]);
    if (bytes.valid())
      web_set_option(this->token(), i, bytes.bytes(), static_cast<unsigned>(bytes.length()));
    else
      web_clear_option(this->token(), i);
  }
  if (!props.selectedIndex_.isValid())
    web_set_selected(this->token(), 0, 0);
  else
  {
    const int selected = props.selectedIndex_.state()->get();
    web_set_selected(this->token(), 1, count == 0 ? -1 : selected < 0 ? 0 : selected >= count ? count - 1 : selected);
  }
}
