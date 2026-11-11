#include "FakeBoundary.hpp"
#include "WebStyleWire.hpp"
#include "WebTextConstants.hpp"
#include <sstream>
namespace webtest
{
  std::map<uint32_t, Resource> resources;
  std::map<unsigned, Task> tasks;
  std::vector<std::string> calls;
  unsigned completed = 0, failCreate = 0, failHidden = 0, failTextAdmit = 0, failTextStyle = 0;
  void (*probe)(const char *, uint32_t) = 0;
  int scrollStatus = 0, appliedX = -1, appliedY = -1, gutterVertical = 0, gutterHorizontal = 0;
  bool geometryMeasurable = true;
  static unsigned nextHandle = 1;
  void reset()
  {
    scrollStatus = gutterVertical = gutterHorizontal = 0;
    appliedX = appliedY = -1;
    geometryMeasurable = true;
    resources.clear();
    tasks.clear();
    calls.clear();
    completed = failCreate = failHidden = failTextAdmit = failTextStyle = 0;
    probe = 0;
    measureCalls = metricCalls = measureBytes = textBuilds = textScans = 0;
    failMeasure = failMetrics = failAdvance = 0;
    metricAscent = 10;
    metricDescent = 3;
    metricDescriptors.clear();
    advanceDescriptors.clear();
    measurementOrder.clear();
    measuredText.clear();
    measureStatus = WEB_MEASURE_REFUSED;
    measuredWidth = -1;
  }
  void fire(unsigned handle, double now)
  {
    Task copy = tasks[handle];
    tasks[handle].cancelled = true;
    // Deliver even a cancelled callback: production C++ must reject stale handles.
    if (copy.callback)
      copy.callback(copy.session, handle, copy.kind, now);
  }
  unsigned task(int kind)
  {
    for (std::map<unsigned, Task>::reverse_iterator it = tasks.rbegin(); it != tasks.rend(); ++it)
      if (it->second.kind == kind && !it->second.cancelled)
        return it->first;
    return 0;
  }
  uint32_t token(int kind)
  {
    for (std::map<uint32_t, Resource>::iterator it = resources.begin(); it != resources.end(); ++it)
      if (it->second.kind == kind && !it->second.revoked)
        return it->first;
    return 0;
  }
  static void record(const char *name, uint32_t token, const std::string &detail = std::string())
  {
    std::ostringstream out;
    out << name << ":" << token << detail;
    calls.push_back(out.str());
    if (probe)
      probe(name, token);
  }
} // namespace webtest
extern "C"
{
  int web_create(int kind, uint32_t token, uint32_t parent, int *width, int *height)
  {
    std::ostringstream args;
    args << "=" << kind << "," << parent << "," << *width << "," << *height;
    webtest::record("create", token, args.str());
    if (webtest::failCreate)
    {
      --webtest::failCreate;
      return 0;
    }
    webtest::Resource row;
    row.kind = kind;
    row.parent = parent;
    row.width = *width;
    row.height = *height;
    webtest::resources[token] = row;
    return 1;
  }
  int web_scrollbar_geometry(int *vertical, int *horizontal)
  {
    if (!webtest::geometryMeasurable)
      return 0;
    *vertical = webtest::gutterVertical;
    *horizontal = webtest::gutterHorizontal;
    return 1;
  }
  int web_commit_scroll(uint32_t token,
                        int x,
                        int y,
                        int w,
                        int h,
                        int horizontal,
                        int documentWidth,
                        int extent,
                        int originX,
                        int originY,
                        int *appliedX,
                        int *appliedY)
  {
    webtest::record("commit-scroll", token);
    if (webtest::scrollStatus)
      return webtest::scrollStatus;
    webtest::Resource &row = webtest::resources[token];
    row.x = x;
    row.y = y;
    row.width = w;
    row.height = h;
    row.documentWidth = documentWidth;
    row.extent = extent;
    row.originX = webtest::appliedX >= 0 ? webtest::appliedX : originX;
    row.originY = webtest::appliedY >= 0 ? webtest::appliedY : originY;
    *appliedX = row.originX;
    *appliedY = row.originY;
    (void)horizontal;
    return loka::web::APPLIED;
  }
  void web_set_rect(uint32_t token, int x, int y, int width, int height)
  {
    webtest::resources[token].x = x;
    webtest::resources[token].y = y;
    webtest::resources[token].width = width;
    webtest::resources[token].height = height;
    std::ostringstream args;
    args << "=" << x << "," << y << "," << width << "," << height;
    webtest::record("rect", token, args.str());
  }
  void web_set_projected(uint32_t token, int projected)
  {
    if (webtest::resources.find(token) == webtest::resources.end())
      return;
    webtest::resources[token].projected = projected != 0;
    webtest::record("projected", token, projected ? "=1" : "=0");
  }
  void web_set_text(uint32_t token, const char *bytes, unsigned length)
  {
    webtest::resources[token].text.assign(bytes, length);
    webtest::record("text", token, "=" + webtest::resources[token].text);
  }
  void web_set_enabled(uint32_t token, int enabled)
  {
    webtest::resources[token].enabled = enabled;
    webtest::record("enabled", token, enabled ? "=1" : "=0");
  }
  int web_set_hidden(uint32_t token, int hidden)
  {
    if (webtest::failHidden && webtest::resources[token].kind == loka::web::ROOT)
    {
      --webtest::failHidden;
      return 0;
    }
    webtest::resources[token].hidden = hidden;
    webtest::record("hidden", token, hidden ? "=1" : "=0");
    return 1;
  }
  void web_revoke(uint32_t token)
  {
    webtest::resources[token].revoked = true;
    webtest::record("revoke", token);
  }
  int web_release(uint32_t token)
  {
    if (!token)
      return loka::web::NO_OWNERSHIP;
    if (!webtest::resources.erase(token))
      return loka::web::ABSENT;
    webtest::record("release", token);
    return loka::web::CONSUMED;
  }
  unsigned web_arm_task(WebWakeCallback callback, void *session, int kind, double ms)
  {
    const unsigned handle = webtest::nextHandle++;
    webtest::Task row;
    row.callback = callback;
    row.session = session;
    row.kind = kind;
    row.ms = ms;
    webtest::tasks[handle] = row;
    webtest::record("arm", handle);
    return handle;
  }
  unsigned web_arm_frame(WebWakeCallback callback, void *session)
  {
    return web_arm_task(callback, session, loka::web::FRAME, 0);
  }
  void web_cancel(unsigned handle)
  {
    webtest::tasks[handle].cancelled = true;
    webtest::record("cancel", handle);
  }
  uint32_t web_read_native_focus(uint32_t)
  {
    return 0;
  }
  void web_completed()
  {
    ++webtest::completed;
    webtest::record("completed", 0);
  }
}
#include "WebTestHooks.hpp"
#include "WebTestAccess.hpp"
namespace webtest
{
  unsigned routeProbes = 0, commitsRefused = 0, installationsRefused = 0, tails = 0;
  void (*clickProbe)(WebElementContext &) = 0;
  void (*tailProbe)() = 0;
} // namespace webtest
namespace loka
{
  namespace web
  {
    namespace testing
    {
      void routeProbe()
      {
        ++webtest::routeProbes;
      }
      bool refuseCommit()
      {
        if (!webtest::commitsRefused)
          return false;
        --webtest::commitsRefused;
        return true;
      }
      bool refuseInstallation()
      {
        if (!webtest::installationsRefused)
          return false;
        --webtest::installationsRefused;
        return true;
      }
      void beforeClick(WebElementContext &ctx)
      {
        if (webtest::clickProbe)
          webtest::clickProbe(ctx);
      }
      void tailCompleted()
      {
        ++webtest::tails;
        if (webtest::tailProbe)
          webtest::tailProbe();
      }
    } // namespace testing
  } // namespace web
} // namespace loka
// Limited evidence: host validates headers and fingerprint, not browser decoding or typography.
extern "C" bool web_text_admit(unsigned long fingerprint, unsigned count)
{
  if (webtest::failTextAdmit)
  {
    --webtest::failTextAdmit;
    return false;
  }
  return fingerprint == kWebTextWireFingerprint && count == WEB_TEXT_FIELD_COUNT;
}
extern "C" bool web_set_text_style(uint32_t token, const int *values, unsigned count)
{
  if (webtest::failTextStyle)
  {
    --webtest::failTextStyle;
    return false;
  }
  if (count != WEB_TEXT_FIELD_COUNT || values[WEB_TEXT_FIELD_VERSION] != kWebTextDescriptorVersion
      || values[WEB_TEXT_FIELD_WORD_COUNT] != WEB_TEXT_FIELD_COUNT)
    return false;
  std::ostringstream args;
  args << "=";
  for (unsigned i = 0; i < count; ++i)
  {
    if (i)
      args << ",";
    args << values[i];
  }
  webtest::record("style", token, args.str());
  return true;
}

