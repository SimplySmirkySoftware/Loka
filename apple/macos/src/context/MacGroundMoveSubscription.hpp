#ifndef LOKA_MAC_GROUND_MOVE_SUBSCRIPTION_HPP
#define LOKA_MAC_GROUND_MOVE_SUBSCRIPTION_HPP

class MacScrollViewContext;
class MacZStackContext;

/** ZStack-owned synchronous subscription to its direct ScrollView ground.
    Both terminal paths detach before either context can be reclaimed. */
class MacGroundMoveSubscription
{
public:
  explicit MacGroundMoveSubscription(MacZStackContext &owner);
  ~MacGroundMoveSubscription();
  /** Ground layout calls once per pass; unchanged identity is O(1), no rows. */
  void attach(MacScrollViewContext &ground);
  /** Replacement or either terminal path calls once; O(1), no rows. */
  void detach();
  /** The two scroll completion doors call once per move; walks only the
      owning ZStack's units and dirties each photo band's overlap. */
  void receive(void *scrollView);

private:
  MacZStackContext &owner_;
  MacScrollViewContext *ground_;
  MacGroundMoveSubscription(const MacGroundMoveSubscription &);
  MacGroundMoveSubscription &operator=(const MacGroundMoveSubscription &);
};

#endif
