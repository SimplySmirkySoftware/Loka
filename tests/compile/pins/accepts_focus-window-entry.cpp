#include "app/core/App.hpp"
#include "app/core/Window.hpp"

void pin(App &app, Window &window)
{
  (void)app;
  (void)window;
  app.reconcileAtCompletion();
}
