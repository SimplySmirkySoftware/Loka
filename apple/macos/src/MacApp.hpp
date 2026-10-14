#ifndef LOKA_MAC_APP_HPP
#define LOKA_MAC_APP_HPP

#include "app/core/App.hpp"
#include "MacMenuAttachment.hpp"
#include <mach/mach_time.h>
#include <vector>

class MacWindow;

class MacApp : public App
{
public:
  explicit MacApp(AppConfigurable *config);
  virtual ~MacApp();

  virtual void run();
  virtual void quit();
  void dispatchNativeMenuCommand(int commandId);
  void flushInvalidationsTick();

  MacMenuAttachment &menuAttachment() { return this->menuAttachment_; }

protected:
  virtual bool windowAdopted(Window *window);
  virtual void projectMenu(Window *window, const loka::app::MenuSources &sources);

private:
  void startInvalidationFlushTimer();
  void stopInvalidationFlushTimer();
  MacMenuAttachment menuAttachment_;
  void *flushTarget_;
  void *flushTimer_;
  unsigned long long lastIdleTick_;
  mach_timebase_info_data_t idleTimebase_;
};

#endif // LOKA_MAC_APP_HPP
