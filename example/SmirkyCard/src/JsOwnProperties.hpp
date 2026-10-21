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

  /** Reads a tree policy from the tree's own data property, the one its modifier
      wrote (`naturalWidth()` writes "rowWidth", `expStaticBlurUnit()` writes
      "staticBlurUnit"), so a value placed on Object.prototype is not a claim. */
  inline bool ownTreePolicy(JSContext *ctx, JSValueConst tree, const char *name)
  {
    JSAtom key = JS_NewAtom(ctx, name);
    if (key == JS_ATOM_NULL)
      return false;
    JSPropertyDescriptor property;
    const int found = JS_GetOwnProperty(ctx, &property, tree, key);
    JS_FreeAtom(ctx, key);
    if (found <= 0)
      return false;
    JS_FreeValue(ctx, property.getter);
    JS_FreeValue(ctx, property.setter);
    int32_t policy = 0;
    const bool claimed = JS_IsNumber(property.value) && JS_ToInt32(ctx, &policy, property.value) == 0 && policy == 1;
    JS_FreeValue(ctx, property.value);
    return claimed;
  }
} // namespace smirkycard

#endif
