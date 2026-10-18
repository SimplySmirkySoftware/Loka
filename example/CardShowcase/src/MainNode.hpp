#ifndef LOKA_CARDSHOWCASE_MAIN_NODE_HPP
#define LOKA_CARDSHOWCASE_MAIN_NODE_HPP

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include "app/FileImageSource.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/scene/BorrowedKeys.hpp"

// PROBE ONLY: optional caret controls and initial viewport.
#include "app/nodes/controls/EditText.hpp"
#include <cstdlib>
#include <cstring>

namespace cardshowcase
{
  // Nominal macOS push button height 32 + two 8-unit bar paddings.
  // The inset is approximate: the app has no laid-out bar-height fact yet.
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
      return rhs.propsTypeId() == this->propsTypeId() && this->keys_ < static_cast<const MainProps &>(rhs).keys_;
    }

  private:
    loka::app::scene::BorrowedKeys<1> keys_;
  };

  /** Shows a blurred toolbar over scrolling photos, colored headings and shadowed Cards. */
  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
  public:
    typedef MainProps PropsType;

    explicit MainNode(const PropsType &p)
        : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(p),
          viewport_(),
          laptops_(),
          flyer_(),
          // PROBE ONLY: immutable launch choice; state remains Boundary-owned.
          probeCaret_(ProbeCaretEnabled()),
          probeText_()
    {
      this->state(this->laptops_, LoadPhoto(p.platformContext(), "LokaLaptops.jpg"));
      this->state(this->flyer_, LoadPhoto(p.platformContext(), "LokaFlyer_0.0.1_PBG4.jpg"));
      // PROBE ONLY: explicit initial offset; absent means the normal top inset.
      const char *scrollY = std::getenv("LOKA_PROBE_SCROLL_Y");
      this->state(this->viewport_, scrollY ? loka::core::Frame(0, std::atoi(scrollY), 0, 0)
                                          : loka::app::ScrollViewProps::topOrigin());
      if (this->probeCaret_)
        this->state(this->probeText_, loka::core::String("Caret probe"));
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(
          ZStack().expBlurUnits()
          << (ScrollView(ScrollViewProps(this->viewport_).contentInsets(ScrollViewContentInsets::of(kBarHeight, 0)))
              << (Box().padding(12)
                  // PROBE ONLY: prepend the caret row with a plain conditional declaration.
                  << (this->ProbeColumn().gap(12)
                      << Text("Expressive Loka") + FontSize<24>() + Red
                      << (Row().gap(12) << ImageView()
                                               .image(this->laptops_.state())
                                               .size(210, 180)
                                               .attr(ImageViewAttr().fit(IMAGE_FIT_COVER))
                                        << ImageView()
                                               .image(this->flyer_.state())
                                               .size(210, 180)
                                               .attr(ImageViewAttr().fit(IMAGE_FIT_COVER)))
                      << Text("A little color, a quiet shadow, and room to breathe. "
                              "Scroll to watch these cards and colors pass beneath the toolbar.")
                             + BlockStyle().wrap(TEXT_WRAP_WORD)
                      << (Card() << (Column().gap(6) << Text("Made for the moment") + FontSize<18>() + Blue
                                                     << Text("Native controls keep familiar gestures close at hand.")
                                                            + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card() << (Column().gap(6) << Text("A softer edge") + FontSize<18>() + Magenta
                                                     << Text("Default card shadows give each thought a little space.")
                                                            + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card() << (Column().gap(6)
                                     << Text("Color in motion") + FontSize<18>() + Green
                                     << Text("Move slowly through the page and notice what changes above.")
                                            + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Row().gap(6) << (Card() << (Column() << Text("Sky") + Blue << Text("Look up")))
                                       << (Card() << (Column() << Text("Bloom") + Magenta << Text("Look closer")))
                                       << (Card() << (Column() << Text("Leaf") + Green << Text("Take a breath"))))
                      << Text("Make something bright.") + FontSize<24>() + Red
                      << Text("Leave room for wonder.") + FontSize<24>() + Blue
                      << Text("Follow a new color.") + FontSize<24>() + Magenta
                      << Text("Keep exploring.") + FontSize<24>() + Green)))
          << (Box().padding(8) << (Row().naturalWidth() << Button("< Back")
                                                        << Text("Expressive Loka - a study in color and light")
                                                               + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)
                                                        << Button("Share"))));
    }

  private:
    // PROBE ONLY: called once per composition; the completed Column owns its definitions.
    loka::app::Column ProbeColumn() const
    {
      using namespace loka::app;
      Column column;
      if (this->probeCaret_)
        column << (Row().gap(8) << EditText(this->probeText_) << Button("Probe"));
      return column;
    }

    // PROBE ONLY: evaluated once by the constructor; only MainNode reads the result.
    static bool ProbeCaretEnabled()
    {
      const char *value = std::getenv("LOKA_PROBE_CARET");
      return value && std::strcmp(value, "1") == 0;
    }

    static loka::core::resource::Image LoadPhoto(PlatformContext *context, const char *name)
    {
      loka::core::resource::Image image;
      loka::core::resource::Blob blob;
      const loka::file::File file = loka::file::File::Application() << loka::file::File(name);
      // A photo that fails to read or decode leaves its ImageView empty.
      if (loka::app::ReadFileImageBlob(context, file, blob) != loka::platform::file::READ_OK
          || !loka::app::DecodeFileImageBlob(context, blob, image))
        return loka::core::resource::Image::Empty();
      return image;
    }

    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::app::scene::NodeState<loka::core::resource::Image> laptops_;
    loka::app::scene::NodeState<loka::core::resource::Image> flyer_;
    // PROBE ONLY: fixed declaration choice and lifecycle-owned editor value.
    const bool probeCaret_;
    loka::app::scene::NodeState<loka::core::String> probeText_;
  };
} // namespace cardshowcase

#endif // LOKA_CARDSHOWCASE_MAIN_NODE_HPP
