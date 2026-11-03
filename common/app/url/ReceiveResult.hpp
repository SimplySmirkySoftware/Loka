#ifndef LOKA_APP_URL_RECEIVE_RESULT_HPP
#define LOKA_APP_URL_RECEIVE_RESULT_HPP
namespace loka
{
  namespace app
  {
    /** Result of offering received text to an inbox. */
    enum ReceiveResult
    {
      /** In the ring; does not mean opened or handled. */
      RECEIVE_QUEUED,
      /** Invalid text or a scheme that does not match the declaration. */
      RECEIVE_INVALID,
      /** Valid text refused because the ring is full. */
      RECEIVE_FULL,
      /** No receive declaration has been installed in the App. */
      RECEIVE_NOT_REGISTERED
    };
  } // namespace app
} // namespace loka
#endif
