#ifndef LOKA_TESTS_NULL_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_TESTS_NULL_ATTRIBUTED_TEXT_CONTEXT_HPP

#include "app/nodes/AttributedText.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "platform/null/context/NullPaintPlacement.hpp"
#include "platform/null/context/NullTextMetrics.hpp"

class NullScenePlatformController;

/** Null-owned projection; placement and presented content end at detach. */
class NullAttributedTextContext : public loka::app::scene::NativeNodeContext
{
public:
  NullAttributedTextContext(loka::app::AttributedTextNode *node, NullScenePlatformController &controller);
  void readLifecycleFactOnAttach() {}
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  const NullTextMeasurement &measurement() const
  {
    return this->measurement_;
  }
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  using loka::app::scene::NativeNodeContext::commitPresented;
  bool commitPresented(const loka::app::AttributedString &value, const loka::app::scene::PaintScope &scope);
  void invalidatePresentation();

private:
  friend class NullInputDoor;
  bool activateLink(std::size_t segmentIndex);
  friend struct loka::app::testing::NullTextMeasurementAccess;
  unsigned measurementBuilds_;
  loka::app::AttributedTextNode *node_;
  NullScenePlatformController &controller_;
  loka::app::scene::PaintFact<loka::app::AttributedString> presented_;
  NullPaintPlacement placement_;
  loka::app::BlockStyle placedBlock_;
  NullTextMeasurement measurement_;
};

void RegisterNullAttributedTextNodeHandler(NullScenePlatformController &controller);
const void *NullAttributedTextNodeHandlerKey();
bool IsNullAttributedTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler);
#endif
