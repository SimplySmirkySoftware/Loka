#ifndef LOKA_TOOLBOX_PROJECTED_NODE_CONTEXT_HPP
#define LOKA_TOOLBOX_PROJECTED_NODE_CONTEXT_HPP

#include "app/scene/projection/NativeNodeContext.hpp"
#include "context/ToolboxLeafPlacement.hpp"

class ToolboxScenePlatformController;

/** Base for projected Toolbox contexts. boundary_ is the non-owning
    "which window/root am I in" tag used when recording hits and text runs
    into the controller ledgers. The wall
    (ToolboxScenePlatformController::prepareProjectedLayout) re-applies it on
    every projection sweep, so cells never touch it.
    Successful leaf layout compares the still-old fields, writes its own fields,
    then completes placement. Admission requires both the classifier and validity:
    a kind that never completes stays refused. ClipBox uses the same validity value. */
class ToolboxProjectedNodeContext : public loka::app::scene::NativeNodeContext,
                                    public loka::app::scene::IBoundaryTaggedContext
{
public:
  explicit ToolboxProjectedNodeContext(ToolboxScenePlatformController *controller)
      : loka::app::scene::NativeNodeContext(),
        boundary_(0),
        controller_(controller)
  {
  }

  void readLifecycleFactOnAttach()
  {
    // No per-context native presentation on Toolbox; the ledgers repaint from
    // projection sweeps — the method exists so the shared ensure ritual stays
    // uniform.
  }

  virtual loka::app::scene::IBoundaryTaggedContext *asBoundaryTagged()
  {
    return this;
  }

  virtual void setBoundary(loka::app::scene::BoundaryNode *boundary)
  {
    this->boundary_ = boundary;
  }

  virtual loka::app::scene::BoundaryNode *boundary() const
  {
    return this->boundary_;
  }

  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

protected:
  /** Once per successful leaf layout visit; O(1), reads only this context's geometry. */
  void comparePlacement(const ToolboxLeafGeometry &previous, const ToolboxLeafGeometry &next);
  void completePlacement()
  {
    this->validity_.complete();
  }
  /** Placement-changing failure or non-attached fact; O(1), no rows. */
  void invalidatePlacement()
  {
    this->validity_.invalidate();
  }
  virtual void retireNativeProjection() {}
  ToolboxScenePlatformController *controller() const
  {
    return this->controller_;
  }

  loka::app::scene::BoundaryNode *boundary_;

private:
  ToolboxScenePlatformController *controller_;
  ToolboxPlacementValidity validity_;
};

#endif // LOKA_TOOLBOX_PROJECTED_NODE_CONTEXT_HPP
