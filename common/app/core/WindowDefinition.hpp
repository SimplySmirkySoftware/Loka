#ifndef LOKA_WINDOW_DEFINITION_HPP
#define LOKA_WINDOW_DEFINITION_HPP

#include <cassert>
#include <new>
#include "app/core/Window.hpp"
#include "app/PlatformContext.hpp"
#include "app/scene/Scene.hpp"

class PlatformContext;

class WindowDefinitionBase
{
public:
  WindowDefinitionBase()
      : nextInComposition(0)
  {
  }
  virtual ~WindowDefinitionBase() {}
  /**
   * Window definition allocation follows the same no-exception policy as scene
   * definitions: nullable results are reserved for allocation-style failure
   * such as OOM, while contract misuse should assert elsewhere.
   */
  virtual Window *create(PlatformContext *context) const = 0;
  virtual WindowDefinitionBase *clone() const = 0;

  WindowDefinitionBase *nextInComposition;
};

template <class PropsT> struct WindowDefinition : public WindowDefinitionBase
{
#if __cplusplus >= 201103L
  static_assert((typename PropsT::TypeTag *)0 == (typename Window::TypeTag *)0, "window_props_type_mismatch");
#elif defined(LOKA_WINDOWDEF_CHECK_TYPETAG)
  typedef char static_assert_window_props_type_mismatch[ //
      ((typename PropsT::TypeTag *)0 == (typename Window::TypeTag *)0) ? 1 : -1];
#endif
  PropsT props;
  WindowDefinition()
      : props()
  {
  }
  WindowDefinition(const PropsT &p)
      : props(p)
  {
  }
  virtual WindowDefinitionBase *clone() const
  {
    return cloneWindowDefinition(*this);
  }
  virtual Window *create(PlatformContext *context) const
  {
    assert(context && "WindowDefinition::create requires PlatformContext");
    if (this->props.rootDefinitionRefused())
      return 0;
    WindowProps resolved(this->props);
    if (resolved.rootDefinitionRefused())
      return 0;
    loka::app::scene::NodeDefinitionBase *def = resolved.takeRootDefinition();
    if (def)
    {
      loka::app::scene::Scene *createdScene = new (std::nothrow) loka::app::scene::Scene(def);
      if (!createdScene)
      {
        delete def;
        return 0;
      }
      resolved.scene(createdScene);
      if (!resolved.peekInitialScene())
      {
        return 0;
      }
    }
    return context->createWindow(resolved);
  }

protected:
  /** Preserves the concrete definition type and rejects a refused content copy. */
  template <class T> static WindowDefinitionBase *cloneWindowDefinition(const T &source)
  {
    loka::core::OwnedDef<T> copy(new (std::nothrow) T(source));
    return copy.isSet() && !copy->props.rootDefinitionRefused() ? copy.take() : 0;
  }
};

inline WindowDefinition<WindowProps> WindowDef(const WindowProps &props)
{
  return WindowDefinition<WindowProps>(props);
}

#endif // LOKA_WINDOW_DEFINITION_HPP
