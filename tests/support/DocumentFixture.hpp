#ifndef LOKA_TESTS_SUPPORT_DOCUMENT_FIXTURE_HPP
#define LOKA_TESTS_SUPPORT_DOCUMENT_FIXTURE_HPP

#include "platform/null/NullScenePlatformController.hpp"
#include "support/PropsReconciliation.hpp"

namespace PropsReconciliationSupport
{
  /** Mounts the borrowed declaration on Null. Cost: one owned definition and scene per fixture. */
  template <class D> struct Fixture
  {
    NullScenePlatformController controller;
    D definition;
    loka::app::scene::Scene scene;
    explicit Fixture(const D &d, bool validProjection = true)
        : definition(d),
          scene((loka::app::scene::Boundary<Tree<D> >(Props<D>(&this->definition))))
    {
      this->scene.mount(&this->controller);
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
      if (validProjection)
        settle(this->scene);
    }
    loka::app::scene::Node *node()
    {
      return root(this->scene)->childrenHead();
    }
  };
} // namespace PropsReconciliationSupport
#endif