namespace webtest
{
  unsigned measureCalls = 0, metricCalls = 0, measureBytes = 0, textBuilds = 0, textScans = 0;
  int failMeasure = 0, measureStatus = WEB_MEASURE_REFUSED, measuredWidth = -1;
  int failMetrics = 0, failAdvance = 0, metricAscent = 10, metricDescent = 3;
  std::vector<std::vector<int> > metricDescriptors, advanceDescriptors;
  std::vector<std::string> measurementOrder, measuredText;
  bool ScheduledFailure(int &count)
  {
    return count < 0 || (count > 0 && --count == 0);
  }
  int MeasureStatus()
  {
    if (failMeasure < 0 || (failMeasure > 0 && --failMeasure == 0))
      return measureStatus;
    return WEB_MEASURE_OK;
  }
} // namespace webtest
extern "C" int web_measure(const int *descriptor, const char *text, unsigned len, int *out)
{
  webtest::advanceDescriptors.push_back(std::vector<int>(descriptor, descriptor + 9));
  webtest::measurementOrder.push_back("advance");
  webtest::measuredText.push_back(std::string(text, len));
  ++webtest::measureCalls;
  webtest::measureBytes += len;
  const int status =
      webtest::ScheduledFailure(webtest::failAdvance) ? webtest::measureStatus : webtest::MeasureStatus();
  if (status == WEB_MEASURE_OK)
    *out = webtest::measuredWidth >= 0 ? webtest::measuredWidth : static_cast<int>(len * 7);
  return status;
}
extern "C" int web_metrics(const int *descriptor, int *ascent, int *descent)
{
  webtest::metricDescriptors.push_back(std::vector<int>(descriptor, descriptor + 9));
  webtest::measurementOrder.push_back("metrics");
  ++webtest::metricCalls;
  const int status =
      webtest::ScheduledFailure(webtest::failMetrics) ? webtest::measureStatus : webtest::MeasureStatus();
  if (status == WEB_MEASURE_OK)
  {
    *ascent = webtest::metricAscent;
    *descent = webtest::metricDescent;
  }
  return status;
}
extern "C" void web_set_line_height(uint32_t token, int height)
{
  std::ostringstream args;
  args << "=" << height;
  webtest::record("line-height", token, args.str());
}
extern "C" void web_set_block(uint32_t token, int align, int truncation)
{
  std::ostringstream args;
  args << "=" << align << "," << truncation;
  webtest::record("block", token, args.str());
}

void loka::web::testing::textBuilt()
{
  ++webtest::textBuilds;
}
void loka::web::testing::textCompared()
{
  ++webtest::textScans;
}
