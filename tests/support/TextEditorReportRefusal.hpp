#ifndef LOKA_TEST_TEXT_EDITOR_REPORT_REFUSAL_HPP
#define LOKA_TEST_TEXT_EDITOR_REPORT_REFUSAL_HPP
#ifndef TEST_BUILD
#error TextEditorReportRefusal is test-only
#endif
#include "app/nodes/controls/TextEditorRequests.hpp"
#include "platform/String.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Fixture string: allow validation, then refuse report's materialization
          once. Like AttributedString's RefusedString, this exercises the actual
          StringBuffer refusal seam without adding a production failure hook. */
      class TextEditorReportRefusal : public platform::String
      {
      public:
        explicit TextEditorReportRefusal(scene::RequestWithReply<LineCursor> &request)
            : request_(request),
              phase_(READY)
        {
          this->request_.state()->bind(&cleared, this, false);
        }
        virtual ~TextEditorReportRefusal()
        {
          this->request_.state()->unbind(&cleared, this);
        }
        virtual bool queryUtf8(platform::Utf8View &out) const
        {
          if (this->phase_ == REPORT)
          {
            this->phase_ = REFUSE_BUFFER;
            return false;
          }
          if (this->phase_ == VALIDATE)
            this->phase_ = REPORT;
          out.bytes = "abcd";
          out.length = 4;
          return true;
        }
        virtual bool appendUtf8(std::string &out) const
        {
          if (this->phase_ == REFUSE_BUFFER)
          {
            this->phase_ = READY;
            return false;
          }
          out.append("abcd");
          return true;
        }

      private:
        enum Phase
        {
          READY,
          VALIDATE,
          REPORT,
          REFUSE_BUFFER
        };
        static void cleared(void *data)
        {
          TextEditorReportRefusal &self = *static_cast<TextEditorReportRefusal *>(data);
          if (self.request_.get().isNone())
            self.phase_ = VALIDATE;
        }
        scene::RequestWithReply<LineCursor> &request_;
        mutable Phase phase_;
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
