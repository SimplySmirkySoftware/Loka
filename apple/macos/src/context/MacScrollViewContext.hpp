#ifndef LOKA_MAC_SCROLL_VIEW_CONTEXT_HPP
#define LOKA_MAC_SCROLL_VIEW_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

class MacScenePlatformController;
class MacGroundMoveSubscription;

/** Owns the NSScrollView that clips a ScrollView subtree and whose flipped
    document view is the native projection parent for that subtree. */
class MacScrollViewContext : public MacRetirableContext
{
public:
  /** Borrows the clip view and restores its notification setting at scope exit. */
  class BoundsNotificationMute
  {
  public:
    explicit BoundsNotificationMute(MacScrollViewContext &context);
    ~BoundsNotificationMute();

  private:
    void *const clipView_;
    const bool postsBounds_;

    BoundsNotificationMute(const BoundsNotificationMute &);
    BoundsNotificationMute &operator=(const BoundsNotificationMute &);
  };

  MacScrollViewContext(MacScenePlatformController *controller,
                       void *parentView,
                       int x,
                       int y,
                       int width,
                       int height,
                       loka::app::ScrollViewNode *node);
  virtual ~MacScrollViewContext();

  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

  bool isValid() const;
  void *documentView() const;
  int visibleWidth() const;
  void relayout(int x, int y, int width, int height);

  /** Commits both native axes after successful traversal, returning clamped Y.
      The caller publishes the completed origin after this commit. */
  int commitScrollMetrics(
      int contentWidth, int contentHeight, int viewportHeight, int clampedX, int requestedY, bool horizontalBar);
  int clampOffset(int value, int maximum) const;
  /** The horizontal range a content width would give this viewport: the
      projected content width minus the native clip width, converted once,
      so admission and the bounds notification clamp with the same number. */
  int horizontalRange(int contentWidthLu) const;

private:
  friend class MacInputDoor;
  friend class MacGroundMoveSubscription;
  /** Bounds-notification body; the rail door owns its complete borrow. */
  void publishClipViewBoundsOrigin();
  void handleScrollerStyleChange();
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  int maximumOffset(bool horizontal) const;

  MacGroundMoveSubscription *groundMove_;
  loka::app::ScrollViewNode *node_;
  void *scrollView_;
};

#endif // LOKA_MAC_SCROLL_VIEW_CONTEXT_HPP
