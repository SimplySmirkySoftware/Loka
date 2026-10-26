#ifndef LOKA_APP_MENU_HPP
#define LOKA_APP_MENU_HPP

#include "app/EditVerb.hpp"
#include <vector>
#include <cassert>
#include "core/diag/LifecycleAudit.hpp"
#include "core/String.hpp"
#include "core/State.hpp"
#include "dsl/composition/CompositionList.hpp"
#include "app/MenuComposition.hpp"

namespace loka
{
  namespace app
  {
#ifdef TEST_BUILD
    namespace testing
    {
      void failMenuItemDefinitionClones(int count, int successfulClonesBeforeFailure = 0);
      bool shouldCloneMenuItemDefinition();
      void failMenuDefinitionClones(int count);
      bool shouldCloneMenuDefinition();
      void failNextMenuBarDefinitionClone();
      void failMenuBarDefinitionClones(int count);
      void allowMenuBarDefinitionClones();
      bool shouldCloneMenuBarDefinition();
    } // namespace testing
#endif

    struct MenuItemAttr
    {
      MenuItemAttr()
          : hasDisabledValue_(false),
            disabledValue_(false),
            disabledState_(0),
            hasVisibleValue_(false),
            visibleValue_(true),
            visibleState_(0),
            hasCheckedValue_(false),
            checkedValue_(false),
            checkedState_(0)
      {
      }

      MenuItemAttr &disabled(bool value)
      {
        this->hasDisabledValue_ = true;
        this->disabledValue_ = value;
        this->disabledState_ = 0;
        return *this;
      }

      MenuItemAttr &disabled(loka::core::State<bool> *state)
      {
        this->hasDisabledValue_ = false;
        this->disabledState_ = state;
        return *this;
      }

      MenuItemAttr &visible(bool value)
      {
        this->hasVisibleValue_ = true;
        this->visibleValue_ = value;
        this->visibleState_ = 0;
        return *this;
      }

      MenuItemAttr &visible(loka::core::State<bool> *state)
      {
        this->hasVisibleValue_ = false;
        this->visibleState_ = state;
        return *this;
      }

      MenuItemAttr &checked(bool value)
      {
        this->hasCheckedValue_ = true;
        this->checkedValue_ = value;
        this->checkedState_ = 0;
        return *this;
      }

      MenuItemAttr &checked(loka::core::State<bool> *state)
      {
        this->hasCheckedValue_ = false;
        this->checkedState_ = state;
        return *this;
      }

      bool operator==(const MenuItemAttr &other) const
      {
        return this->hasDisabledValue_ == other.hasDisabledValue_ && this->disabledValue_ == other.disabledValue_
               && this->disabledState_ == other.disabledState_ && this->hasVisibleValue_ == other.hasVisibleValue_
               && this->visibleValue_ == other.visibleValue_ && this->visibleState_ == other.visibleState_
               && this->hasCheckedValue_ == other.hasCheckedValue_
               && this->checkedValue_ == other.checkedValue_ && this->checkedState_ == other.checkedState_;
      }

      bool operator<(const MenuItemAttr &other) const
      {
        if (this->hasDisabledValue_ != other.hasDisabledValue_)
          return this->hasDisabledValue_ < other.hasDisabledValue_;
        if (this->disabledValue_ != other.disabledValue_)
          return this->disabledValue_ < other.disabledValue_;
        if (this->disabledState_ != other.disabledState_)
          return this->disabledState_ < other.disabledState_;
        if (this->hasVisibleValue_ != other.hasVisibleValue_)
          return this->hasVisibleValue_ < other.hasVisibleValue_;
        if (this->visibleValue_ != other.visibleValue_)
          return this->visibleValue_ < other.visibleValue_;
        if (this->visibleState_ != other.visibleState_)
          return this->visibleState_ < other.visibleState_;
        if (this->hasCheckedValue_ != other.hasCheckedValue_)
          return this->hasCheckedValue_ < other.hasCheckedValue_;
        if (this->checkedValue_ != other.checkedValue_)
          return this->checkedValue_ < other.checkedValue_;
        return this->checkedState_ < other.checkedState_;
      }

      bool hasDisabledValue_;
      bool disabledValue_;
      loka::core::State<bool> *disabledState_;
      bool hasVisibleValue_;
      bool visibleValue_;
      loka::core::State<bool> *visibleState_;
      bool hasCheckedValue_;
      bool checkedValue_;
      loka::core::State<bool> *checkedState_;
    };

