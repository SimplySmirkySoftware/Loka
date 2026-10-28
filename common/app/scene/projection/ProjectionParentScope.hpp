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
      /** Measurement owned by the frame stack for one viewport or root-unit
          admission. A root unit owns its refusals; a unit inside a viewport
          borrows the viewport channel, so the viewport refuses metrics commit.
          The projection root itself has no channel. */
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

        /** Refusal belongs to the channel's admission, including descendants.
            Nested containers read it before narrowing another child result.
            The fact expires when the owning admission ends. */
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
              channel(0),
              kind_(ROOT)
        {
        }

        ProjectionParentScope(void *parent, int translateX, int translateY, const loka::core::Frame &clip)
            : nativeParent(parent),
              translationX(translateX),
              translationY(translateY),
              clipRect(clip),
              channel(0),
              kind_(ROOT)
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
        friend class ProjectionParentScopeStack;
        enum Kind
        {
          ROOT,
          VIEWPORT,
          UNIT,
          CLIP
        };
        Kind kind_;

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

      /** Allocation-free LIFO stack with at most eight active frames, excluding
          ROOT. One VIEWPORT and one UNIT may be active; VIEWPORT cannot enter
          either kind, and UNIT cannot enter UNIT. CLIP inherits its parent's
          channel and refuses at six active frames, reserving
          room for VIEWPORT and UNIT. Translation is inherited unless the caller
          explicitly pushes a rebased clip. Admission scans only this stack's frames. */
      class ProjectionParentScopeStack
      {
      public:
        enum
        {
          MAX_ACTIVE_SCOPES = 8
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
          if (this->insideViewport() || this->insideUnit() || this->activeDepth_ >= MAX_ACTIVE_SCOPES)
            return false;
          this->viewportChannel_ = ProjectionMeasurementChannel();
          this->frames_[++this->activeDepth_] = scope;
          this->current().channel = &this->viewportChannel_;
          this->current().kind_ = ProjectionParentScope::VIEWPORT;
          return true;
        }

        /** Whether the immediate enclosing frame supplies a clip-only boundary. */
        bool currentIsClip() const
        {
          return this->current().kind_ == ProjectionParentScope::CLIP;
        }

        bool insideUnit() const
        {
          return this->containsKind(ProjectionParentScope::UNIT);
        }

        bool insideViewport() const
        {
          return this->containsKind(ProjectionParentScope::VIEWPORT);
        }

        bool pushUnit(const ProjectionParentScope &scope)
        {
          if (this->activeDepth_ >= MAX_ACTIVE_SCOPES || this->insideUnit())
            return false;
          ProjectionParentScope child = scope;
          child.channel = this->current().channel;
          if (!this->current().channel)
          {
            this->rootUnitChannel_ = ProjectionMeasurementChannel();
            child.channel = &this->rootUnitChannel_;
          }
          child.kind_ = ProjectionParentScope::UNIT;
          this->frames_[++this->activeDepth_] = child;
          return true;
        }

        /** Whether a CLIP push fits while reserving two frames for viewport/unit. */
        bool admitsClip() const
        {
          return this->activeDepth_ < MAX_ACTIVE_SCOPES - 2;
        }

        /** Adds a clip without changing its parent's projection or measurement. */
        bool pushClip(const ProjectionParentScope &scope)
        {
          ProjectionParentScope child = scope;
          child.translationX = this->current().translationX;
          child.translationY = this->current().translationY;
          return this->pushClipFrame(child);
        }

        /** Adds a clip for a rail whose clip container has its own client origin.
            The caller derives scope with deriveScrolled from current(); its
            translation, native parent and clip rectangle are kept. Measurement
            and admission are the parent's, as for pushClip. */
        bool pushRebasedClip(const ProjectionParentScope &scope)
        {
          return this->pushClipFrame(scope);
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
        bool pushClipFrame(const ProjectionParentScope &scope)
        {
          if (!this->admitsClip())
            return false;
          ProjectionParentScope child = scope;
          child.channel = this->current().channel;
          child.kind_ = ProjectionParentScope::CLIP;
          this->frames_[++this->activeDepth_] = child;
          return true;
        }

        bool containsKind(ProjectionParentScope::Kind kind) const
        {
          for (unsigned i = 1; i <= this->activeDepth_; ++i)
            if (this->frames_[i].kind_ == kind)
              return true;
          return false;
        }

        ProjectionParentScope frames_[MAX_ACTIVE_SCOPES + 1];
        unsigned activeDepth_;
        ProjectionMeasurementChannel viewportChannel_;
        ProjectionMeasurementChannel rootUnitChannel_;

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

      /** Owns one admitted unit frame; the stack supplies its measurement channel. */
      class ProjectionUnitScopeGuard : public ProjectionParentScopeGuard
      {
      public:
        ProjectionUnitScopeGuard(ProjectionParentScopeStack &stack, const ProjectionParentScope &scope)
            : ProjectionParentScopeGuard(stack.pushUnit(scope) ? &stack : 0)
        {
        }
      };
      /** Owns one admitted clip frame and its balanced pop. */
      class ProjectionClipScopeGuard : public ProjectionParentScopeGuard
      {
      public:
        ProjectionClipScopeGuard(ProjectionParentScopeStack &stack, const ProjectionParentScope &scope)
            : ProjectionParentScopeGuard(stack.pushClip(scope) ? &stack : 0)
        {
        }
      };
      /** Owns one admitted rebased clip frame and its balanced pop. */
      class ProjectionRebasedClipScopeGuard : public ProjectionParentScopeGuard
      {
      public:
        ProjectionRebasedClipScopeGuard(ProjectionParentScopeStack &stack, const ProjectionParentScope &scope)
            : ProjectionParentScopeGuard(stack.pushRebasedClip(scope) ? &stack : 0)
        {
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_APP_SCENE_PROJECTION_PROJECTION_PARENT_SCOPE_HPP
