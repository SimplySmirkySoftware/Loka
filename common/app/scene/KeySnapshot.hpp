#ifndef LOKA_KEY_SNAPSHOT_HPP
#define LOKA_KEY_SNAPSHOT_HPP

#include "core/State.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** One key value read at construction and compared against the live key
          later. It belongs to a keyed consumer's committed result: a Boundary
          seat's committed declaration owns it, and the App's window seat holds
          it inline (KeyedSeatDesign.md). */
      template <class K> class KeySnapshot
      {
      public:
        explicit KeySnapshot(const loka::core::State<K> *key) : key_(key), value_(key->get()) {}
        KeySnapshot(const loka::core::State<K> *key, const K &value) : key_(key), value_(value) {}
        /** The captured key, independent of subsequent live-key changes. */
        const K &value() const { return this->value_; }
        bool matches() const { return this->value_ == this->key_->get(); }

      private:
        const loka::core::State<K> *key_;
        K value_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
