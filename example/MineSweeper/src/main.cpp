#include "app/bootstrap/RunApp.hpp"
#include "ProductionAppConfig.hpp"

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  (void)hPrevInstance;
  (void)lpCmdLine;
  return loka::platform::RunApp<MineSweeperProductionAppConfig>(hInstance, nCmdShow);
}
#elif defined(LOKA_RETRO68)
int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return loka::platform::RunApp<MineSweeperProductionAppConfig>();
}
#else
#error Unsupported platform for MineSweeper main.cpp
#endif
