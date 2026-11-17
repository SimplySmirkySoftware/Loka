#include "WebAttributedTextContext.hpp"
#include "WebRunsStatus.hpp"
#include "app/nodes/AttributedText.hpp"
#include <climits>
using namespace loka::app;
using namespace loka::app::scene;
short WebAttributedTextContext::layout(IPlatformController *, LayoutState &state)
{
  const AttributedTextProps &props = this->owner()->asAttributedTextNode()->props;
  const AttributedString value = props.text_ ? props.text_->get() : AttributedString();
  int status = WEB_MEASURE_OK;
  if (!value.valid())
  {
    this->projection_.clear();
    this->controller_.refuseMeasurement();
  }
  else if (!this->projection_.get() || !this->projection_.get()->matches(value, props.blockStyle_, state.width))
  {
    WebAttributedSnapshot *built = 0;
    status = WebAttributedLayout::build(value, props.blockStyle_, state.width, built);
    if (status == WEB_MEASURE_OK)
      this->projection_.replace(built);
  }
  if (status == WEB_MEASURE_REFUSED)
  {
    this->controller_.refuseTextMeasurement(this->owner(), state);
    this->controller_.refuseMeasurement();
  }
  assert(status != WEB_MEASURE_PROTOCOL && "Web attributed build protocol refusal");
  const WebAttributedSnapshot *snapshot = this->projection_.get();
  const int height = snapshot ? snapshot->height() : 0;
  const int bottom = static_cast<int>(state.y) + height;
  if (bottom > SHRT_MAX)
  {
    this->controller_.checkCoordinate(bottom);
    return state.y;
  }
  state.height = static_cast<short>(height);
  LayoutState projected;
  if (this->controller_.project(state, projected))
    web_set_rect(this->element_.token(), projected.x, projected.y, projected.width, projected.height);
  return static_cast<short>(bottom);
}
void WebAttributedTextContext::render(IPlatformController *)
{
  const WebAttributedSnapshot *snapshot = this->projection_.get();
  web_set_projected(this->element_.token(), snapshot ? 1 : 0);
  if (!snapshot)
    return;
  const int status = web_set_runs(
      this->element_.token(), snapshot->words(), snapshot->wordCount(), snapshot->bytes(), snapshot->byteLength());
  if (status == WEB_RUNS_REFUSED)
    this->controller_.requestRelayout();
  assert(status != WEB_RUNS_PROTOCOL && "Web runs protocol refusal");
}
void WebAttributedTextContext::onFactChanged(NodeLifecycleFact old, NodeLifecycleFact next)
{
  if (next == NODE_FACT_RETIRED)
    this->projection_.clear();
  WebResourceContext::onFactChanged(old, next);
}
