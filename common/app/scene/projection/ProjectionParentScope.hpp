#ifndef LOKA_APP_SCENE_PROJECTION_PROJECTION_PARENT_SCOPE_HPP
#define LOKA_APP_SCENE_PROJECTION_PROJECTION_PARENT_SCOPE_HPP

#include <cassert>
#include <climits>

#include "app/scene/Node.hpp"
#include "core/Frame.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** Measurement owned by the frame stack for one viewport admission.
          The ScrollView arm reads it through its frame; unit frames borrow
          the same channel pointer, and the projection root has none. */
      class ProjectionMeasurementChannel
      {
      public:
        ProjectionMeasurementChannel()
            : contentHeight_(0),
              shortRangeRefused_(false)
        {
        }

        /** Extends the explicit int measurement channel without ever
            assigning an out-of-range value to LayoutState. */
        bool tryAccumulateContentHeight(int childStartY, int childEndY)
        {
          if (childEndY < childStartY)
          {
            return false;
          }
          const int remaining = SHRT_MAX - this->contentHeight_;
          if (childStartY > INT_MAX - remaining || childEndY > childStartY + remaining)
          {
            return false;
          }
          this->contentHeight_ += childEndY - childStartY;
          return true;
        }

        int contentHeight() const
        {
          return this->contentHeight_;
        }

        /** Refusal belongs to the viewport traversal, including its units.
            Nested containers read it before narrowing another child result.
            The fact expires when the viewport admission ends. */
        void markShortRangeRefused()
        {
          this->shortRangeRefused_ = true;
        }

        bool hasShortRangeRefusal() const
        {
          return this->shortRangeRefused_;
        }

      private:
        int contentHeight_;
        bool shortRangeRefused_;
      };

      /** One platform-projection parent frame. Native parent identity is
          deliberately opaque at the shared wall; each rail owns the cast. */
      struct ProjectionParentScope
      {
        ProjectionParentScope()
            : nativeParent(0),
              translationX(0),
              translationY(0),
              clipRect(0, 0, 0, 0),
              channel(0)
        {
        }

        ProjectionParentScope(void *parent, int translateX, int translateY, const loka::core::Frame &clip)
            : nativeParent(parent),
              translationX(translateX),
              translationY(translateY),
              clipRect(clip),
              channel(0)
        {
        }

        /** Derives a child-parent frame whose coordinates project as
            content - offset. Returns false instead of overflowing int. */
        bool deriveScrolled(void *parent,
                            int offsetX,
                            int offsetY,
                            const loka::core::Frame &clip,
                            ProjectionParentScope &out) const
        {
          int childTranslationX = 0;
          int childTranslationY = 0;
          if (!trySubtract(this->translationX, offsetX, childTranslationX) ||
              !trySubtract(this->translationY, offsetY, childTranslationY))
          {
            return false;
          }
          out = ProjectionParentScope(parent, childTranslationX, childTranslationY, clip);
          out.channel = this->channel;
          return true;
        }

        /** Applies this frame's translation at the projection leaf. */
        bool project(const LayoutState &content, LayoutState &projected) const
        {
          int projectedX = 0;
          int projectedY = 0;
          if (!tryAdd(content.x, this->translationX, projectedX) ||
              !tryAdd(content.y, this->translationY, projectedY) ||
              projectedX < SHRT_MIN || projectedX > SHRT_MAX ||
              projectedY < SHRT_MIN || projectedY > SHRT_MAX)
          {
            return false;
          }
          projected = content;
          projected.x = static_cast<short>(projectedX);
          projected.y = static_cast<short>(projectedY);
          return true;
        }

        /** Restores a projected leaf result to content coordinates. */
        bool restoreContentY(int projectedY, int &contentY) const
        {
          return trySubtract(projectedY, this->translationY, contentY);
        }

        bool tryAccumulateContentHeight(int childStartY, int childEndY)
        {
          return this->channel && this->channel->tryAccumulateContentHeight(childStartY, childEndY);
        }
        int contentHeight() const
        {
          return this->channel ? this->channel->contentHeight() : 0;
        }
        void markShortRangeRefused()
        {
          if (this->channel)
            this->channel->markShortRangeRefused();
        }
        bool hasShortRangeRefusal() const
        {
          return this->channel && this->channel->hasShortRangeRefusal();
        }

        void *nativeParent;
        int translationX;
        int translationY;
        loka::core::Frame clipRect;
        ProjectionMeasurementChannel *channel;

      private:
        static bool tryAdd(int lhs, int rhs, int &out)
        {
          if ((rhs > 0 && lhs > INT_MAX - rhs) ||
              (rhs < 0 && lhs < INT_MIN - rhs))
          {
            return false;
          }
          out = lhs + rhs;
          return true;
        }

        static bool trySubtract(int lhs, int rhs, int &out)
        {
          if ((rhs > 0 && lhs < INT_MIN + rhs) ||
              (rhs < 0 && lhs > INT_MAX + rhs))
          {
            return false;
          }
          out = lhs - rhs;
          return true;
        }
      };

      /** Allocation-free LIFO stack. V1 admits root -> viewport -> unit or
          root -> unit; a viewport is admitted only directly under the root.
          Admission is always-on; no new walk, one borrowed channel pointer per frame
          and one small viewport channel value on the stack. */
      class ProjectionParentScopeStack
      {
      public:
        enum
        {
          MAX_ACTIVE_SCOPES = 2
        };

        explicit ProjectionParentScopeStack(void *rootParent = 0)
            : activeDepth_(0)
        {
          this->frames_[0] = ProjectionParentScope(
              rootParent, 0, 0, loka::core::Frame(0, 0, 0, 0));
        }

        ~ProjectionParentScopeStack()
        {
          assert(this->activeDepth_ == 0 &&
                 "projection-parent scopes must be balanced before teardown");
        }

        bool resetRoot(void *rootParent, const loka::core::Frame &clip)
        {
          if (this->activeDepth_ != 0)
          {
            assert(false && "the projection root cannot change inside a child scope");
            return false;
          }
          this->frames_[0] = ProjectionParentScope(rootParent, 0, 0, clip);
          return true;
        }

        bool pushViewport(const ProjectionParentScope &scope)
        {
          if (this->activeDepth_ != 0)
            return false;
          this->viewportChannel_ = ProjectionMeasurementChannel();
          this->frames_[++this->activeDepth_] = scope;
          this->current().channel = &this->viewportChannel_;
          return true;
        }

        /** Equal adjacent channels identify a unit only for the closed set
            {root, viewport, unit}. A future channel-inheriting frame kind
            would also read as a unit and must revisit this rule. */
        bool insideUnit() const
        {
          return this->activeDepth_ && this->current().channel == this->frames_[this->activeDepth_ - 1].channel;
        }

        bool pushUnit(const ProjectionParentScope &scope)
        {
          if (this->activeDepth_ >= MAX_ACTIVE_SCOPES || this->insideUnit())
            return false;
          ProjectionParentScope child = scope;
          child.channel = this->current().channel;
          this->frames_[++this->activeDepth_] = child;
          return true;
        }

        bool pop()
        {
          if (this->activeDepth_ == 0)
          {
            assert(false && "projection-parent scope pop must be LIFO");
            return false;
          }
          this->current().channel = 0;
          --this->activeDepth_;
          return true;
        }

        ProjectionParentScope &current()
        {
          return this->frames_[this->activeDepth_];
        }

        const ProjectionParentScope &current() const
        {
          return this->frames_[this->activeDepth_];
        }

        unsigned activeDepth() const
        {
          return this->activeDepth_;
        }

      private:
        ProjectionParentScope frames_[MAX_ACTIVE_SCOPES + 1];
        unsigned activeDepth_;
        ProjectionMeasurementChannel viewportChannel_;

        ProjectionParentScopeStack(const ProjectionParentScopeStack &);
        ProjectionParentScopeStack &operator=(const ProjectionParentScopeStack &);
      };

      /** Pairs one successful push with its pop on every return path. */
      class ProjectionParentScopeGuard
      {
      protected:
        explicit ProjectionParentScopeGuard(ProjectionParentScopeStack *stack)
            : stack_(stack)
        {
        }

      public:
        ~ProjectionParentScopeGuard()
        {
          if (this->stack_)
          {
            const bool popped = this->stack_->pop();
            (void)popped;
            assert(popped && "a projection-parent guard must pop its own frame");
          }
        }

        bool isActive() const
        {
          return this->stack_ != 0;
        }

      private:
        ProjectionParentScopeStack *stack_;

        ProjectionParentScopeGuard(const ProjectionParentScopeGuard &);
        ProjectionParentScopeGuard &operator=(const ProjectionParentScopeGuard &);
      };
      /** Owns one admitted viewport frame and its balanced pop. */
      class ProjectionViewportScopeGuard : public ProjectionParentScopeGuard
      {
      public:
        ProjectionViewportScopeGuard(ProjectionParentScopeStack &stack, const ProjectionParentScope &scope)
            : ProjectionParentScopeGuard(stack.pushViewport(scope) ? &stack : 0)
        {
        }
      };

      /** Owns one admitted unit frame; the stack supplies its inherited channel. */
      class ProjectionUnitScopeGuard : public ProjectionParentScopeGuard
      {
      public:
        ProjectionUnitScopeGuard(ProjectionParentScopeStack &stack, const ProjectionParentScope &scope)
            : ProjectionParentScopeGuard(stack.pushUnit(scope) ? &stack : 0)
        {
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_APP_SCENE_PROJECTION_PROJECTION_PARENT_SCOPE_HPP
