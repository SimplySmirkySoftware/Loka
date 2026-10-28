#ifndef LOKA_TOOLBOX_DRAW_MEASUREMENT_HPP
#define LOKA_TOOLBOX_DRAW_MEASUREMENT_HPP

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    /** Numeric completed fact; never retains a window, node, or native context. */
    struct WholeDrawSample
    {
      unsigned long paintUs;
      unsigned long layoutUs;
      bool complete;
      WholeDrawSample(unsigned long paint, unsigned long layout, bool valid)
          : paintUs(paint),
            layoutUs(layout),
            complete(valid)
      {
      }
    };
    /** Fixture-owned receiver, borrowed only while its scoped installation lives.
        now() must be an allocation-free clock read; accept() runs after end read. */
    class WholeDrawReceiver
    {
    public:
      virtual ~WholeDrawReceiver() {}
      virtual unsigned long now() const = 0;
      virtual void expectDraw() = 0;
      virtual void accept(const WholeDrawSample &sample) = 0;
    };
    inline WholeDrawReceiver *&WholeDrawReceiverSlot()
    {
      static WholeDrawReceiver *receiver = 0;
      return receiver;
    }
    /** Install after App construction; destroy before App/window destruction. */
    class WholeDrawReceiverScope
    {
    public:
      explicit WholeDrawReceiverScope(WholeDrawReceiver &receiver)
          : previous_(WholeDrawReceiverSlot())
      {
        WholeDrawReceiverSlot() = &receiver;
      }
      ~WholeDrawReceiverScope()
      {
        WholeDrawReceiverSlot() = this->previous_;
      }

    private:
      WholeDrawReceiver *previous_;
      WholeDrawReceiverScope(const WholeDrawReceiverScope &);
      WholeDrawReceiverScope &operator=(const WholeDrawReceiverScope &);
    };
    /** One stack-owned draw. Layout and erase have independent clock intervals. */
    class WholeDrawBracket
    {
    public:
      WholeDrawBracket()
          : receiver_(WholeDrawReceiverSlot()),
            phase_(EMPTY),
            start_(0),
            layoutStart_(0),
            layoutUs_(0)
      {
        if (this->receiver_)
          this->receiver_->expectDraw();
      }
      void beginLayout()
      {
        if (this->receiver_)
        {
          this->layoutStart_ = this->receiver_->now();
          this->phase_ = LAYOUT;
        }
      }
      void endLayout()
      {
        if (this->receiver_ && this->phase_ == LAYOUT)
        {
          this->layoutUs_ = this->receiver_->now() - this->layoutStart_;
          this->phase_ = LAID_OUT;
        }
      }
      void beginErase()
      {
        if (this->receiver_)
        {
          this->start_ = this->receiver_->now();
          this->phase_ = this->phase_ == LAID_OUT ? PAINT : INVALID;
        }
      }
      void finish()
      {
        if (!this->receiver_)
          return;
        const unsigned long end = this->receiver_->now();
        this->receiver_->accept(WholeDrawSample(end - this->start_, this->layoutUs_, this->phase_ == PAINT));
        this->phase_ = EMPTY;
      }

    private:
      WholeDrawReceiver *receiver_;
      enum Phase
      {
        EMPTY,
        LAYOUT,
        LAID_OUT,
        PAINT,
        INVALID
      };
      Phase phase_;
      unsigned long start_;
      unsigned long layoutStart_;
      unsigned long layoutUs_;
    };
  } // namespace testing
} // namespace loka
#endif
#endif
