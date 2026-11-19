#ifndef LOKA_APP_DOC_RENDER_HPP
#define LOKA_APP_DOC_RENDER_HPP

#include "app/doc/Markdown.hpp"
#include "app/doc/Html.hpp"
#include "app/scene/detail/OneShotComposition.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Borrows the destination for one synchronous call to the selected document exit. */
        template <bool (*Write)(scene::Node *, std::string &)> struct DocumentWriter
        {
          std::string &out;

          /** Cost: once per accepted render, visits the page's attached document tree. */
          bool operator()(scene::Node *root) const
          {
            return Write(root, this->out);
          }
        };
      } // namespace detail

      /** Append one fresh rendering of a window-independent Boundary. Refusal preserves out.
          Call from a tool's main thread outside any Operation or Regime. Each call runs page code again.
          Layout and idle work are absent. A LazyView contributes only its seeded viewport window.
          Cost: one page mount, bounded settlement, one document walk and teardown per call. */
      template <class BoundaryDefinitionT> bool RenderMarkdown(const BoundaryDefinitionT &page, std::string &out)
      {
        detail::DocumentWriter<WriteMarkdown> writer = {out};
        return scene::detail::OneShotComposition::lend(page, writer);
      }

      /** Append a fresh HTML fragment under the same mount and settlement contract as RenderMarkdown.
          Cost: one page mount, bounded settlement, one document walk and teardown per tool call. */
      template <class BoundaryDefinitionT> bool RenderHtml(const BoundaryDefinitionT &page, std::string &out)
      {
        detail::DocumentWriter<WriteHtml> writer = {out};
        return scene::detail::OneShotComposition::lend(page, writer);
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
