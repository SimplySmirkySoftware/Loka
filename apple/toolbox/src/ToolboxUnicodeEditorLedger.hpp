#ifndef LOKA_TOOLBOX_UNICODE_EDITOR_LEDGER_HPP
#define LOKA_TOOLBOX_UNICODE_EDITOR_LEDGER_HPP
class ToolboxWindow;
namespace loka
{
  namespace core
  {
    struct Frame;
  }
  namespace app
  {
    namespace scene
    {
      class NodeContext;
    }
  } // namespace app
} // namespace loka
struct ToolboxUnicodeEditorResource;

/** Controller-owned native resources. Detach is synchronous at retire; drain
    reclaims at the controller's existing native retirement clock. No refcon. */
class ToolboxUnicodeEditorLedger
{
public:
  ToolboxUnicodeEditorLedger();
  ~ToolboxUnicodeEditorLedger();
  /** Install into a borrowed window using port-local logical frame edges.
      Page 2b refuses without acquiring any native resource in either flavor. */
  bool
  install(ToolboxUnicodeEditorResource *, ToolboxWindow *window, const loka::core::Frame &frame, unsigned long options);
  void link(ToolboxUnicodeEditorResource *);
  ToolboxUnicodeEditorResource *find(loka::app::scene::NodeContext *);
  ToolboxUnicodeEditorResource *take(loka::app::scene::NodeContext *);
  void retire(ToolboxUnicodeEditorResource *);
  bool hasPending() const;
  void drain();

private:
  ToolboxUnicodeEditorLedger(const ToolboxUnicodeEditorLedger &);
  ToolboxUnicodeEditorLedger &operator=(const ToolboxUnicodeEditorLedger &);
  ToolboxUnicodeEditorResource *live_;
  ToolboxUnicodeEditorResource *retired_;
};
#endif
