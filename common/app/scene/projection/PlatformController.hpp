#ifndef LOKA_CORE2_SCENE_PROJECTION_IPLATFORMCONTROLLER_HPP
#define LOKA_CORE2_SCENE_PROJECTION_IPLATFORMCONTROLLER_HPP

#include "app/scene/Node.hpp"
#include "app/text/TextStore.hpp"
#include "app/text/TextViewValues.hpp"
#include "app/text/TextSplice.hpp"
#include "app/text/TextStoreSource.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/boundary/BoundaryApplyInfo.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {

      /** Chunk results distinguish completion, a short output buffer, and refusal. */
      enum TextChunkResult
      {
        TEXT_CHUNK_PROGRESS,
        TEXT_CHUNK_END,
        TEXT_CHUNK_NO_ROOM,
        TEXT_CHUNK_REFUSED_MALFORMED,
        TEXT_CHUNK_REFUSED_RESOURCE,
        TEXT_CHUNK_REFUSED_RANGE,
        TEXT_CHUNK_REFUSED_UNSUPPORTED
      };
      const unsigned long kTextNativeEnd = ~0UL;

      class TextDocumentRegistration;
      class Node;
      class BoundaryNode;
      enum NodeDirtyFlags;
      struct PlatformApplyPlan;

      class IPlatformController;
      class BorrowScope;
      class BorrowPhase;
    }
#ifdef TEST_BUILD
    namespace testing
    {
      scene::BorrowPhase &controllerlessBorrowPhase();
    }
