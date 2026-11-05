#ifndef SMIRKYCARD_JS_OWN_PROPERTIES_HPP
#define SMIRKYCARD_JS_OWN_PROPERTIES_HPP

#include "quickjs.h"

namespace smirkycard
{
  /** Stack owner of QuickJS's property enumeration and its atom references. */
  class JsOwnProperties
  {
  public:
    explicit JsOwnProperties(JSContext *context)
        : context_(context),
          entries_(0),
          count_(0)
    {
    }
    ~JsOwnProperties()
    {
      JS_FreePropertyEnum(this->context_, this->entries_, this->count_);
    }

    bool read(JSValueConst object, int flags)
    {
      JS_FreePropertyEnum(this->context_, this->entries_, this->count_);
      this->entries_ = 0;
      this->count_ = 0;
      return JS_GetOwnPropertyNames(this->context_, &this->entries_, &this->count_, object, flags) == 0;
    }
    uint32_t count() const
    {
      return this->count_;
    }
    JSAtom atom(uint32_t index) const
    {
      return this->entries_[index].atom;
    }

  private:
    JSContext *context_;
    JSPropertyEnum *entries_;
    uint32_t count_;
    JsOwnProperties(const JsOwnProperties &);
    JsOwnProperties &operator=(const JsOwnProperties &);
  };

} // namespace smirkycard

#endif
