#ifndef LOKA_APP_DOC_BLOCK_WALK_HPP
#define LOKA_APP_DOC_BLOCK_WALK_HPP
#include "app/doc/BlockReader.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Cost: one dispatch per attached composition resident, pruning the block's projection. */
        inline bool WalkDocumentBlocks(scene::Node *node, BlockReader &reader)
        {
          if (node->lifecycleFact() != scene::NODE_FACT_ATTACHED)
            return true;
          switch (node->readDocumentBlock(reader))
          {
          case scene::DOCUMENT_BLOCK_READ:
            return true;
          case scene::DOCUMENT_BLOCK_REFUSED:
            return false;
          case scene::DOCUMENT_BLOCK_NONE:
            break;
          }
          scene::INestable *nest = node->asNestable();
          for (scene::Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
            if (!WalkDocumentBlocks(child, reader))
              return false;
          return true;
        }

        /** Cost: once per visited container, dispatch each of its own children once until refusal. */
        template <class Props>
        scene::DocumentBlockRead ReadContainer(BlockReader &reader, const Props &props, scene::Node *child)
        {
          if (!reader.enter(props))
            return scene::DOCUMENT_BLOCK_REFUSED;
          for (; child; child = child->nextInComposition)
            if (!WalkDocumentBlocks(child, reader))
              return scene::DOCUMENT_BLOCK_REFUSED;
          return reader.leave(props) ? scene::DOCUMENT_BLOCK_READ : scene::DOCUMENT_BLOCK_REFUSED;
        }
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
