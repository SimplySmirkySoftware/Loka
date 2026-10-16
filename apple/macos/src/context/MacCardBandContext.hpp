#ifndef LOKA_MAC_CARD_BAND_CONTEXT_HPP
#define LOKA_MAC_CARD_BAND_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "app/nodes/controls/Card.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

/** Owns one transparent band view through the same fact and retirement doors as Cell. */
class MacCardBandContext : public MacRetirableContext
{
public:
  MacCardBandContext(MacScenePlatformController *controller,
                     void *parentView,
                     int x,
                     int y,
                     int width,
                     int height,
                     loka::app::CardBandNode *node);
  virtual ~MacCardBandContext();
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  virtual bool acceptsInstallation() const;
  virtual void onPropsApplied();
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  void relayout(int x, int y, int width, int height);
  /** Synchronous native draw callback; retirement clears the view back-pointer. */
  void draw() const;

private:
  loka::app::CardBandNode *node_;
  void *view_;
  loka::core::Frame band_;
  loka::app::CardBandProps props_;
};

void RegisterMacCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif
