#include "app/scene/Scene.hpp"
void membershipPin(loka::app::scene::Scene &scene, loka::app::scene::SceneTextDocuments::Visitor &visitor)
{
  (void)visitor;
  scene.textDocuments().visit(visitor);
}
