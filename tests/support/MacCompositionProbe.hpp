#ifndef LOKA_TESTS_MAC_COMPOSITION_PROBE_HPP
#define LOKA_TESTS_MAC_COMPOSITION_PROBE_HPP
class MacTextStore;
namespace loka
{
  namespace macos
  {
    namespace testing
    {
      /** Consume the observer before entering its synchronous character-edit callback. */
      void onNextCharacterEdit(const MacTextStore *row, void (*callback)(void *), void *context);
    } // namespace testing
  } // namespace macos
} // namespace loka
#endif
