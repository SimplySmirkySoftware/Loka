#include "WebScrollContext.hpp"
#include <algorithm>
using namespace loka::app;
loka::core::Frame WebScrollMetrics::clamp(int x, int y) const
{
  return loka::core::Frame(std::max(0, std::min(x, this->documentWidth_ - this->viewportWidth_)),
                           std::max(0, std::min(y, this->extent_ - this->viewportHeight_)),
                           0,
                           0);
}
int WebScrollMetrics::commit(loka::web::Token token, int x, int y, int &appliedX, int &appliedY) const
{
  const loka::core::Frame target = this->clamp(x, y);
  const int status = web_commit_scroll(token,
                                       this->rect_.x,
                                       this->rect_.y,
                                       this->rect_.width,
                                       this->rect_.height,
                                       this->axes_.canMove(ScrollViewVisit::X),
                                       this->documentWidth_,
                                       this->extent_,
                                       target.x,
                                       target.y,
                                       &appliedX,
                                       &appliedY);
  assert(status != loka::web::PROTOCOL && "Web scroll protocol refusal");
  if (status == loka::web::APPLIED)
  {
    const loka::core::Frame applied = this->clamp(appliedX, appliedY);
    appliedX = applied.x;
    appliedY = applied.y;
  }
  return status;
}
WebScrollContext::~WebScrollContext()
{
  delete this->metrics_;
}
bool WebScrollContext::commit(WebScrollMetrics *candidate)
{
  if (!candidate)
    return false;
  ScrollViewNode *node = this->owner()->asScrollViewNode();
  const loka::core::Frame target = node->requestedOrigin();
  int x = 0, y = 0;
  if (candidate->commit(this->token(), target.x, target.y, x, y) != loka::web::APPLIED)
  {
    delete candidate;
    return false;
  }
  node->publishViewport(x, y, candidate->width(), candidate->height());
  delete this->metrics_;
  this->metrics_ = candidate;
  return true;
}
bool WebScrollContext::apply(const loka::core::Frame &target)
{
  if (!this->metrics_)
    return false;
  int x = 0, y = 0;
  if (this->metrics_->commit(this->token(), target.x, target.y, x, y) != loka::web::APPLIED)
    return false;
  this->owner()->asScrollViewNode()->publishOrigin(x, y);
  return true;
}
bool WebScrollContext::active() const
{
  return this->attached() && this->metrics_ && this->controller_.acceptsInput();
}
void WebScrollContext::acceptScroll(int x, int y)
{
  if (!this->active())
    return;
  const loka::core::Frame applied = this->metrics_->clamp(x, y);
  this->owner()->asScrollViewNode()->publishOrigin(applied.x, applied.y);
}