#endif
    namespace scene
    {
      /** Controller-owned borrow phase, independent of Scene run/focus phases.
          This is the controller's native-input borrow interval, distinct from
          core::Operation, the transaction clock. */
      class BorrowPhase
      {
        friend class IPlatformController;
        friend class BorrowScope;
#ifdef TEST_BUILD
        friend BorrowPhase &loka::app::testing::controllerlessBorrowPhase();
#endif
      public:
        bool open() const { return this->open_; }
      private:
        BorrowPhase() : open_(false) {}
        BorrowPhase(const BorrowPhase &);
        BorrowPhase &operator=(const BorrowPhase &);
        bool open_;
      };
      /** Stack borrow interval. Only this writer opens/restores the phase. */
      class BorrowScope
      {
      public:
        explicit BorrowScope(IPlatformController &controller);
#ifdef TEST_BUILD
        explicit BorrowScope(BorrowPhase &phase)
            : phase_(phase), previous_(phase.open_)
        {
          this->phase_.open_ = true;
        }
#endif
        ~BorrowScope() { this->phase_.open_ = this->previous_; }
      private:
        BorrowScope(const BorrowScope &);
        BorrowScope &operator=(const BorrowScope &);
        BorrowPhase &phase_;
        const bool previous_;
      };

      namespace detail
      {
        /** One scope-owning implementation for all typed arities. The temporary
            encloses the whole call expression, including the body's continuation. */
        class InputInvocation : private BorrowScope
        {
        public:
          explicit InputInvocation(loka::app::scene::IPlatformController &controller)
              : BorrowScope(controller) {}

          template<class C, class R>
          R operator()(C &c, R (C::*body)()) const
          { return (c.*body)(); }
          template<class C, class R, class A>
          R operator()(C &c, R (C::*body)(A), A a) const
          { return (c.*body)(a); }
          template<class C, class R, class A, class B>
          R operator()(C &c, R (C::*body)(A, B), A a, B b) const
          { return (c.*body)(a, b); }
          template<class C, class R, class A, class B, class D>
          R operator()(C &c, R (C::*body)(A, B, D), A a, B b, D d) const
          { return (c.*body)(a, b, d); }
        };

      } // namespace detail

      /**
       * Abstract platform controller for projecting scene changes into native UI.
       */
      class IPlatformController
      {
        friend class BorrowScope;
        BorrowPhase borrowPhase_;
        IPlatformController(const IPlatformController &);
        IPlatformController &operator=(const IPlatformController &);
      public:
        IPlatformController() {}
        virtual ~IPlatformController() { assert(!this->borrowPhase_.open()); }
        const BorrowPhase &borrowPhase() const { return this->borrowPhase_; }

        /** Refusal-only: O(1) input restoration and dispatch, no rows walked
            here. Include this attempt's inputs and request an after-flush layout;
            requestRelayout retains the rail's scheduling/bookkeeping costs. */
        void refuseTextMeasurement(Node *node, const LayoutState &state)
        {
          if (!node)
            return;
          node->requeueLayoutInputs(state.inputs);
          this->requestRelayout();
        }

        /** Schedule layout after the current flush; never perform it inline.
            Controllers without layout have no work to schedule. */
        virtual void requestRelayout() {}

        /** The default Node render could not discharge its screen obligation.
            Rails with paint-result accounting report it here; O(1), no rows. */
        virtual void noteMissingRenderContext() {}

        /** False cannot answer; true with null means native focus is absent. */
        virtual bool readNativeFocus(NodeContext *&out)
        {
          (void)out;
          return false;
        }

        /** Attempts native focus without activating the window. Copy native handles
            and the context identity BEFORE any native call. Callbacks may retire
            and free the context: afterwards never touch ctx, only compare identities.
            Completion re-reads native focus; the return is not publication authority. */
        virtual bool applyNativeFocus(NodeContext &ctx)
        {
          (void)ctx;
          return false;
        }

        /** Build completely before returning BORN; old is borrowed and untouched on refusal.
            BORN transfers views without allocation, applies selection, and consumes all
            construction marks. Its unchanged, unmarked reading carries [0, native end].
            The kernel connects fresh and retires old; the rail retires partial failures.
            Called once per declaration and once per taken replacement, walking only
            the new row's input. Rails without a store implementation refuse. */
        virtual TextStoreBirth
        createTextStore(TextStoreSource &, TextStoreRow *, TextStoreRow *&fresh, TextStoreReading &)
        {
          fresh = 0;
          return TEXT_STORE_REFUSED_UNSUPPORTED;
        }
        /** Copy handles and identities before the native call and revalidate the row afterwards.
            Callbacks during the call may only mark the row's change note. */
        virtual void commitMarkedText(TextStoreRow &) {}
        /** Once per view attach, only this row and place. Prepare fallible resources before
            attaching; refusal keeps the store parked. Parked stores still serve app requests.
            Preserve body, selection, undo and marks. Prior IME commit is a separate input operation.
            Never reenter the kernel: notifications only mark. Any synchronous paint sees the
            attached shape. Defaults refuse unsupported rails. Replacement birth must finish
            all fallible work before transferring the view, preserving old on refusal. */
        virtual bool attachTextView(TextStoreRow &, NodeContext &, const TextViewPresentation &)
        {
          return false;
        }
        /** Once per linked detach or row retirement. No allocation, no failure; remove borrowed
            presentation resources and park the body. Preserve body, selection, undo and marks.
            No kernel reentry. Retired native resources are silent, including at drain. */
        virtual void detachTextView(TextStoreRow &) {}
        /** Once after attach or compatible props apply, only this row. No kernel reentry.
            Cosmetic preparation failure uses defaults; behavioral settings must be honored. */
        virtual void applyTextViewPresentation(TextStoreRow &, const TextViewPresentation &) {}
        virtual bool readTextStore(TextStoreRow &, TextStoreReading &)
        {
          return false;
        }
        /** True means an unreconciled body change; the refusing default blocks queries. */
        virtual bool peekTextStore(TextStoreRow &)
        {
          return true;
        }
        /** Copy handles and identities before native calls; do not touch a context afterwards. */
        virtual bool applyTextRequest(TextStoreRow &, unsigned long)
        {
          return false;
        }
        /** Validate ordered, in-body, canonical endpoints and native integer widths in
            every build; invalid ranges refuse RANGE without changes. Complete all fallible
            conversion, representability, overflow and capacity checks before changing
            body or selection. Refusal preserves body, selection, undo and pending marks;
            native veto returns UNAVAILABLE. Rails unable to guarantee this keep the default.
            Replace the selected range and collapse selection to the inserted placement.
            Consume all marks produced inside this door. OK returns an unchanged, unmarked
            reading with range [nativeFrom, nativeFrom + inserted native length], removed
            length nativeTo - nativeFrom, and caret == anchor at the native placement.
            Rails with native undo record one editing step; rails without undo are exempt.
            Called at most once per document completion, walking only this store's body
            and inserted source. Unexpected partial failure asserts in lifecycle audit;
            release returns ALLOCATION with marks intact for kernel reconciliation. */
        virtual TextSpliceResult
        applyTextSplice(TextStoreRow &, unsigned long, unsigned long, TextStoreSource &, TextStoreReading &)
        {
          return TEXT_SPLICE_UNAVAILABLE;
        }
        /** Both native ends must be canonical. Progress emits maximal whole UTF-8
            units, normalizing CR, LF and CRLF to LF. Every other result leaves
            the offset unchanged and written zero. Capacity must be positive. */
        virtual TextChunkResult
        readTextChunk(TextStoreRow &, unsigned long &, unsigned long, char *, unsigned long, unsigned long &written)
        {
          written = 0;
          return TEXT_CHUNK_REFUSED_UNSUPPORTED;
        }

        // Project a changed node tree into native UI.
        virtual void onChange(Node *rootNode, NodeDirtyFlags flags, bool fullRebuild) = 0;

        // Optional boundary-local apply seam. Default is no-op to preserve existing controllers.
        virtual void onBoundaryApply(Node *, BoundaryNode *, const BoundaryLocalApplyInfo &, const PlatformApplyPlan &)
        {
        }

        // Platforms that can fully consume pure paint-only updates through onBoundaryApply()
        // may opt into skipping the legacy global onChange() callback for those cycles.
        virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
        {
          return false;
        }

        // Optional hook to start a new scene apply/update measurement cycle.
        virtual void beginApplyCycle() {}

        // Synchronize pending native UI changes.
        virtual void synchronize() = 0;
        virtual bool hasPendingSync() const = 0;

        /** Destroys native handles queued by terminal context delivery. This
            runs only at the platform safe point, after native callbacks have
            unwound and immediately before the App reclaim boundary. */
        virtual void drainNativeRetirements()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->borrowPhase().open());
#endif
        }

        // Destroy platform-owned UI resources.
        virtual void destroy() = 0;

        /** Synchronously revoke menu commands and value subscriptions before
            Scene root detach. Production rails adopt this door in N2. */
        virtual void releaseMenu() {}

        /** Completes the detach line for every context owned by a retired
            subtree without forcing a full scene rebuild. Terminal fact
            delivery must hide, unbind, remove native event routes, and queue
            native destruction before the context object is deleted here;
            actual native destruction is forbidden on this path. */
        virtual void releaseNodeContexts(Node *node)
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->borrowPhase().open());
#endif
          if (!node)
          {
            return;
          }
          // Parked retained branches (Conditional slots) hand their native
          // pairs over here too — the retire door, not the reclaim drain,
          // is where every context in the subtree ends.
          for (unsigned i = 0; Node *branch = node->retainedLifecycleBranch(i); ++i)
          {
            IPlatformController::releaseNodeContexts(branch);
          }
          INestable *nestable = node->asNestable();
          if (nestable)
          {
            for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
            {
              IPlatformController::releaseNodeContexts(child);
            }
          }
          node->setContext(0);
        }

        // Generic hook for nodes that can begin their own platform projection.
        virtual bool prepareProjectedLayout(Node *, LayoutState &)
        {
          return false;
        }

        // Optional external extension seam for platform-specific/custom node handlers.
        virtual bool registerNodeHandler(IPlatformNodeHandler *)
        {
          return false;
        }

      protected:
        /** Route through the controller's mounted Scene root without a tree walk.
            Null, Toolbox and Win32 share this after-run scheduling mechanism. */
        static void requestSceneRelayout(Node *rootNode);
      };

      inline BorrowScope::BorrowScope(IPlatformController &controller)
          : phase_(controller.borrowPhase_), previous_(this->phase_.open_)
      {
        this->phase_.open_ = true;
      }
    } // namespace scene
#ifdef TEST_BUILD
    namespace testing
    {
      /** Scene-less fixtures only; no controller or Scene consumes this phase. */
      inline scene::BorrowPhase &controllerlessBorrowPhase()
      {
        static scene::BorrowPhase phase;
        return phase;
      }
    }
#endif
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_PROJECTION_IPLATFORMCONTROLLER_HPP