    struct MenuItemDefinitionWithAttr;

    enum MenuActionType
    {
      MENU_ACTION_NONE = 0,
      MENU_ACTION_ABOUT_APP,
      MENU_ACTION_QUIT_APP,
      MENU_ACTION_SHOW_COLOR_PICKER,
      MENU_ACTION_EDIT_UNDO,
      MENU_ACTION_EDIT_CUT,
      MENU_ACTION_EDIT_COPY,
      MENU_ACTION_EDIT_PASTE,
      MENU_ACTION_EDIT_SELECT_ALL,
      MENU_ACTION_COUNT
    };

    /** Single exhaustive correspondence; non-Edit actions leave out unchanged. */
    inline bool menuEditVerb(MenuActionType action, EditVerb &out)
    {
      switch (action)
      {
      case MENU_ACTION_EDIT_UNDO:
        out = EDIT_UNDO;
        return true;
      case MENU_ACTION_EDIT_CUT:
        out = EDIT_CUT;
        return true;
      case MENU_ACTION_EDIT_COPY:
        out = EDIT_COPY;
        return true;
      case MENU_ACTION_EDIT_PASTE:
        out = EDIT_PASTE;
        return true;
      case MENU_ACTION_EDIT_SELECT_ALL:
        out = EDIT_SELECT_ALL;
        return true;
      case MENU_ACTION_NONE:
      case MENU_ACTION_ABOUT_APP:
      case MENU_ACTION_QUIT_APP:
      case MENU_ACTION_SHOW_COLOR_PICKER:
      case MENU_ACTION_COUNT:
        return false;
      }
      return false;
    }

    struct MenuItemDefinition
    {
      MenuItemDefinition()
          : title(),
            titleState(0),
            enabledState(0),
            enabledValue_(true),
            hasEnabledValue_(false),
            onClickState(0),
            action(MENU_ACTION_NONE),
            isSeparator(false),
            children_(),
            hasShortcut(false),
            shortcutKey(0),
            attr_(),
            hasAttr_(false),
            nextInComposition(0)
      {
      }

      explicit MenuItemDefinition(const char *text)
          : title(loka::core::String::Literal(text)),
            titleState(0),
            enabledState(0),
            enabledValue_(true),
            hasEnabledValue_(false),
            onClickState(0),
            action(MENU_ACTION_NONE),
            isSeparator(false),
            children_(),
            hasShortcut(false),
            shortcutKey(0),
            attr_(),
            hasAttr_(false),
            nextInComposition(0)
      {
      }

      explicit MenuItemDefinition(const loka::core::String &text)
          : title(text),
            titleState(0),
            enabledState(0),
            enabledValue_(true),
            hasEnabledValue_(false),
            onClickState(0),
            action(MENU_ACTION_NONE),
            isSeparator(false),
            children_(),
            hasShortcut(false),
            shortcutKey(0),
            attr_(),
            hasAttr_(false),
            nextInComposition(0)
      {
      }

      /** By-value copy cannot report refusal; children remain empty on failure.
          Refusal-capable callers must use clone(). */
      MenuItemDefinition(const MenuItemDefinition &other)
          : title(other.title),
            titleState(other.titleState),
            enabledState(other.enabledState),
            enabledValue_(other.enabledValue_),
            hasEnabledValue_(other.hasEnabledValue_),
            onClickState(other.onClickState),
            action(other.action),
            isSeparator(other.isSeparator),
            children_(),
            hasShortcut(other.hasShortcut),
            shortcutKey(other.shortcutKey),
            attr_(other.attr_),
            hasAttr_(other.hasAttr_),
            nextInComposition(0)
      {
        this->copyFrom(other);
      }

      ~MenuItemDefinition()
      {
        clearChildren();
      }

      /** Preserve the entire destination when a child clone refuses. */
      MenuItemDefinition &operator=(const MenuItemDefinition &other)
      {
        if (this == &other)
          return *this;
        this->copyFrom(other);
        return *this;
      }

    private:
      bool copyFrom(const MenuItemDefinition &other)
      {
        loka::dsl::CompositionList<MenuItemDefinition> candidate;
        if (!candidate.copyFrom(other.children_))
          return false;
        this->title = other.title;
        this->titleState = other.titleState;
        this->enabledState = other.enabledState;
        this->enabledValue_ = other.enabledValue_;
        this->hasEnabledValue_ = other.hasEnabledValue_;
        this->onClickState = other.onClickState;
        this->action = other.action;
        this->isSeparator = other.isSeparator;
        this->hasShortcut = other.hasShortcut;
        this->shortcutKey = other.shortcutKey;
        this->attr_ = other.attr_;
        this->hasAttr_ = other.hasAttr_;
        this->nextInComposition = 0;
        candidate.detachTo(this->children_);
        return true;
      }

