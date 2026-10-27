#ifndef LOKA_TESTS_REQUEST_REASON_TESTS_HPP
#define LOKA_TESTS_REQUEST_REASON_TESTS_HPP
#include "app/scene/state/Request.hpp"
namespace RequestReasonTest
{
  enum FirstReason
  {
    FIRST_ACCEPTED,
    FIRST_UNAVAILABLE,
    FIRST_REFUSED
  };
  enum SecondReason
  {
    SECOND_ACCEPTED,
    SECOND_UNAVAILABLE,
    SECOND_REFUSED
  };
  template <class Reason> struct Target
  {
    int value;
    explicit Target(int v = 0)
        : value(v)
    {
    }
    static Target None()
    {
      return Target();
    }
    bool isNone() const
    {
      return this->value == 0;
    }
    bool operator!=(const Target &other) const
    {
      return this->value != other.value;
    }
  };
  typedef Target<FirstReason> First;
  typedef Target<SecondReason> Second;
  /** Deliberately has no RequestTraits: a fact does not name a refusal reason. */
  struct Fact
  {
    int value;
    explicit Fact(int v = 0)
        : value(v)
    {
    }
    bool operator!=(const Fact &other) const
    {
      return this->value != other.value;
    }
  };
  typedef Target<int> NoReason;
} // namespace RequestReasonTest
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <> struct RequestTraits<RequestReasonTest::First>
      {
        enum
        {
          coalescable = 1
        };
        typedef RequestReasonTest::FirstReason Reason;
        static Reason accepted()
        {
          return RequestReasonTest::FIRST_ACCEPTED;
        }
        static Reason unavailable()
        {
          return RequestReasonTest::FIRST_UNAVAILABLE;
        }
      };
      template <> struct RequestTraits<RequestReasonTest::Second>
      {
        enum
        {
          coalescable = 1
        };
        typedef RequestReasonTest::SecondReason Reason;
        static Reason accepted()
        {
          return RequestReasonTest::SECOND_ACCEPTED;
        }
        static Reason unavailable()
        {
          return RequestReasonTest::SECOND_UNAVAILABLE;
        }
      };
      template <> struct RequestTraits<RequestReasonTest::NoReason>
      {
        enum
        {
          coalescable = 1
        };
      };
    } // namespace scene
  } // namespace app
} // namespace loka
void testRequestReasonsStayRequestTyped();
void testRequestSingleHeterogeneousSeat();
#endif
