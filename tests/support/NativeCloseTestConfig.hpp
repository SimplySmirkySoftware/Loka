#ifndef LOKA_TESTS_NATIVE_CLOSE_TEST_CONFIG_HPP
#define LOKA_TESTS_NATIVE_CLOSE_TEST_CONFIG_HPP

#include "app/core/AppConfigurable.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowSeat.hpp"
#include "platform/null/NullPlatformContext.hpp"

/** Real rail windows with a headless context, as in the native admission pins. */
template <class NativeWindow>
class NativeCloseTestContext : public NullPlatformContext
{
public:
  virtual Window *createWindow(const WindowProps &props)
  {
    return new NativeWindow(this, props);
  }
};

/** Keep the ask unanswered so the pin can observe the native callback boundary. */
class NativeCloseTestConfig : public AppConfigurable
{
public:
  loka::app::DocumentRoster<loka::app::DocumentFacts, 1> roster;
  explicit NativeCloseTestConfig(PlatformContext &context, bool keyed)
      : AppConfigurable(&context), keyed_(keyed) {}
  virtual void compose(AppComposition &composition)
  {
    if (this->keyed_)
      composition << loka::app::DocumentWindows(this->roster, &NativeCloseTestConfig::window, this);
    else
      composition << WindowDef(WindowProps());
  }

private:
  const bool keyed_;
  static WindowProps window(loka::core::ItemId, const loka::app::DocumentFacts &, void *)
  {
    return WindowProps();
  }
};

#endif