    public:
      /** Return a completed owned definition or 0 on clone refusal. */
      MenuItemDefinition *clone() const
      {
#ifdef TEST_BUILD
        if (!testing::shouldCloneMenuItemDefinition())
          return 0;
#endif
        loka::core::OwnedDef<MenuItemDefinition> candidate(new MenuItemDefinition());
        if (!candidate.isSet() || !candidate->copyFrom(*this))
          return 0;
        return candidate.take();
      }

      MenuItemDefinition &text(const loka::core::String &value)
      {
        title = value;
        titleState = 0;
        return *this;
      }

      MenuItemDefinition &text(const char *value)
      {
        title = loka::core::String::Literal(value);
        titleState = 0;
        return *this;
      }

      /** Borrow a live title; a null state uses the declared fallback title. */
      MenuItemDefinition &text(loka::core::State<loka::core::String> *state)
      {
        this->titleState = state;
        return *this;
      }

      MenuItemDefinition &enabled(loka::core::State<bool> *state)
      {
        enabledState = state;
        hasEnabledValue_ = false;
        return *this;
      }

      MenuItemDefinition &enabled(bool value)
      {
        enabledState = 0;
        enabledValue_ = value;
        hasEnabledValue_ = true;
        return *this;
      }

      MenuItemDefinition &onClick(loka::core::EmitterState *emitter)
      {
        onClickState = emitter;
        return *this;
      }

      MenuItemDefinition &actionType(MenuActionType type)
      {
        action = type;
        return *this;
      }

      MenuItemDefinition &separator()
      {
        isSeparator = true;
        return *this;
      }

      MenuItemDefinition &shortcut(char key)
      {
        hasShortcut = true;
        shortcutKey = key;
        return *this;
      }

      MenuItemDefinitionWithAttr attr(const MenuItemAttr &value) const;

      bool isEnabledInitial() const
      {
        if (this->enabledState)
        {
          return this->enabledState->get();
        }
        if (this->hasEnabledValue_)
        {
          return this->enabledValue_;
        }
        if (!this->hasAttr_)
        {
          return true;
        }
        if (this->attr_.disabledState_)
        {
          return !this->attr_.disabledState_->get();
        }
        if (this->attr_.hasDisabledValue_)
        {
          return !this->attr_.disabledValue_;
        }
        return true;
      }

      bool isVisibleInitial() const
      {
        if (!this->hasAttr_)
        {
          return true;
        }
        if (this->attr_.visibleState_)
        {
          return this->attr_.visibleState_->get();
        }
        if (this->attr_.hasVisibleValue_)
        {
          return this->attr_.visibleValue_;
        }
        return true;
      }

      bool isCheckedInitial() const
      {
        if (!this->hasAttr_)
        {
          return false;
        }
        if (this->attr_.checkedState_)
        {
          return this->attr_.checkedState_->get();
        }
        if (this->attr_.hasCheckedValue_)
        {
          return this->attr_.checkedValue_;
        }
        return false;
      }

      loka::core::State<bool> *checkedBindingState() const
      {
        return this->hasAttr_ ? this->attr_.checkedState_ : 0;
      }

      loka::core::State<bool> *enabledBindingState() const
      {
        if (this->enabledState)
        {
          return this->enabledState;
        }
        if (this->hasAttr_ && this->attr_.disabledState_)
        {
          return this->attr_.disabledState_;
        }
        return 0;
      }

      bool enabledBindingInvert() const
      {
        return (!this->enabledState && this->hasAttr_ && this->attr_.disabledState_);
      }

      MenuItemDefinition &operator<<(const MenuItemDefinition &child)
      {
        children_.appendClone(child);
        return *this;
      }

      bool hasChildren() const
      {
        return children_.count() > 0;
      }
      MenuItemDefinition *childrenHead() const
      {
        return children_.head();
      }
      size_t childrenCount() const
      {
        return children_.count();
      }

      void clearChildren()
      {
        children_.clear();
      }

