#ifndef LOKA_MAC_PRESENTED_OWNER_TEST_ACCESS_HPP
#define LOKA_MAC_PRESENTED_OWNER_TEST_ACCESS_HPP

#include "../MacObjCCompat.hpp"

/** Borrowed factory fields, copied once per synthetic observation. */
struct MacTestPresentedFields
{
  int path;
  NSSize size;
  CGImageRef image;
  CGFloat scale;
  NSString *gravity;
};

/** A fixture owns one private rail presentation and drives its real factories and writer. */
class MacTestPresentedOwner
{
public:
  /** Construct one NONE owner per test scope. */
  MacTestPresentedOwner();
  /** Destroy one owner, releasing at most one image. */
  ~MacTestPresentedOwner();
  /** Replace once per synthetic window commit, no rows. */
  void window(NSSize size);
  /** Replace once per synthetic layer commit, no rows. */
  void layer(CGImageRef image, NSSize size, CGFloat scale, NSString *gravity);
  /** Read one tuple from this owner, without retaining its image. */
  MacTestPresentedFields read() const;

private:
  void *owner_;
  MacTestPresentedOwner(const MacTestPresentedOwner &);
  MacTestPresentedOwner &operator=(const MacTestPresentedOwner &);
};
#endif
