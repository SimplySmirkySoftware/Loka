#ifndef LOKA_MAC_CLIP_BOX_CONTEXT_HPP
#define LOKA_MAC_CLIP_BOX_CONTEXT_HPP
#include "MacRetirableContext.hpp"
#include "../platform/MacProjection.hpp"

/** Registration checks two reserved keys once per call; no row walk. */
bool IsMacClipBoxType(const void *key);

/** Node-owned clip parent; native ownership transfers to the controller clock
    at retirement. The view's frame is the only stored geometry. */
class MacClipBoxContext : public MacRetirableContext, public loka::app::scene::IClipBoxContext
{
public:
  explicit MacClipBoxContext(MacScenePlatformController *controller);
  virtual ~MacClipBoxContext();
  virtual loka::app::scene::IClipBoxContext *asClipBoxContext()
  {
    return this;
  }
  /** Layout calls once per admitted visit; at most one allocation, no row walk. */
  bool ensureHost(void *parent, const loka::macos::MacRect &seat);
  /** Layout calls once per completed visit after ensureHost; one frame write, no row walk. */
  void place(const loka::macos::MacRect &rect);
  void *view() const
  {
    return this->view_;
  }
  void readLifecycleFactOnAttach();
  /** One native hide/show per fact; retirement removes and queues once. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void *view_;
  MacClipBoxContext(const MacClipBoxContext &);
  MacClipBoxContext &operator=(const MacClipBoxContext &);
};
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    void failMacClipBoxContexts(unsigned count);
    bool declineMacClipBoxContext();
    void failMacClipHostViews(unsigned count);
    bool declineMacClipHostView();
  } // namespace testing
} // namespace loka
#endif
#endif