      loka::core::String title;
      loka::core::State<loka::core::String> *titleState;
      loka::core::State<bool> *enabledState;
      bool enabledValue_;
      bool hasEnabledValue_;
      loka::core::EmitterState *onClickState;
      MenuActionType action;
      bool isSeparator;
      loka::dsl::CompositionList<MenuItemDefinition> children_;
      bool hasShortcut;
      char shortcutKey;
      MenuItemAttr attr_;
      bool hasAttr_;
      MenuItemDefinition *nextInComposition;
    };

    struct MenuItemDefinitionWithAttr : public MenuItemDefinition
    {
      MenuItemDefinitionWithAttr()
          : MenuItemDefinition()
      {
      }
      MenuItemDefinitionWithAttr(const MenuItemDefinition &base)
          : MenuItemDefinition(base)
      {
      }

    private:
      MenuItemDefinitionWithAttr attr(const MenuItemAttr &value) const;
    };

    inline MenuItemDefinitionWithAttr MenuItemDefinition::attr(const MenuItemAttr &value) const
    {
      MenuItemDefinition copy(*this);
      assert(!copy.hasAttr_ && "MenuItem.attr() can only be set once per item");
      copy.attr_ = value;
      copy.hasAttr_ = true;
      return MenuItemDefinitionWithAttr(copy);
    }

    struct MenuDefinition
    {
      MenuDefinition()
          : title(),
            isAppMenu(false),
            items_(),
            nextInComposition(0)
      {
      }

      explicit MenuDefinition(const char *text)
          : title(loka::core::String::Literal(text)),
            isAppMenu(false),
            items_(),
            nextInComposition(0)
      {
      }

      explicit MenuDefinition(const loka::core::String &text)
          : title(text),
            isAppMenu(false),
            items_(),
            nextInComposition(0)
      {
      }

      /** By-value copy cannot report refusal; children remain empty on failure.
          Refusal-capable callers must use clone(). */
      MenuDefinition(const MenuDefinition &other)
          : title(other.title),
            isAppMenu(other.isAppMenu),
            items_(),
            nextInComposition(0)
      {
        this->copyFrom(other);
      }

      ~MenuDefinition()
      {
        clearItems();
      }

      /** Preserve the entire destination when a child clone refuses. */
      MenuDefinition &operator=(const MenuDefinition &other)
      {
        if (this == &other)
          return *this;
        this->copyFrom(other);
        return *this;
      }

    private:
      bool copyFrom(const MenuDefinition &other)
      {
        loka::dsl::CompositionList<MenuItemDefinition> candidate;
        if (!candidate.copyFrom(other.items_))
          return false;
        this->title = other.title;
        this->isAppMenu = other.isAppMenu;
        this->nextInComposition = 0;
        candidate.detachTo(this->items_);
        return true;
      }

    public:
      /** Return a completed owned definition or 0 on clone refusal. */
      MenuDefinition *clone() const
      {
#ifdef TEST_BUILD
        if (!testing::shouldCloneMenuDefinition())
          return 0;
#endif
        loka::core::OwnedDef<MenuDefinition> candidate(new MenuDefinition());
        if (!candidate.isSet() || !candidate->copyFrom(*this))
          return 0;
        return candidate.take();
      }

      MenuDefinition &text(const loka::core::String &value)
      {
        title = value;
        return *this;
      }

      MenuDefinition &text(const char *value)
      {
        title = loka::core::String::Literal(value);
        return *this;
      }

      MenuDefinition &appMenu(bool flag)
      {
        isAppMenu = flag;
        return *this;
      }

      MenuDefinition &operator<<(const MenuItemDefinition &item)
      {
        items_.appendClone(item);
        return *this;
      }

      bool hasItems() const
      {
        return items_.count() > 0;
      }
      MenuItemDefinition *itemsHead() const
      {
        return items_.head();
      }
      size_t itemsCount() const
      {
        return items_.count();
      }

      void clearItems()
      {
        items_.clear();
      }

      loka::core::String title;
      bool isAppMenu;
      loka::dsl::CompositionList<MenuItemDefinition> items_;
      MenuDefinition *nextInComposition;
    };

    struct MenuBarDefinition LOKA_AUDITED(MenuBarDefinition)
    {
      MenuBarDefinition()
          : menus_()
      {
      }

      /** By-value copy cannot report refusal; children remain empty on failure.
          Refusal-capable callers must use clone(). */
      MenuBarDefinition(const MenuBarDefinition &other)
          : LOKA_AUDITED_COPY(MenuBarDefinition, other)
            menus_()
      {
        this->copyFrom(other);
      }

      ~MenuBarDefinition()
      {
        clearMenus();
      }

