#include "CardFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebSession.hpp"
#include "WebWindow.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
using loka::core::Frame;
using loka::dsl::testing::SceneTestAccess;
namespace
{
  /** Cost: session bootstrap once; one test config allocation, no rows. */
  template <CardShadowExtent Extent, bool Scrolled> AppConfigurable *CreateCards(PlatformContext *platform)
  {
    return new webtest::CardConfig<Extent, false, Scrolled>(platform);
  }
  /** Cost: suite once per scenario; walks four fixture rows per geometry/lifetime observation. */
  template <CardShadowExtent Extent, bool Scrolled> void CheckCard()
  {
    typedef webtest::CardFixture<Extent, false, Scrolled> Fixture;
    const Frame dropStrips[] = {Frame(0, 0, 94, 3), Frame(0, 3, 3, 77), Frame(94, 0, 6, 74), Frame(3, 74, 97, 6)};
    const Frame wideStrips[] = {Frame(0, 0, 85, 11), Frame(0, 11, 15, 69), Frame(85, 0, 15, 61), Frame(15, 61, 85, 19)};
    const Frame dropBodies[] = {Frame(0, 0, 97, 12), Frame(0, -3, 12, 77), Frame(-9, 0, 12, 77), Frame(-3, -9, 97, 12)};
    const Frame wideBodies[] = {
        Frame(12, 8, 76, 44), Frame(12, -3, 36, 56), Frame(-33, 8, 36, 56), Frame(-3, -41, 76, 44)};
    webtest::reset();
    WebSession session;
    const int margin = Scrolled ? 12 : 0;
    LOKA_VERIFY((session.start(&CreateCards<Extent, Scrolled>, 0) == WebSession::STARTED));
    const Token root = webtest::token(ROOT);
    Scene &scene = *session.routes().find(root)->root->scene();
    Token bands[CARD_BAND_COUNT] = {};
    const Token parent = Scrolled ? webtest::token(SCROLL) : root;
    LOKA_VERIFY(!Scrolled || parent);
    LOKA_VERIFY(webtest::resources.size() == (Scrolled ? 6u : 5u));
    for (std::map<Token, webtest::Resource>::const_iterator it = webtest::resources.begin();
         it != webtest::resources.end();
         ++it)
    {
      if (it->first == root || it->first == parent)
        continue;
      const webtest::Resource &row = it->second;
      LOKA_VERIFY(row.kind == CARD_BAND && row.projected && !row.hidden && row.parent == parent);
      CardBandNode *band = session.routes().find(it->first)->receiver->owner()->asCardBandNode();
      LOKA_VERIFY(band);
      const CardBandRole role = band->props.role;
      LOKA_VERIFY(!bands[role]);
      bands[role] = it->first;
      Frame strip = Extent == CARD_SHADOW_DROP ? dropStrips[role] : wideStrips[role];
      if (Scrolled)
        strip.y += 7;
      LOKA_VERIFY(Frame(row.x, row.y, row.width, row.height) == strip);
      LOKA_VERIFY(row.cardBody.first == (Extent == CARD_SHADOW_DROP ? dropBodies[role] : wideBodies[role]));
      LOKA_VERIFY(row.cardBody.second == Extent);
    }
    for (int pass = 0; pass < 2; ++pass)
    {
      LOKA_VERIFY(session.resize(root, (pass ? 100 : 10) + 2 * margin, 120, 0) == WebSession::ACCEPTED);
      for (int role = 0; role < CARD_BAND_COUNT; ++role)
      {
        LOKA_VERIFY(webtest::resources.count(bands[role]));
        LOKA_VERIFY(webtest::resources[bands[role]].projected == (pass != 0));
      }
    }
    for (int attached = 0; attached < 2; ++attached)
    {
      {
        loka::core::Operation turn;
        static_cast<Fixture *>(SceneTestAccess::rootBoundary(scene))->shown.set(attached != 0);
      }
      session.wake(IMMEDIATE, 0);
      for (int role = 0; role < CARD_BAND_COUNT; ++role)
      {
        LOKA_VERIFY(webtest::resources.count(bands[role]));
        LOKA_VERIFY(webtest::resources[bands[role]].hidden == (attached == 0));
      }
    }
    session.dispose();
    webtest::fire(webtest::task(SHUTDOWN), 1000);
    LOKA_VERIFY(webtest::resources.empty());
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
      LOKA_VERIFY(!session.routes().find(bands[role]));
  }
} // namespace
/** Cost: native test runner once; three isolated fixture mounts. */
void WebCardFrameTests()
{
  CheckCard<CARD_SHADOW_DROP, false>();
  CheckCard<CARD_SHADOW_WIDE, false>();
  CheckCard<CARD_SHADOW_WIDE, true>();
}
