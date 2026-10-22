#ifndef LOKA_MAC_QUIT_EVENT_HPP
#define LOKA_MAC_QUIT_EVENT_HPP

struct __NSAppleEventManagerSuspension;

/** Owns at most one suspended quit Apple Event; destruction refuses it. */
class MacQuitEventReply
{
public:
  MacQuitEventReply()
      : suspension_(0)
  {
  }
  ~MacQuitEventReply();
  bool hold();
  bool isHeld() const
  {
    return this->suspension_ != 0;
  }
  void accept();
  void decline();

private:
  MacQuitEventReply(const MacQuitEventReply &);
  MacQuitEventReply &operator=(const MacQuitEventReply &);
  const __NSAppleEventManagerSuspension *suspension_;
};

#endif
