#ifndef LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_REQUESTS_HPP
#define LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_REQUESTS_HPP
#include "app/scene/state/Request.hpp"
namespace loka
{
  namespace app
  {
    /** One coalescable destination, in content coordinates; Top exposes the top gap. */
    class ScrollTarget
    {
    public:
      ScrollTarget()
          : kind_(NONE),
            x_(0),
            y_(0)
      {
      }
      static ScrollTarget None()
      {
        return ScrollTarget();
      }
      static ScrollTarget Top()
      {
        return ScrollTarget(TOP, 0, 0);
      }
      static ScrollTarget Origin(int x, int y)
      {
        return ScrollTarget(ORIGIN, x, y);
      }
      bool isNone() const
      {
        return this->kind_ == NONE;
      }
      int x() const
      {
        return this->x_;
      }
      int contentY(int top) const
      {
        return this->kind_ == TOP ? -top : this->y_;
      }
      bool operator==(const ScrollTarget &other) const
      {
        return this->kind_ == other.kind_ && this->x_ == other.x_ && this->y_ == other.y_;
      }
      bool operator!=(const ScrollTarget &other) const
      {
        return !(*this == other);
      }

    private:
      enum Kind
      {
        NONE,
        TOP,
        ORIGIN
      };
      ScrollTarget(Kind kind, int x, int y)
          : kind_(kind),
            x_(x),
            y_(y)
      {
      }
      Kind kind_;
      int x_, y_;
    };
    enum ScrollRequestResult
    {
      SCROLL_OK,
      SCROLL_UNAVAILABLE
    };
    /** Completed projection outcome; Null has no native range to clamp. */
    class ScrollViewVisit
    {
    public:
      enum Axis
      {
        X,
        Y
      };
      static ScrollViewVisit refused()
      {
        return ScrollViewVisit(false, false, false);
      }
      static ScrollViewVisit succeededUnclamped()
      {
        return ScrollViewVisit(true, true, true);
      }
      static ScrollViewVisit succeededWithNativeAxes(bool x, bool y)
      {
        return ScrollViewVisit(true, x, y);
      }
      bool succeeded() const
      {
        return this->succeeded_;
      }
      /** Native presence permits movement; the native setter owns range clamping. */
      bool canMove(Axis axis) const
      {
        return axis == X ? this->movableX_ : this->movableY_;
      }

    private:
      ScrollViewVisit(bool succeeded, bool x, bool y)
          : succeeded_(succeeded),
            movableX_(x),
            movableY_(y)
      {
      }
      bool succeeded_;
      bool movableX_, movableY_;
    };
    namespace scene
    {
      template <> struct RequestTraits<ScrollTarget>
      {
        enum
        {
          coalescable = 1
        };
        typedef ScrollRequestResult Reason;
        static Reason accepted()
        {
          return SCROLL_OK;
        }
        static Reason unavailable()
        {
          return SCROLL_UNAVAILABLE;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
