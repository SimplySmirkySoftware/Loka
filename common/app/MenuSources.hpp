#ifndef LOKA_APP_MENU_SOURCES_HPP
#define LOKA_APP_MENU_SOURCES_HPP

namespace loka
{
  namespace app
  {
    struct MenuBarDefinition;
    namespace scene { class Scene; }

    /** Borrowed, non-owning completed value for one synchronous menu offer.
        App owns base; Scene owns overlay. Only the offer site derives source
        as overlay ? scene : 0; (overlay == 0) == (source == 0).
        No pointer extends its owner's lifetime. */
    struct MenuSources
    {
      const MenuBarDefinition *base;
      const MenuBarDefinition *overlay;
      const scene::Scene *source;

      bool absent() const
      {
        return this->base == 0 && this->overlay == 0;
      }
      bool operator==(const MenuSources &other) const
      {
        return this->base == other.base && this->overlay == other.overlay &&
               this->source == other.source;
      }
    };
  }
}

#endif // LOKA_APP_MENU_SOURCES_HPP
