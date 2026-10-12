#ifndef SMIRKYCARD_CARD_FLOW_HPP
#define SMIRKYCARD_CARD_FLOW_HPP
#include "CardRecords.hpp"
#include "CardFlowDescription.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class BindingToken;
    }
  } // namespace app
} // namespace loka

namespace smirkycard
{
  namespace testing
  {
    class CardFlowAccess;
  }

  /** Card-owned synchronous execution and subscription. The description is rooted
      until silent reclamation; Detaching only withdraws the watch. */
  class CardFlow
  {
  public:
    /** Per-card admission phase, with one stack owner for its Running interval. */
    class Admission
    {
    public:
      Admission()
          : active_(0)
      {
      }
      class Execution
      {
      public:
        Execution(Admission &door, CardFlow &flow, bool watching);
        ~Execution();
        bool accepted() const
        {
          return this->door_ != 0;
        }

      private:
        friend class Admission;
        bool capacity(JSContext *) const;
        JSValue publish(JSContext *, JsSeatRecord::Kind,
                        const loka::core::resource::Image &, const loka::file::File &);
        bool resolve(JSContext *, JSValueConst, JsSeatRecord::Kind,
                     loka::core::resource::Image &, loka::file::File &);
        struct Slot
        {
          Slot() : kind(JsSeatRecord::IMAGE), image(), file() {}
          JsSeatRecord::Kind kind;
          loka::core::resource::Image image;
          loka::file::File file;
        };
        Admission *door_;
        CardFlow &flow_;
        const uint32_t serial_;
        unsigned occupied_;
        Slot slots_[4];
        Execution(const Execution &);
        Execution &operator=(const Execution &);
      };

      /** Synchronous value projection; callers have checked the bound card. */
      JSValue project(JSContext *, const loka::core::resource::Image &);
      JSValue project(JSContext *, const loka::file::File &);
      bool resolve(JSContext *, const JsCardNode &, JSValueConst, JsSeatRecord::Kind,
                   loka::core::resource::Image &, loka::file::File &);
      bool hasExecution() const { return this->active_ != 0; }
      bool capacity(JSContext *) const;
      static bool installHandles(JSRuntime *);
    private:
      JSValue publish(JSContext *, JsSeatRecord::Kind,
                      const loka::core::resource::Image &, const loka::file::File &);
      Execution *active_;
      Admission(const Admission &);
      Admission &operator=(const Admission &);
    };
    struct Initial
    {
      JsCardNode &card;
      JSContext *context;
      JSValue description;
      JSValue handle;
      JsSeatRecord *seat;
      Initial(JsCardNode &c, JSContext *ctx, JSValueConst d, JSValueConst h, JsSeatRecord *s)
          : card(c),
            context(ctx),
            description(d),
            handle(h),
            seat(s)
      {
      }
    };
    explicit CardFlow(const Initial &initial);
    ~CardFlow();
    bool matches(JSValueConst handle) const;
    bool activate(loka::app::scene::BindingToken &token);
    void withdraw();
    bool run(JSValueConst value, bool watching = false);
    void fire();
    /** BindingToken owns its callback; this bridge owns the earlier detach line. */
    void bind(loka::core::StateBase::OnChangeFn callback, void *user, bool immediate);
    void unbind(loka::core::StateBase::OnChangeFn callback, void *user);
    CardFlow *next;

  private:
    friend class testing::CardFlowAccess;
    class Subscription;
    enum CallResult
    {
      Returned,
      Threw,
      Canceled
    };
    CallResult invoke(JSValueConst fn, JSValueConst input, JSValue &output);
    // Admission::Execution reaches ScriptRuntime and JsCardNode through these:
    // GCC 4.0 (Tiger UB1) does not extend CardFlow's friendship to its nested classes.
    static uint32_t issueExecutionSerial(CardFlow &flow);
    static void refuseExhaustedSerial(CardFlow &flow);
    bool live() const;
    JsCardNode &card_;
    JSContext *const context_;
    const JSValue description_;
    const JSValue handle_;
    JsSeatRecord *const seat_;
    Subscription *subscription_;
    CardFlow(const CardFlow &);
    CardFlow &operator=(const CardFlow &);
  };
} // namespace smirkycard
#endif
