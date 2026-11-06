#ifndef LOKA_TEST_TOOLBOX_BUTTON_FOLLOW_ACCESS_HPP
#define LOKA_TEST_TOOLBOX_BUTTON_FOLLOW_ACCESS_HPP
#include <Controls.h>
namespace loka
{
  namespace testing
  {
    bool CachedSilentButtonMove(bool (*query)());
    void ResetSilentButtonMoveCapability();
    /** Fixture-owned receiver is borrowed only within its lexical scope. */
    class SilentButtonMoveObserver
    {
    public:
      virtual ~SilentButtonMoveObserver() {}
      virtual void before(ControlRef control) = 0;
      virtual void after(ControlRef control) = 0;
    };
    class SilentButtonMoveScope
    {
    public:
      explicit SilentButtonMoveScope(SilentButtonMoveObserver &observer);
      ~SilentButtonMoveScope();

    private:
      SilentButtonMoveScope(const SilentButtonMoveScope &);
      SilentButtonMoveScope &operator=(const SilentButtonMoveScope &);
    };
    void BeforeSilentButtonMove(ControlRef control);
    void AfterSilentButtonMove(ControlRef control);
  } // namespace testing
} // namespace loka
#endif
