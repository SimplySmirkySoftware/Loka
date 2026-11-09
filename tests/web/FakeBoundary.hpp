#ifndef LOKA_WEB_FAKE_BOUNDARY_HPP
#define LOKA_WEB_FAKE_BOUNDARY_HPP
#include "WebBoundary.hpp"
#include <map>
#include <string>
#include <vector>
namespace webtest
{
  struct Resource
  {
    Resource()
        : kind(0),
          parent(0),
          hidden(true),
          enabled(true),
          projected(true),
          revoked(false),
          width(0),
          height(0),
          x(0),
          y(0),
          documentWidth(0),
          extent(0),
          originX(0),
          originY(0)
    {
    }
    int kind;
    uint32_t parent;
    bool hidden, enabled, projected, revoked;
    int width, height;
    int x, y, documentWidth, extent, originX, originY;
    std::string text;
  };
  struct Task
  {
    Task()
        : callback(0),
          session(0),
          kind(0),
          ms(0),
          cancelled(false)
    {
    }
    WebWakeCallback callback;
    void *session;
    int kind;
    double ms;
    bool cancelled;
  };
  extern std::map<uint32_t, Resource> resources;
  extern std::map<unsigned, Task> tasks;
  extern std::vector<std::string> calls;
  extern unsigned completed, failCreate, failHidden, failTextAdmit, failTextStyle;
  extern void (*probe)(const char *, uint32_t);
  extern unsigned measureCalls, metricCalls, measureBytes, textBuilds, textScans;
  extern int failMeasure, measureStatus, measuredWidth;
  extern int scrollStatus, appliedX, appliedY, gutterVertical, gutterHorizontal;
  extern bool geometryMeasurable;
  void reset();
  void fire(unsigned handle, double now);
  unsigned task(int kind);
  uint32_t token(int kind);
} // namespace webtest
#endif
