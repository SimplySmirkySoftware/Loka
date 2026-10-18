#include "app/bootstrap/RunApp.hpp"
#include "MyAppConfig.hpp"
#include <Foundation/Foundation.h>

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  int result = loka::platform::RunApp<CardShowcaseAppConfig>();
  // Leopard/PPC may crash while popping the outermost process-lifetime pool
  // during shutdown. The process is exiting here anyway, so let the OS reclaim
  // it instead of releasing the pool explicitly.
  (void)pool;
  return result;
}
