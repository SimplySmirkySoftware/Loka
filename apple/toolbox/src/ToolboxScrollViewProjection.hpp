// Inline controller bodies shared with the host projection fixture.
bool projectLayoutState(loka::app::scene::LayoutState &state)
{
  const loka::app::scene::ProjectionParentScope &scope = this->projectionParentScopes_.current();
  // This rail has no inner native origin and translates both axes.
  // Both operands are short-bounded, so their sum is safe in int.
  const int projectedX = static_cast<int>(state.x) + scope.translationX;
  const int projectedY = static_cast<int>(state.y) + scope.translationY;
  if (projectedX < SHRT_MIN || projectedX > SHRT_MAX || projectedY < SHRT_MIN || projectedY > SHRT_MAX)
  {
    this->refuseScrollViewShortRange();
    return false;
  }
  state.x = static_cast<short>(projectedX);
  state.y = static_cast<short>(projectedY);
  return true;
}
bool restoreProjectedLayoutState(loka::app::scene::LayoutState &state)
{
  // Both axes use short-bounded operands, so subtraction is safe in int.
  // Refuse at the narrowing edge without the generic int-overflow path.
  const int contentX = static_cast<int>(state.x) - this->projectionParentScopes_.current().translationX;
  const int contentY = static_cast<int>(state.y) - this->projectionParentScopes_.current().translationY;
  if (contentX < SHRT_MIN || contentX > SHRT_MAX || contentY < SHRT_MIN || contentY > SHRT_MAX)
  {
    this->refuseScrollViewShortRange();
    return false;
  }
  state.x = static_cast<short>(contentX);
  state.y = static_cast<short>(contentY);
  return true;
}

void refuseScrollViewShortRange()
{
  if (this->projectionParentScopes_.current().channel)
  {
    this->projectionParentScopes_.current().markShortRangeRefused();
  }
}

bool refuseNarrowingInScrollScope(int resultY)
{
  if (!this->projectionParentScopes_.current().channel)
  {
    return false;
  }
  loka::app::scene::ProjectionParentScope &scope = this->projectionParentScopes_.current();
  if (scope.hasShortRangeRefusal())
  {
    return true;
  }
  if (resultY >= SHRT_MIN && resultY <= SHRT_MAX)
  {
    return false;
  }
  scope.markShortRangeRefused();
  return true;
}
