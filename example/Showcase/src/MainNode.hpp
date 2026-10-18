#ifndef LOKA_SHOWCASE_MAIN_NODE_HPP
#define LOKA_SHOWCASE_MAIN_NODE_HPP

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include <cstdlib>

namespace showcase
{
  // Approximate per rail: nominal macOS button height 32 + two 8 LU pads.
  // macOS measures its actual button; the app has no laid-out bar-height fact yet.
  const int kBarHeight = 48;

  class MainNode : public loka::app::scene::BoundaryNodeFor<MainNode>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<MainNode> PropsType;

    explicit MainNode(const PropsType &p)
        : loka::app::scene::BoundaryNodeFor<MainNode>(p),
          viewport_()
    {
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
                      << Text("A little color, a quiet shadow, and room to breathe. "
                              "Scroll to watch these cards and colors pass beneath the toolbar.")
                             + BlockStyle().wrap(TEXT_WRAP_WORD)
                      << (Card()
                          << (Column().gap(6)
                              << Text("Made for the moment") + FontSize<18>() + Blue
                              << Text("Native controls keep familiar gestures close at hand.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card()
                          << (Column().gap(6)
                              << Text("A softer edge") + FontSize<18>() + Magenta
                              << Text("Default card shadows give each thought a little space.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Card()
                          << (Column().gap(6)
                              << Text("Color in motion") + FontSize<18>() + Green
                              << Text("Move slowly through the page and notice what changes above.")
                                     + BlockStyle().wrap(TEXT_WRAP_WORD)))
                      << (Row().gap(6)
                          << (Card() << (Column() << Text("Sky") + Blue << Text("Look up")))
                          << (Card() << (Column() << Text("Bloom") + Magenta << Text("Look closer")))
                          << (Card() << (Column() << Text("Leaf") + Green << Text("Take a breath"))))
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
    loka::app::scene::NodeState<loka::core::Frame> viewport_;
  };
} // namespace showcase

#endif // LOKA_SHOWCASE_MAIN_NODE_HPP
