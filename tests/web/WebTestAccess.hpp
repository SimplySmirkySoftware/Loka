#ifndef LOKA_WEB_TEST_ACCESS_HPP
#define LOKA_WEB_TEST_ACCESS_HPP
#include "WebSession.hpp"
#include "WebPlatformContext.hpp"
#include "WebWindow.hpp"
#include "WebTestHooks.hpp"
namespace loka
{
  namespace web
  {
    namespace testing
    {
      struct Access
      {
        /** Cost: fixture factory once per module; borrows one existing reference. */
        static WebSession &session(WebPlatformContext &platform)
        {
          return platform.session_;
        }
#ifdef TEST_BUILD
        static int scrollVisit(WebResourceContext &context, const loka::app::scene::LayoutState &seat)
        {
          return context.controller().layoutNode(context.owner(), seat);
        }
        /** Cost: Cell wall pin once per offer; uses the controller's existing viewport scope. */
        static bool cellViewportVisit(WebElementContext &context, loka::app::scene::LayoutState seat)
        {
          WebScenePlatformController &controller = context.controller();
          loka::app::scene::ProjectionParentScope scope;
          if (!controller.projectionParents_.current().deriveScrolled(
                  reinterpret_cast<void *>(static_cast<uintptr_t>(context.token())),
                  0,
                  0,
                  loka::core::Frame(0, 0, 100, 100),
                  scope))
            return false;
          loka::app::scene::ProjectionViewportScopeGuard guard(controller.projectionParents_, scope);
          context.layout(&controller, seat);
          return controller.projectionParents_.current().hasShortRangeRefusal();
        }
        /** Deliberately slow alternative used only by mutation 4. */
        static bool scanNode(loka::app::scene::Node *node, Token token)
        {
          if (!node)
            return false;
          routeProbe();
          if ((node->asTextNode() || node->asButtonNode() || node->asCellNode() || node->asScrollViewNode())
              && node->getContext() && static_cast<WebResourceContext *>(node->getContext())->element_.token() == token)
            return true;
          loka::app::scene::INestable *nestable = node->asNestable();
          if (nestable)
            for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
              if (scanNode(child, token))
                return true;
          return false;
        }
        static WebResource *scanRoots(WebSession &session, WebResource *row, Token token)
        {
          if (!row)
            return 0;
          if (row->root && row->root->controller() && scanNode(row->root->controller()->tree_, token))
            return session.routes_.find(token);
          WebResource *left = scanRoots(session, row->left, token);
          return left ? left : scanRoots(session, row->right, token);
        }
        static WebResource *sceneWalk(WebSession &session, Token token)
        {
          return scanRoots(session, session.routes_.live_, token);
        }

#endif
        static void close(WebSession &session, Window *window)
        {
          session.app_->requestWindowClose(window);
        }
        static void nextToken(WebSession &session, Token token)
        {
          session.tokens_.next_ = token;
        }
        static bool routesEmpty(const WebSession &session)
        {
          return !session.routes_.live_;
        }
        static bool disposed(const WebSession &session)
        {
          return session.lifecycle_ == WebSession::DISPOSED;
        }
        static bool regime(const WebSession &session)
        {
          return session.regime_ != 0;
        }
      };
    } // namespace testing
  } // namespace web
} // namespace loka
namespace webtest
{
  extern unsigned routeProbes, commitsRefused, installationsRefused, tails;
  extern void (*clickProbe)(WebElementContext &);
  extern void (*tailProbe)();
} // namespace webtest
#endif
