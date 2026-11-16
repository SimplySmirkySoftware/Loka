#include "PopupFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebTestAccess.hpp"
#include "WebPopupContext.hpp"
#include "support/TestVerify.hpp"
#include "platform/String.hpp"
#include <cstdio>
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
namespace
{
  struct Rig
  {
    WebSession session;
    Token token;
    explicit Rig(int mode = 0, bool fail = false)
    {
      webtest::reset();
      webtest::popupMode() = mode;
      webtest::failMetrics = fail ? -1 : 0;
      LOKA_VERIFY(this->session.start(&webtest::CreatePopups, 0) == WebSession::STARTED);
      this->token = webtest::token(POPUP);
      LOKA_VERIFY(this->token);
    }
    ~Rig()
    {
      webtest::probe = 0;
      webtest::tailProbe = 0;
      this->session.dispose();
      webtest::fire(webtest::task(SHUTDOWN), 1000);
      LOKA_VERIFY(webtest::resources.empty());
    }
    WebPopupContext &context()
    {
      return *this->session.routes().find(this->token)->popup();
    }
    webtest::Resource &row()
    {
      return webtest::resources[this->token];
    }
    void flush()
    {
      this->session.wake(IMMEDIATE, 0);
    }
    void set(int index)
    {
      {
        loka::core::Operation turn;
        webtest::popupFixture()->selected.set(index);
      }
      this->flush();
    }
    void apply(const PopupMenu &props)
    {
      {
        loka::core::Operation turn;
        LOKA_VERIFY(props.applyPropsToNode(this->context().owner()));
        this->context().controller().requestRelayout();
      }
      this->flush();
    }
  };
  Rig *current = 0;
  void Rewrite()
  {
    webtest::popupFixture()->selected.set(0);
  }
  void Grow()
  {
    webtest::popupFixture()->items.push_back(loka::core::String::Literal("new"));
    webtest::popupFixture()->selected.set(3);
  }
  void Disable()
  {
    webtest::popupFixture()->enabled.set(false);
  }
  void Nested()
  {
    webtest::popupFixture()->action = 0;
    LOKA_VERIFY(current->session.select(current->token, 2, 0) == WebSession::NESTED);
    LOKA_VERIFY(current->row().selection.index() == 0);
    webtest::popupFixture()->selected.set(1);
  }
  void Completing()
  {
    webtest::tailProbe = 0;
    LOKA_VERIFY(current->session.select(current->token, 2, 0) == WebSession::REFUSED_COMPLETING);
    loka::core::Operation turn;
    webtest::popupFixture()->selected.set(2);
  }
  void ObserveRender(const char *name, uint32_t)
  {
    if (std::strcmp(name, "selected") != 0)
      return;
    webtest::probe = 0;
    LOKA_VERIFY(current->session.select(current->token, 0, 0) == WebSession::REFUSED_COMPLETING);
    webtest::popupFixture()->selected.set(2);
  }
  void PopupProjectionAndInput()
  {
    Rig rig;
    current = &rig;
    webtest::PopupFixture &f = *webtest::popupFixture();
    LOKA_VERIFY(rig.row().options.size() == 3 && rig.row().options[1] == "<b>Banana");
    LOKA_VERIFY(rig.row().selection.index() == 0 && rig.row().selection.controlled() && rig.row().enabled
                && rig.row().projected);
    rig.set(2);
    LOKA_VERIFY(rig.row().selection.index() == 2);
    rig.set(-2);
    LOKA_VERIFY(rig.row().selection.index() == 0 && f.selected.get() == -2);
    rig.set(40000);
    LOKA_VERIFY(rig.row().selection.index() == 2 && f.selected.get() == 40000);
    LOKA_VERIFY(rig.session.select(webtest::token(ROOT), 1, 0) == WebSession::REFUSED_TOKEN);
    rig.set(0);
    const int invalid[] = {-1, 3, 40000, 65536};
    for (unsigned i = 0; i < 4; ++i)
    {
      const unsigned calls = webtest::calls.size();
      {
        loka::core::Operation turn;
        LOKA_VERIFY(!rig.context().select(invalid[i]));
      }
      LOKA_VERIFY(f.selected.get() == 0 && f.hits == 0 && webtest::calls.size() == calls);
    }
    LOKA_VERIFY(rig.session.select(rig.token, 1, 0) == WebSession::ACCEPTED);
    LOKA_VERIFY(f.selected.get() == 1 && f.hits == 1 && rig.row().selection.index() == 1);
    f.watchAction = &Rewrite;
    rig.session.select(rig.token, 2, 0);
    LOKA_VERIFY(f.selected.get() == 0 && rig.row().selection.index() == 0);
    f.watchAction = 0;
    f.action = &Nested;
    rig.session.select(rig.token, 1, 0);
    LOKA_VERIFY(f.selected.get() == 1 && rig.row().selection.index() == 1);
    f.action = 0;
    webtest::tailProbe = &Completing;
    rig.session.select(rig.token, 0, 0);
    LOKA_VERIFY(rig.row().selection.index() == 2);
    webtest::probe = &ObserveRender;
    rig.session.select(rig.token, 1, 0);
    LOKA_VERIFY(!webtest::probe && rig.row().selection.index() == 2);
    f.action = &Grow;
    webtest::calls.clear();
    rig.session.select(rig.token, 1, 0);
    LOKA_VERIFY(rig.row().options.size() == 4 && rig.row().selection.index() == 3);
    bool option = false, selected = false;
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
    {
      if (webtest::calls[i].find("option:") == 0)
        option = true;
      if (webtest::calls[i].find("selected:") == 0)
      {
        LOKA_VERIFY(option);
        selected = true;
      }
    }
    LOKA_VERIFY(selected);
    f.action = &Disable;
    rig.session.select(rig.token, 2, 0);
    LOKA_VERIFY(rig.row().selection.index() == 2 && !rig.row().enabled);
    const unsigned hits = f.hits, calls = webtest::calls.size();
    LOKA_VERIFY(rig.session.select(rig.token, 1, 0) == WebSession::REFUSED_TOKEN);
    LOKA_VERIFY(!rig.context().select(1));
    LOKA_VERIFY(f.hits == hits && webtest::calls.size() == calls);
    rig.session.dispose();
    LOKA_VERIFY(rig.session.select(rig.token, 1, 0) == WebSession::REFUSED_CLOSED);
    std::puts("PopupProjectionAndInput passed");
  }
  class RefusedLabel : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };
  void PopupUtf8()
  {
    Rig rig;
    loka::Vector<loka::core::String> items;
    items.push_back(loka::core::String::Literal("kept"));
    items.push_back(
        loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(new RefusedLabel())));
    items.push_back(loka::core::String::Literal("last"));
    rig.apply(PopupMenu(&items).selectedIndex(webtest::popupFixture()->selected));
    rig.set(1);
    LOKA_VERIFY(rig.row().options.size() == 3 && rig.row().options[0] == "kept");
    LOKA_VERIFY(rig.row().options[1].empty() && rig.row().optionDisabled[1]);
    LOKA_VERIFY(rig.row().options[2] == "last" && rig.row().selection.index() == 1);
    LOKA_VERIFY(!webtest::task(IMMEDIATE));
    std::puts("PopupUtf8 refused label clears its seat without retry passed");
  }
  void PopupProps()
  {
    Rig rig;
    webtest::PopupFixture &f = *webtest::popupFixture();
    f.items[0] = loka::core::String::Literal("unpublished");
    rig.flush();
    LOKA_VERIFY(rig.row().options[0] == "Apple");
    loka::Vector<loka::core::String> fresh;
    fresh.push_back(loka::core::String::Literal("replacement"));
    rig.apply(PopupMenu(&fresh).selectedIndex(f.selected));
    LOKA_VERIFY(rig.context().token() == rig.token && rig.row().options.size() == 1);
    LOKA_VERIFY(rig.row().options[0] == "replacement");
    rig.apply(PopupMenu(&fresh).onChange(&f.changed));
    LOKA_VERIFY(!rig.row().selection.controlled());
    const unsigned calls = webtest::calls.size();
    LOKA_VERIFY(!rig.context().select(0));
    LOKA_VERIFY(!f.hits && calls == webtest::calls.size());
    rig.apply(PopupMenu(&fresh).selectedIndex(f.selected));
    LOKA_VERIFY(rig.row().selection.controlled());
    rig.apply(PopupMenu().selectedIndex(f.selected));
    LOKA_VERIFY(rig.row().options.empty() && rig.row().selection.index() == -1);
    fresh.clear();
    for (unsigned i = 0; i < 33000; ++i)
      fresh.push_back(loka::core::String::Literal("x"));
    rig.apply(PopupMenu(&fresh).selectedIndex(f.selected));
    LOKA_VERIFY(rig.row().options.size() == 32767);
    rig.session.select(rig.token, 32766, 0);
    LOKA_VERIFY(f.selected.get() == 32766 && rig.row().selection.index() == 32766);
    LOKA_VERIFY(!rig.context().select(32767));
    LOKA_VERIFY(f.selected.get() == 32766);
    std::puts("PopupProps applied items, borrowed negative control, seat transitions and count boundary passed");
  }
  void PopupLayout()
  {
    {
      Rig rig(1);
      LOKA_VERIFY(rig.row().height == webtest::resources[webtest::token(BUTTON)].height);
    }
    for (int initial = 0; initial < 2; ++initial)
    {
      Rig rig(0, initial != 0);
      webtest::PopupFixture &f = *webtest::popupFixture();
      if (!initial)
      {
        webtest::failMetrics = -1;
        rig.context().controller().requestRelayout();
        rig.flush();
      }
      LOKA_VERIFY(!rig.row().projected && webtest::task(IMMEDIATE));
      if (initial)
        LOKA_VERIFY(rig.row().width == 0 && rig.row().height == 0);
      const unsigned measured = webtest::metricCalls;
      rig.context().render(&rig.context().controller());
      LOKA_VERIFY(webtest::metricCalls == measured && !rig.row().projected);
      {
        loka::core::Operation turn;
        f.shown.set(false);
      }
      rig.flush();
      LOKA_VERIFY(rig.row().hidden && !rig.context().active());
      const unsigned hits = f.hits, calls = webtest::calls.size();
      LOKA_VERIFY(rig.session.select(rig.token, 1, 0) == WebSession::REFUSED_TOKEN);
      LOKA_VERIFY(!rig.context().select(1) && calls == webtest::calls.size() && hits == f.hits);
      webtest::failMetrics = 0;
      {
        loka::core::Operation turn;
        f.shown.set(true);
      }
      rig.flush();
      LOKA_VERIFY(!rig.row().hidden && rig.row().projected && rig.row().height > 0);
      webtest::failMetrics = -1;
      rig.context().controller().requestRelayout();
      rig.flush();
      LOKA_VERIFY(!rig.row().projected && webtest::task(IMMEDIATE));
      webtest::failMetrics = 0;
      webtest::fire(webtest::task(IMMEDIATE), 1);
      LOKA_VERIFY(rig.row().projected);
      {
        loka::core::Operation turn;
        f.shown.set(false);
      }
      rig.flush();
      {
        loka::core::Operation turn;
        f.shown.set(true);
      }
      rig.flush();
      LOKA_VERIFY(rig.row().projected && !rig.row().hidden);
    }
    {
      Rig rig(2);
      Node *box = rig.session.routes()
                      .find(webtest::token(SCROLL))
                      ->receiver->owner()
                      ->asScrollViewNode()
                      ->childrenHead()
                      ->asNestable()
                      ->childrenHead();
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(32760).applyPropsToNode(box));
        rig.context().controller().requestRelayout();
      }
      rig.flush();
      LOKA_VERIFY(!rig.row().projected);
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(1).applyPropsToNode(box));
        rig.context().controller().requestRelayout();
      }
      rig.flush();
      LOKA_VERIFY(rig.row().projected);
      {
        loka::core::Operation turn;
        webtest::popupFixture()->shown.set(false);
      }
      rig.flush();
      const int oldSelection = rig.row().selection.index();
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(32767).applyPropsToNode(box));
        rig.context().controller().requestRelayout();
        webtest::popupFixture()->selected.set(2);
        webtest::popupFixture()->shown.set(true);
      }
      rig.flush();
      LOKA_VERIFY(rig.row().selection.index() == oldSelection && rig.row().projected);
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(1).applyPropsToNode(box));
        rig.context().controller().requestRelayout();
      }
      rig.flush();
      LOKA_VERIFY(rig.row().selection.index() == 2);
    }
    std::puts("PopupLayout aligned height, mounted retry, render metric count, park and coordinate refusal passed");
  }
} // namespace
void WebPopupTests()
{
  PopupProjectionAndInput();
  PopupProps();
  PopupUtf8();
  PopupLayout();
}
