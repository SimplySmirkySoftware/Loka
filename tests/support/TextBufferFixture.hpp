#ifndef LOKA_TEST_TEXT_BUFFER_FIXTURE_HPP
#define LOKA_TEST_TEXT_BUFFER_FIXTURE_HPP
#include "app/TextBuffer.hpp"

namespace loka
{
  namespace testing
  {
    /** Test setup conveniences expressed through the same facade as app mutations. */
    class TextBufferFixture : public app::TextBuffer
    {
    public:
      class OneOp : public core::ListOpCursor<core::String>
      {
      public:
        explicit OneOp(const core::ListOp<core::String> &op)
            : op_(op),
              done_(false)
        {
        }
        virtual void rewind()
        {
          this->done_ = false;
        }
        virtual bool next(core::ListOp<core::String> &out)
        {
          if (this->done_)
            return false;
          out = this->op_;
          this->done_ = true;
          return true;
        }

      private:
        core::ListOp<core::String> op_;
        bool done_;
      };

    public:
      core::ListAttachResult attach(core::PushStateTracker *tracker, unsigned short count, std::size_t bytes = 8192)
      {
        class Rail : public PlatformContext
        {
        public:
          virtual bool queryTextEditorCapacity(app::TextCapacity &out) const
          {
            out = app::TextCapacity::of(65535, INT_MAX);
            return true;
          }
          virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
          {
            return 0;
          }
          virtual Window *createWindow(const WindowProps &)
          {
            return 0;
          }
          virtual app::scene::NodeContext *createNodeContext(app::scene::Node *) const
          {
            return 0;
          }
          virtual bool
          createImageFromBlob(const core::resource::Blob &, std::size_t, std::size_t, core::resource::Image &)
          {
            return false;
          }
          virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
          {
            return false;
          }
        } rail;
        const AttachResult result = app::TextBuffer::attach(tracker, rail, app::TextCapacity::of(count, bytes));
        switch (result)
        {
        case app::TextBuffer::ATTACH_OK:
          return core::ATTACH_OK;
        case app::TextBuffer::ATTACH_ALREADY_ATTACHED:
          return core::ATTACH_ALREADY_ATTACHED;
        case app::TextBuffer::ATTACH_NULL_TRACKER:
          return core::ATTACH_NULL_TRACKER;
        case app::TextBuffer::ATTACH_GENERATION_EXHAUSTED:
          return core::ATTACH_GENERATION_EXHAUSTED;
        case app::TextBuffer::ATTACH_EMPTY_CAPACITY:
        case app::TextBuffer::ATTACH_OVER_RAIL_MAXIMUM:
        case app::TextBuffer::ATTACH_RAIL_DECLINED:
        case app::TextBuffer::ATTACH_ALLOCATION_REFUSED:
          return core::ATTACH_ALLOCATION_REFUSED;
        }
        return core::ATTACH_ALLOCATION_REFUSED;
      }
      const core::State<core::ListRevision> &revision() const
      {
        return this->lines().revision();
      }
      unsigned short size() const
      {
        return this->lines().size();
      }
      const app::TextLines::Entry &at(unsigned short i) const
      {
        return this->lines().at(i);
      }
      int find(core::ItemId id) const
      {
        return this->lines().find(id);
      }
      core::ListEditResult queryMutationTracker(core::StateTracker *&out) const
      {
        return this->lines().queryMutationTracker(out);
      }
      core::ListEditResult apply(core::ListOpCursor<core::String> &ops)
      {
        return this->lines().apply(ops);
      }
      core::ListEditResult update(core::ItemId id, const core::String &row)
      {
        return this->lines().update(id, row);
      }
      core::ListEditResult insert(unsigned short i, const core::String &row, core::ItemId *id = 0)
      {
        OneOp op(core::ListOp<core::String>(core::INSERT, core::ItemId(), i, row));
        core::ListEditResult result = this->apply(op);
        if (result == core::EDIT_OK && id)
          *id = this->at(i).id;
        return result;
      }
      core::ListEditResult remove(core::ItemId id)
      {
        OneOp op(core::ListOp<core::String>(core::REMOVE, id));
        return this->apply(op);
      }
      core::ListEditResult replaceWithOne(const core::String &row)
      {
        class Replacement : public core::ListOpCursor<core::String>
        {
        public:
          Replacement(const app::TextLines &lines, const core::String &row)
              : lines_(lines),
                row_(row),
                position_(0)
          {
          }
          virtual void rewind()
          {
            this->position_ = 0;
          }
          virtual bool next(core::ListOp<core::String> &out)
          {
            if (this->position_ < this->lines_.size())
              out = core::ListOp<core::String>(core::REMOVE,
                                               this->lines_.at(this->lines_.size() - 1 - this->position_).id);
            else if (this->position_ == this->lines_.size())
              out = core::ListOp<core::String>(core::INSERT, core::ItemId(), 0, this->row_);
            else
              return false;
            ++this->position_;
            return true;
          }

        private:
          const app::TextLines &lines_;
          core::String row_;
          std::size_t position_;
        } replacement(this->lines(), row);
        return this->apply(replacement);
      }
      core::ListEditResult move(core::ItemId id, unsigned short i)
      {
        OneOp op(core::ListOp<core::String>(core::MOVE, id, i));
        return this->apply(op);
      }
    };
    inline core::ListEditResult InsertTextLine(app::TextLines &lines, unsigned short index, const core::String &row)
    {
      TextBufferFixture::OneOp op(core::ListOp<core::String>(core::INSERT, core::ItemId(), index, row));
      return lines.apply(op);
    }
    inline core::ListEditResult RemoveTextLine(app::TextLines &lines, core::ItemId id)
    {
      TextBufferFixture::OneOp op(core::ListOp<core::String>(core::REMOVE, id));
      return lines.apply(op);
    }
  } // namespace testing
} // namespace loka
#endif
