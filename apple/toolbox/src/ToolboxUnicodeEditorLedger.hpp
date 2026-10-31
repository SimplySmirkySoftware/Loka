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
  /** Owns an uncommitted wrapper. A successful install must immediately release
      into link; a refused install leaves no native object in this owner. */
  class Prepared
  {
  public:
    explicit Prepared(loka::app::scene::NodeContext *context);
    ~Prepared();
    ToolboxUnicodeEditorResource *resource() const
    {
      return this->resource_;
    }
    ToolboxUnicodeEditorResource *release();

  private:
    Prepared(const Prepared &);
    Prepared &operator=(const Prepared &);
    ToolboxUnicodeEditorResource *resource_;
  };
  ToolboxUnicodeEditorLedger();
  ~ToolboxUnicodeEditorLedger();
  /** Install into a borrowed window using port-local logical frame edges.
      Borrows prepared storage; failure leaves it empty. Does not commit. */
  bool
  install(ToolboxUnicodeEditorResource *, ToolboxWindow *window, const loka::core::Frame &frame, unsigned long options);
  /** Commit ownership without allocation. */
  void link(ToolboxUnicodeEditorResource *);
  ToolboxUnicodeEditorResource *find(loka::app::scene::NodeContext *);
  ToolboxUnicodeEditorResource *take(loka::app::scene::NodeContext *);
  void retire(ToolboxUnicodeEditorResource *);
  bool hasPending() const;
  void drain();
  /** Draw under the caller's established port and clip, without an update bracket. */
  void draw(ToolboxUnicodeEditorResource *);
  /** Resolved live resource only; native focus never calls back into Loka. */
  void focus(ToolboxUnicodeEditorResource *, bool);

private:
  ToolboxUnicodeEditorLedger(const ToolboxUnicodeEditorLedger &);
  ToolboxUnicodeEditorLedger &operator=(const ToolboxUnicodeEditorLedger &);
  ToolboxUnicodeEditorResource *live_;
  ToolboxUnicodeEditorResource *retired_;
};
#endif
