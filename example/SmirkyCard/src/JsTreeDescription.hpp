#ifndef SMIRKYCARD_JS_TREE_DESCRIPTION_HPP
#define SMIRKYCARD_JS_TREE_DESCRIPTION_HPP

#include "quickjs.h"
#include <cassert>
#include <new>

namespace smirkycard
{
  namespace testing
  {
    class CardFlowAccess;
  }

  /** A fixed-address temporary root. Adopt consumes exactly one owned value. */
  class JsValueRoot
  {
  public:
    explicit JsValueRoot(JSContext *context, JSValue value = JS_UNDEFINED);
    ~JsValueRoot();
    JSValueConst value() const
    {
      return this->value_;
    }
    JSContext *context() const
    {
      return this->context_;
    }
    void adopt(JSValue value)
    {
      JS_FreeValue(this->context_, this->value_);
      this->value_ = value;
    }
    JSValue take()
    {
      JSValue value = this->value_;
      this->value_ = JS_UNDEFINED;
      return value;
    }

  private:
    JSContext *const context_;
    JSValue value_;
    JsValueRoot(const JsValueRoot &);
    JsValueRoot &operator=(const JsValueRoot &);
  };

  /** Slot meaning and permitted child category have one static schema owner. */
  enum JsTreeCategory
  {
    TREE_DRAWABLE,
    TREE_TOOLBAR_ITEM
  };
  enum JsTreeSlotRole
  {
    TREE_SEAT,
    TREE_CHILD,
    TREE_CHILDREN,
    TREE_HANDLER
  };
  struct JsTreeSlot
  {
    JsTreeSlotRole role;
    JsTreeCategory category;
  };

  /** Static identity for a concrete facts type and its ordered JS edges. */
  struct JsTreeSchema
  {
    const char *name;
    JsTreeCategory category;
    unsigned slotCount;
    const JsTreeSlot *slots;
    void *(*copyFacts)(JSContext *, const void *);
    void (*freeFacts)(JSRuntime *, void *);
  };

  /** Typed fact allocation shares the engine's refusing allocation boundary. */
  template <class T> struct JsTreeFactStorage
  {
    static void *copy(JSContext *context, const void *source)
    {
      void *storage = js_malloc(context, sizeof(T));
      return storage ? new (storage) T(*static_cast<const T *>(source)) : 0;
    }
    static void release(JSRuntime *runtime, void *facts)
    {
      static_cast<T *>(facts)->~T();
      js_free_rt(runtime, facts);
    }
  };

  /** Bind schema identity to its C facts type at both construction and reading. */
  template <class T> struct JsTreeSchemaFor : JsTreeSchema
  {
    JsTreeSchemaFor(const char *displayName,
                    JsTreeCategory acceptedCategory,
                    unsigned count,
                    const JsTreeSlot *orderedSlots)
    {
      this->name = displayName;
      this->category = acceptedCategory;
      this->slotCount = count;
      this->slots = orderedSlots;
      this->copyFacts = &JsTreeFactStorage<T>::copy;
      this->freeFacts = &JsTreeFactStorage<T>::release;
    }
  };

  class JsTreeStaging;
  /** Rooted read-only descriptor; facts cannot outlive this noncopyable view. */
  class JsTreeView
  {
  public:
    JsTreeView(JSContext *context, JSValueConst value);
    bool valid() const;
    const JsTreeSchema *schema() const;
    JSValueConst value() const
    {
      return this->root_.value();
    }
    JSContext *context() const
    {
      return this->root_.context();
    }
    void slot(unsigned index, JsValueRoot &out) const;
    template <class T> const T *facts(const JsTreeSchemaFor<T> &expected) const
    {
      const bool matches = this->schema() == &expected;
      assert(matches);
      return matches ? static_cast<const T *>(this->facts()) : 0;
    }
    static void finalize(JSRuntime *runtime, JSValue value);
    static void mark(JSRuntime *runtime, JSValueConst value, JS_MarkFunc *mark);

  private:
    friend class JsTreeStaging;
    friend class JsCardBindingRegistry;
    static JSClassID classId;
#ifdef TEST_BUILD
    static unsigned liveEnvelopes_;
    static unsigned liveSlots_;
#endif
    friend class testing::CardFlowAccess;
    struct Envelope;
    Envelope *envelope() const;
    const void *facts() const;
    static void release(JSRuntime *runtime, Envelope *envelope);
    JsValueRoot root_;
    JsTreeView(const JsTreeView &);
    JsTreeView &operator=(const JsTreeView &);
  };

  /** Sole owner during construction; publication transfers the complete envelope. */
  class JsTreeStaging
  {
  public:
    template <class T>
    JsTreeStaging(JSContext *context, const JsTreeSchemaFor<T> &schema, const T &facts)
        : destination_(context, JS_NewObjectClass(context, JsTreeView::classId)),
          envelope_(0),
          phase_(BUILDING)
    {
      this->initialize(schema, &facts);
    }
    explicit JsTreeStaging(const JsTreeView &source);
    ~JsTreeStaging();
    bool valid() const
    {
      return this->phase_ == BUILDING && this->envelope_ != 0;
    }
    /** Consume the value on both success and failure. */
    void slot(unsigned index, JSValue value);
    JSValue publish();

  private:
    void initialize(const JsTreeSchema &schema, const void *facts);
    enum Phase { BUILDING, REFUSED, PUBLISHED };
    JsValueRoot destination_;
    JsTreeView::Envelope *envelope_;
    Phase phase_;
    JsTreeStaging(const JsTreeStaging &);
    JsTreeStaging &operator=(const JsTreeStaging &);
  };
} // namespace smirkycard
#endif
