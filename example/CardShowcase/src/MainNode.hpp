#ifndef LOKA_CARDSHOWCASE_MAIN_NODE_HPP
#define LOKA_CARDSHOWCASE_MAIN_NODE_HPP

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include <cstdlib>
#include <cstdio>
#include <string>
#if !defined(LOKA_RETRO68)
#include "app/FileImageSource.hpp"
#endif
#include "app/nodes/ImageView.hpp"
#include "app/scene/BorrowedKeys.hpp"

namespace cardshowcase
{
  // Approximate per rail: nominal macOS button height 32 + two 8 LU pads.
  // macOS measures its actual button; the app has no laid-out bar-height fact yet.
  const int kBarHeight = 48;

  class MainNode;
  struct MainTypeTag
  {
  };

  /** Borrows the application platform for one-time photo decoding. */
  struct MainProps : public loka::app::scene::NodePropsBase<MainProps>
  {
    typedef MainTypeTag TypeTag;
    typedef MainNode NodeType;

    MainProps &platformContext(PlatformContext *context)
    {
      this->keys_.set(0, context);
      return *this;
    }
    PlatformContext *platformContext() const
    {
      return static_cast<PlatformContext *>(const_cast<void *>(this->keys_.get(0)));
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() &&
             this->keys_ < static_cast<const MainProps &>(rhs).keys_;
    }

  private:
    loka::app::scene::BorrowedKeys<1> keys_;
  };

  // PROBE ONLY: LOKA_PROBE_CARD_SHADOW=none|drop|wide-stencil|wide-alpha picks every
  // card's style, so the scroll cost of shadows and of blur can be measured apart.
  inline loka::app::CardStyle ProbeCardStyle()
  {
    using namespace loka::app;
    const char *value = std::getenv("LOKA_PROBE_CARD_SHADOW");
    if (!value)
      return CardStyle();
    const std::string name(value);
    if (name == "none")
      return CardStyle(CARD_SHADOW_NONE);
    if (name == "drop")
      return CardStyle(CARD_SHADOW_DROP, CARD_SHADOW_STENCIL);
    if (name == "wide-stencil")
      return CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_STENCIL);
    if (name == "wide-alpha")
      return CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
    return CardStyle();
  }

  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
  public:
    typedef MainProps PropsType;

    explicit MainNode(const PropsType &p)
        : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(p),
          viewport_(),
          laptops_(),
          flyer_()
    {
      this->state(this->laptops_, LoadPhoto(p.platformContext(), "LokaLaptops.jpg"));
      this->state(this->flyer_, LoadPhoto(p.platformContext(), "LokaFlyer_0.0.1_PBG4.jpg"));
      // PROBE ONLY: start scrolled so content sits under the toolbar for a capture.
      const char *probeY = std::getenv("LOKA_PROBE_SCROLL_Y");
      this->state(this->viewport_,
                  probeY ? loka::core::Frame(0, std::atoi(probeY), 0, 0) : loka::app::ScrollViewProps::topOrigin());
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(
          ZStack()
          << (ScrollView(ScrollViewProps(this->viewport_)
                             .contentInsets(ScrollViewContentInsets::of(kBarHeight, 0)))
              << (Box().padding(12)
                  << (Column().gap(12)
                      << Text("Expressive Loka") + FontSize<24>() + Red
#if !defined(LOKA_RETRO68)
                      << (Row().gap(12)
                          << ImageView().image(this->laptops_.state()).size(210, 180)
                                 .attr(ImageViewAttr().fit(IMAGE_FIT_COVER))
                          << ImageView().image(this->flyer_.state()).size(210, 180)
                                 .attr(ImageViewAttr().fit(IMAGE_FIT_COVER)))
#endif
                      << Text("A little color, a quiet shadow, and room to breathe. "
                              "Scroll to watch these cards and colors pass beneath the toolbar.")
                             + BlockStyle().wrap(TEXT_WRAP_WORD)
                      << (Card(ProbeCardStyle())
                          << (Column().gap(6)
                              << Text("Made for the moment") + FontSize<18>() + Blue
                              << Text("Native controls keep familiar gestures close at hand.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card(ProbeCardStyle())
                          << (Column().gap(6)
                              << Text("A softer edge") + FontSize<18>() + Magenta
                              << Text("Default card shadows give each thought a little space.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card(ProbeCardStyle())
                          << (Column().gap(6)
                              << Text("Color in motion") + FontSize<18>() + Green
                              << Text("Move slowly through the page and notice what changes above.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Row().gap(6)
                          << (Card(ProbeCardStyle()) << (Column() << Text("Sky") + Blue << Text("Look up")))
                          << (Card(ProbeCardStyle()) << (Column() << Text("Bloom") + Magenta << Text("Look closer")))
                          << (Card(ProbeCardStyle()) << (Column() << Text("Leaf") + Green << Text("Take a breath"))))
                      << Text("Make something bright.") + FontSize<24>() + Red
                      << Text("Leave room for wonder.") + FontSize<24>() + Blue
                      << Text("Follow a new color.") + FontSize<24>() + Magenta
                      << Text("Keep exploring.") + FontSize<24>() + Green)))
          << (Box().padding(8)
              << (Row().naturalWidth()
                  << Button("< Back")
                  << Text("Expressive Loka - a study in color and light")
                         + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)
                  << Button("Share"))));
    }

  private:
    static loka::core::resource::Image LoadPhoto(PlatformContext *context, const char *name)
    {
      loka::core::resource::Image image;
#if !defined(LOKA_RETRO68)
      loka::core::resource::Blob blob;
      const loka::file::File file = loka::file::File::Application() << loka::file::File(name);
      if (loka::app::ReadFileImageBlob(context, file, blob) != loka::platform::file::READ_OK ||
          !loka::app::DecodeFileImageBlob(context, blob, image))
        std::fprintf(stderr, "probe: photo unavailable: %s\n", name);
#else
      // Classic's native decoder accepts PICT, not these JPEG photographs.
      (void)context;
      (void)name;
#endif
      return image;
    }

    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::app::scene::NodeState<loka::core::resource::Image> laptops_;
    loka::app::scene::NodeState<loka::core::resource::Image> flyer_;
  };
} // namespace cardshowcase

#endif // LOKA_CARDSHOWCASE_MAIN_NODE_HPP
