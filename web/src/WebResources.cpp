#include "WebResources.hpp"
#include "WebTestHooks.hpp"
#include <cassert>
#include <new>
WebInputRoutes::~WebInputRoutes()
{
  assert(!this->live_);
}
WebResource *WebInputRoutes::reserve(loka::web::Token token, loka::web::Kind kind)
{
  if (!token)
    return 0;
  WebResource **seat = &this->live_;
  unsigned bit = 0;
  while (*seat)
  {
    if ((*seat)->token == token)
      return 0;
    seat = (token & (uint32_t(1) << bit++)) ? &(*seat)->right : &(*seat)->left;
  }
  *seat = new (std::nothrow) WebResource(token, kind);
  return *seat;
}
WebResource *WebInputRoutes::find(loka::web::Token token) const
{
  WebResource *row = this->live_;
  unsigned bit = 0;
  while (row && row->token != token)
  {
#ifdef TEST_BUILD
    loka::web::testing::routeProbe();
#endif
    if (bit == 32)
      return 0;
    row = (token & (uint32_t(1) << bit++)) ? row->right : row->left;
  }
  return row;
}
WebResource *WebInputRoutes::extract(loka::web::Token token)
{
  WebResource **seat = &this->live_;
  unsigned bit = 0;
  while (*seat && (*seat)->token != token)
  {
    if (bit == 32)
      return 0;
    seat = (token & (uint32_t(1) << bit++)) ? &(*seat)->right : &(*seat)->left;
  }
  WebResource *row = *seat;
  if (!row)
    return 0;
  WebResource **leaf = seat;
  while ((*leaf)->left || (*leaf)->right)
    leaf = (*leaf)->left ? &(*leaf)->left : &(*leaf)->right;
  if (leaf == seat)
    *seat = 0;
  else
  {
    WebResource *replacement = *leaf;
    *leaf = 0;
    replacement->left = row->left;
    replacement->right = row->right;
    *seat = replacement;
  }
  row->left = row->right = 0;
  row->leaf = 0;
  row->root = 0;
  return row;
}
WebElement::~WebElement()
{
  assert(!this->token_);
}
bool WebElement::create(
    WebInputRoutes &routes, WebTokens &tokens, loka::web::Kind kind, loka::web::Token parent, int &width, int &height)
{
  assert(!this->token_);
  WebResource *row = routes.reserve(tokens.take(), kind);
  if (!row)
    return false;
  if (!web_create(kind, row->token, parent, &width, &height))
  {
    web_release(row->token);
    delete routes.extract(row->token);
    return false;
  }
#ifdef TEST_BUILD
  if (loka::web::testing::refuseCommit())
  {
    web_release(row->token);
    delete routes.extract(row->token);
    return false;
  }
#endif
  this->token_ = row->token;
  return true;
}
WebResource *WebElement::revoke(WebInputRoutes &routes)
{
  if (!this->token_)
    return 0;
  WebResource *row = routes.extract(this->token_);
  web_set_hidden(this->token_, true);
  web_revoke(this->token_);
  this->token_ = 0;
  return row;
}
