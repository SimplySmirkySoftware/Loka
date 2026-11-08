#ifndef LOKA_WEB_BOUNDARY_HPP
#define LOKA_WEB_BOUNDARY_HPP
#include <stdint.h>
namespace loka
{
  namespace web
  {
    typedef uint32_t Token;
    enum Kind
    {
      ROOT,
      TEXT,
      BUTTON,
      KIND_COUNT
    };
    enum Release
    {
      CONSUMED,
      ABSENT,
      NO_OWNERSHIP
    };
    enum Wake
    {
      FRAME,
      TIMEOUT,
      IMMEDIATE,
      SHUTDOWN,
      WAKE_COUNT
    };
  } // namespace web
} // namespace loka
/** Module-local imports; buffers are borrowed only for the call. Cost: resource
    procedure once per transition, projection once per visited leaf, scheduler
    once per outer entry; each call addresses one adapter-owned row. */
extern "C"
{
  /** Cost: session start once before allocation; probes the private service's fixed Env table. */
  bool web_text_admit(unsigned long fingerprint, unsigned count);
  int web_create(int kind, uint32_t token, uint32_t parent, int *width, int *height);
  void web_set_rect(uint32_t token, int x, int y, int width, int height);
  void web_set_text(uint32_t token, const char *bytes, unsigned length);
  bool web_set_text_style(uint32_t token, const int *descriptor, unsigned count);
  /** Cost: breaker miss or natural width once; measures the borrowed range only. */
  int web_measure(const int *descriptor, const char *ptr, unsigned len, int *out);
  /** Cost: build or natural height once; reads session metrics for one font key. */
  int web_metrics(const int *descriptor, int *ascent, int *descent);
  /** Cost: render once per visit, writes one owned element after its font. */
  void web_set_line_height(uint32_t token, int height);
  /** Cost: render once per visit, writes one owned element's block properties. */
  void web_set_block(uint32_t token, int align, int truncation);
  void web_set_enabled(uint32_t token, int enabled);
  int web_set_hidden(uint32_t token, int hidden);
  void web_revoke(uint32_t token);
  int web_release(uint32_t token);
  typedef void (*WebWakeCallback)(void *, unsigned, int, double);
  unsigned web_arm_frame(WebWakeCallback callback, void *session);
  unsigned web_arm_task(WebWakeCallback callback, void *session, int kind, double ms);
  void web_cancel(unsigned handle);
  uint32_t web_read_native_focus(uint32_t root);
  void web_completed();
}
#endif
