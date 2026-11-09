#ifndef LOKA_WEB_RESOURCES_HPP
#define LOKA_WEB_RESOURCES_HPP
#include "WebBoundary.hpp"

class WebWindow;
class WebElementContext;
class WebResourceContext;
class WebScrollContext;
class WebScenePlatformController;
namespace loka
{
  namespace web
  {
    namespace testing
    {
      struct Access;
    }
  } // namespace web
} // namespace loka
/** Finite module identity space. Cost: creation once per attempted resource,
    O(1), only allocator state; exhaustion is permanent until a new module. */
class WebTokens
{
public:
  WebTokens()
      : next_(1)
  {
  }
  /** Cost: each creation attempt once; O(1) allocator state, exhaustion never wraps. */
  loka::web::Token take()
  {
    const loka::web::Token value = this->next_;
    if (value)
      this->next_ = value == UINT32_MAX ? 0 : value + 1;
    return value;
  }

private:
  friend struct loka::web::testing::Access;
  loka::web::Token next_;
};
/** Stable resource record: live route, then one controller's retirement row.
    Receivers are cleared before retirement; linkage transfer never allocates. */
struct WebResource
{
  WebResource(loka::web::Token value, loka::web::Kind type)
      : token(value),
        kind(type),
        receiver(0),
        root(0),
        next(0),
        left(0),
        right(0)
  {
  }
  const loka::web::Token token;
  const loka::web::Kind kind;
  WebResourceContext *receiver;
  /** Cost: receiver admission/dispatch once each; checked kind conversion, O(1). */
  WebElementContext *leaf() const;
  /** Cost: scroll admission/dispatch once each; checked kind conversion, O(1). */
  WebScrollContext *scroll() const;
  WebWindow *root;
  WebResource *next;
  WebResource *left;
  WebResource *right;
};
/** Owns the live index; records move to their controller at extraction.
    Cost: ingress/creation/extraction once per event/transition, at most 33 record probes over this store's own rows;
   never walks scenes or windows. */
class WebInputRoutes
{
public:
  WebInputRoutes()
      : live_(0)
  {
  }
  ~WebInputRoutes();
  /** Cost: shutdown once after owner teardown; O(1) own index emptiness. */
  bool empty() const
  {
    return this->live_ == 0;
  }
  /** Cost: creation once; at most 33 own index probes and one nullable record allocation. */
  WebResource *reserve(loka::web::Token token, loka::web::Kind kind);
  /** Cost: ingress or resource procedure once; at most 33 own index probes. */
  WebResource *find(loka::web::Token token) const;
  /** Cost: revoke/rollback once; at most two 33-step own-index paths, no allocation. */
  WebResource *extract(loka::web::Token token);

  /** Cost: gutter change once; visits this index's live rows without mutation. */
  template <class Visitor> void forEachRoot(Visitor &visitor) const
  {
    visitRoots(this->live_, visitor);
  }

private:
  template <class Visitor> static void visitRoots(WebResource *row, Visitor &visitor)
  {
    if (!row)
      return;
    if (row->root)
      visitor(*row->root);
    visitRoots(row->left, visitor);
    visitRoots(row->right, visitor);
  }
  friend struct loka::web::testing::Access;
  WebResource *live_;
  WebInputRoutes(const WebInputRoutes &);
  WebInputRoutes &operator=(const WebInputRoutes &);
};
/** Exclusive native obligation. Cost: owner transition once, one resource;
    rollback releases inline only before commit; terminal transfer is deferred. */
class WebElement
{
public:
  WebElement()
      : token_(0)
  {
  }
  ~WebElement();
  /** Cost: owner construction once; one record and adapter entry, same records on rollback. */
  bool create(WebInputRoutes &routes,
              WebTokens &tokens,
              loka::web::Kind kind,
              loka::web::Token parent,
              int &width,
              int &height);
  loka::web::Token token() const
  {
    return this->token_;
  }
  /** Cost: terminal delivery once; extract one own record, hide and revoke one adapter entry. */
  WebResource *revoke(WebInputRoutes &routes);

private:
  loka::web::Token token_;
  WebElement(const WebElement &);
  WebElement &operator=(const WebElement &);
};
#endif
