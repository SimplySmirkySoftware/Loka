#ifndef LOKA_CORE2_SCENE_PROJECTION_PLATFORM_LAYOUT_HANDLER_HPP
#define LOKA_CORE2_SCENE_PROJECTION_PLATFORM_LAYOUT_HANDLER_HPP

#include "app/scene/Node.hpp"
#include "app/layout/NaturalSizeSource.hpp"
#include "app/layout/StackSpans.hpp"
#include <climits>

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class IPlatformLayoutTraversal : public loka::app::layout::INaturalSizeSource
      {
      public:
        virtual ~IPlatformLayoutTraversal() {}
        /** Rails without natural-width support retain shared Row seats. */
        virtual bool queryNaturalWidth(Node *, short &) const { return false; }
        virtual int layoutChild(Node *child, const LayoutState &state) = 0;
        /** A handler asks before narrowing an int result Y; true means the
            result is refused and must not be committed. */
        virtual bool refuseLayoutResultY(int y) { return y < SHRT_MIN || y > SHRT_MAX; }
        virtual void setLayoutResultY(short y) = 0;
        virtual short layoutResultY() const = 0;
      };

      class IPlatformLayoutHandler
      {
      public:
        virtual ~IPlatformLayoutHandler() {}

        virtual const void *nodeTypeKey() const = 0;
        /** Optional Column-only range and pass-local collector. Null range means
            the full pass. The caller owns spans and must invalidate it before
            changes to constraints, structure, props or attachment; range reuse
            requires unchanged placement inputs. Descendant calls do not inherit
            these arguments. No rail selects a band until it owns that contract. */
        virtual int layoutNode(Node *node, const LayoutState &state, IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0) = 0;
      };

      class PlatformLayoutHandlerRegistry
      {
      public:
        PlatformLayoutHandlerRegistry()
            : head_(0)
        {
        }
        ~PlatformLayoutHandlerRegistry()
        {
          HandlerEntry *entry = this->head_;
          while (entry)
          {
            HandlerEntry *next = entry->next_;
            delete entry->handler_;
            delete entry;
            entry = next;
          }
          this->head_ = 0;
        }

        // Owning registry: built-in layout handlers are registered as heap
        // objects, and the registry takes responsibility for deleting them on
        // destruction or replacement. Callers must not register stack/static
        // handlers here.
        bool registerHandler(IPlatformLayoutHandler *handler)
        {
          if (!handler)
          {
            return false;
          }
          if (!handler->nodeTypeKey())
          {
            delete handler;
            return false;
          }

          HandlerEntry *existing = this->head_;
          while (existing)
          {
            if (existing->nodeTypeKey_ == handler->nodeTypeKey())
            {
              if (existing->handler_ != handler)
              {
                delete existing->handler_;
              }
              existing->handler_ = handler;
              return true;
            }
            existing = existing->next_;
          }

          HandlerEntry *entry = new HandlerEntry(handler->nodeTypeKey(), handler);
          if (!entry)
          {
            delete handler;
            return false;
          }
          entry->next_ = this->head_;
          this->head_ = entry;
          return true;
        }

        IPlatformLayoutHandler *find(const Node *node) const
        {
          if (!node)
          {
            return 0;
          }
          const void *typeKey = node->nodeTypeKey();
          if (!typeKey)
          {
            return 0;
          }

          HandlerEntry *entry = this->head_;
          while (entry)
          {
            if (entry->nodeTypeKey_ == typeKey)
            {
              return entry->handler_;
            }
            entry = entry->next_;
          }
          return 0;
        }

      private:
        struct HandlerEntry
        {
          HandlerEntry(const void *nodeTypeKey, IPlatformLayoutHandler *handler)
              : nodeTypeKey_(nodeTypeKey),
                handler_(handler),
                next_(0)
          {
          }

          const void *nodeTypeKey_;
          IPlatformLayoutHandler *handler_;
          HandlerEntry *next_;
        };

        HandlerEntry *head_;

        PlatformLayoutHandlerRegistry(const PlatformLayoutHandlerRegistry &);
        PlatformLayoutHandlerRegistry &operator=(const PlatformLayoutHandlerRegistry &);
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_PROJECTION_PLATFORM_LAYOUT_HANDLER_HPP