      /** Preserve the entire destination when a child clone refuses. */
      MenuBarDefinition &operator=(const MenuBarDefinition &other)
      {
        if (this == &other)
          return *this;
        this->copyFrom(other);
        return *this;
      }

    private:
      bool copyFrom(const MenuBarDefinition &other)
      {
        return this->menus_.copyFrom(other.menus_);
      }

    public:
      /** Return a completed owned definition or 0 on clone refusal. */
      MenuBarDefinition *clone() const
      {
#ifdef TEST_BUILD
        if (!testing::shouldCloneMenuBarDefinition())
          return 0;
#endif
        loka::core::OwnedDef<MenuBarDefinition> candidate(new MenuBarDefinition());
        if (!candidate.isSet() || !candidate->copyFrom(*this))
          return 0;
        return candidate.take();
      }

      MenuBarDefinition &operator<<(const MenuDefinition &menu)
      {
        menus_.appendClone(menu);
        return *this;
      }

      bool empty() const
      {
        return menus_.count() == 0;
      }
      MenuDefinition *menusHead() const
      {
        return menus_.head();
      }
      size_t menusCount() const
      {
        return menus_.count();
      }
      MenuDefinition *menuAt(size_t index) const
      {
        size_t i = 0;
        MenuDefinition *cur = menus_.head();
        while (cur)
        {
          if (i == index)
          {
            return cur;
          }
          cur = cur->nextInComposition;
          i += 1;
        }
        return 0;
      }

      void clearMenus()
      {
        menus_.clear();
      }

      loka::dsl::CompositionList<MenuDefinition> menus_;
    };

    // MenuComposition is defined in MenuComposition.hpp.

    inline MenuDefinition Menu(const char *title)
    {
      return MenuDefinition(title);
    }

    inline MenuDefinition Menu(const loka::core::String &title)
    {
      return MenuDefinition(title);
    }

    inline MenuDefinition AppMenu()
    {
      MenuDefinition menu("App");
      menu.appMenu(true);
      return menu;
    }

    inline MenuItemDefinition MenuItem(const char *title)
    {
      return MenuItemDefinition(title);
    }

    inline MenuItemDefinition MenuItem(const loka::core::String &title)
    {
      return MenuItemDefinition(title);
    }

    /**
     * Request About in a fresh app container with an empty, childless item.
     * Rails place it and supply "About" on Classic and macOS; Win32 does not
     * show About yet. A declared non-empty title or title State value wins.
     */
    inline MenuDefinition AboutMenu()
    {
      return AppMenu() << MenuItem("").actionType(MENU_ACTION_ABOUT_APP);
    }

    /**
     * Request Quit in a fresh app container with an empty, childless item.
     * Rails place it and supply "Quit" on Classic and macOS, "Exit" on Win32.
     * A declared non-empty title or title State value wins.
     */
    inline MenuDefinition QuitMenu()
    {
      return AppMenu() << MenuItem("").actionType(MENU_ACTION_QUIT_APP);
    }

    /** Ordinary Edit menu; rails supply titles and default key equivalents. */
    inline MenuDefinition EditMenu()
    {
      return Menu("Edit") << MenuItem("").actionType(MENU_ACTION_EDIT_UNDO) << MenuItemDefinition().separator()
                          << MenuItem("").actionType(MENU_ACTION_EDIT_CUT)
                          << MenuItem("").actionType(MENU_ACTION_EDIT_COPY)
                          << MenuItem("").actionType(MENU_ACTION_EDIT_PASTE)
                          << MenuItem("").actionType(MENU_ACTION_EDIT_SELECT_ALL);
    }

    /**
     * Request the current standard bundle: About then Quit in one fresh app
     * container, with empty, childless items. Rails place the items and section
     * separators and supply About / Quit on Classic and macOS, Exit on Win32;
     * Win32 does not show About yet. A declared non-empty title or title State
     * value wins. This bundle may grow: each addition changes adopting
     * applications' menus and requires its own compatibility decision.
     * Use AboutMenu() / QuitMenu() to request only one of these actions.
     */
    inline MenuDefinition StandardMenuPresets()
    {
      return AppMenu() << MenuItem("").actionType(MENU_ACTION_ABOUT_APP)
                       << MenuItem("").actionType(MENU_ACTION_QUIT_APP);
    }

    inline MenuItemDefinition MenuSeparator()
    {
      return MenuItemDefinition().separator();
    }

  } // namespace app
} // namespace loka

#endif // LOKA_APP_MENU_HPP
