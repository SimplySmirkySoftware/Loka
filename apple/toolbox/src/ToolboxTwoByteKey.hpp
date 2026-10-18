#ifndef LOKA_TOOLBOX_TWO_BYTE_KEY_HPP
#define LOKA_TOOLBOX_TWO_BYTE_KEY_HPP

#include <cassert>

/** Holds a lead byte between the two key-down events of a two-byte character.
    Only the key-down branch of ToolboxApp::run writes or reads this app-owned
    value. It cannot be derived from the current event because it spans events. */
class ToolboxTwoByteKey
{
public:
  ToolboxTwoByteKey()
      : lead_(0)
  {
  }
  bool holdsLead() const
  {
    return this->lead_ != 0;
  }
  unsigned char lead() const
  {
    assert(this->holdsLead());
    return this->lead_;
  }
  void hold(unsigned char lead)
  {
    assert(!this->holdsLead());
    assert(lead >= 0x80);
    this->lead_ = lead;
  }
  void release()
  {
    this->lead_ = 0;
  }

private:
  unsigned char lead_;
};

#endif // LOKA_TOOLBOX_TWO_BYTE_KEY_HPP
