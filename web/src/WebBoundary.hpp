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
      SCROLL,
      CELL,
      POPUP,
      RECT_SURFACE,
      ATTRIBUTED_TEXT,
      KIND_COUNT
    };
    enum ScrollCommit
    {
      APPLIED,
      BOXLESS,
      PROTOCOL
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
  /** Cost: bootstrap once after admissions; O(1) module option read, no resource rows. */
  int web_skin();
  /** Cost: session start once before allocation; probes the private service's fixed Env table. */
  bool web_text_admit(unsigned long fingerprint, unsigned count);
  int web_create(int kind, uint32_t token, uint32_t parent, int *width, int *height);
  /** Cost: successful layout/request once; one row, box check and native read-back may flush layout. */
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
                        int *appliedY);
  /** Cost: bootstrap and accepted resize once; one temporary probe, synchronous layout. */
  int web_scrollbar_geometry(int *vertical, int *horizontal);
  void web_set_rect(uint32_t token, int x, int y, int width, int height);
  /** Cost: projection once per visit (popup layout, text render), writes one owned element. */
  void web_set_projected(uint32_t token, int projected);
  /** Cost: popup render once; adjusts only its own select's option rows. */
  void web_set_option_count(uint32_t token, int count);
  /** Cost: popup render once per valid item; writes one owned option row. */
  void web_set_option(uint32_t token, int index, const char *bytes, unsigned length);
  /** Cost: popup render once per refused item; clears one owned option row. */
  void web_clear_option(uint32_t token, int index);
  /** Cost: popup render once after options; writes one owned select row. */
  void web_set_selected(uint32_t token, int controlled, int index);
  void web_set_text(uint32_t token, const char *bytes, unsigned length);
  /** Cost: attributed render once; validates/copies this snapshot and stages its own row fragments. */
  int web_set_runs(uint32_t token, const int *words, unsigned wordCount, const char *bytes, unsigned byteLength);
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
