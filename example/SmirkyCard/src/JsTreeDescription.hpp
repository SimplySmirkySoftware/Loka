#ifndef SMIRKYCARD_JS_TREE_DESCRIPTION_HPP
#define SMIRKYCARD_JS_TREE_DESCRIPTION_HPP

#include "quickjs.h"
#include "core/String.hpp"
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

  enum JsTreeKind
  {
    TREE_CUSTOM,
    TREE_VSTACK,
    TREE_TEXT,
    TREE_EDIT,
    TREE_BUTTON,
    TREE_ROW,
    TREE_MARKUP,
    TREE_CELL,
    TREE_GRID,
    TREE_SHOW,
    TREE_FILE,
    TREE_IMAGE,
    TREE_SCROLL,
    TREE_CARD,
    TREE_ZSTACK,
    TREE_BOX,
    TREE_CLIPBOX,
    TREE_TOOLBAR,
    TREE_ITEM_BUTTON,
    TREE_ITEM_LABEL,
    TREE_ITEM_FLEX,
    TREE_ITEM_FIXED,
    TREE_KIND_COUNT
  };
  enum JsTreeEdge
  {
    EDGE_SEAT,
    EDGE_TEXT,
    EDGE_LABEL,
    EDGE_ENABLED,
    EDGE_CHILD,
    EDGE_CHILDREN,
    EDGE_HANDLER,
    EDGE_VIEWPORT
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
    JsTreeEdge edge;
    JsTreeSlotRole role;
    JsTreeCategory category;
  };

  /** Static identity for a concrete facts type and its ordered JS edges. */
  struct JsTreeSchema
  {
    const char *name;
    JsTreeKind kind;
    const loka::core::String *(*readId)(const void *);
    void (*writeId)(void *, const loka::core::String &);
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
                    const JsTreeSlot *orderedSlots,
                    JsTreeKind kind = TREE_CUSTOM,
                    const loka::core::String *(*readId)(const void *) = 0,
                    void (*writeId)(void *, const loka::core::String &) = 0)
    {
      this->name = displayName;
      this->kind = kind;
      this->readId = readId;
      this->writeId = writeId;
      this->category = acceptedCategory;
      this->slotCount = count;
      this->slots = orderedSlots;
      this->copyFacts = &JsTreeFactStorage<T>::copy;
      this->freeFacts = &JsTreeFactStorage<T>::release;
    }
  };

  /** Normalize a literal once; nonempty native String refusal becomes OOM. */
  bool ReadJsTreeString(JSContext *ctx, JSValueConst value, loka::core::String &out, bool rejectNul = false);
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
    void edge(JsTreeEdge edge, JsValueRoot &out) const;
    unsigned childCount() const;
    void childAt(unsigned index, JsValueRoot &out) const;
    const loka::core::String *testId() const;
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
    void edge(JsTreeEdge edge, JSValue value);
    void copySlots(const JsTreeView &source);
    void testId(const loka::core::String &id);
    template <class T> T *facts(const JsTreeSchemaFor<T> &schema)
    {
      return this->valid() ? static_cast<T *>(this->mutableFacts(schema)) : 0;
    }

  private:
    void initialize(const JsTreeSchema &schema, const void *facts);
    void *mutableFacts(const JsTreeSchema &schema);
    enum Phase
    {
      BUILDING,
      REFUSED,
      PUBLISHED
    };
    JsValueRoot destination_;
    JsTreeView::Envelope *envelope_;
    Phase phase_;
    JsTreeStaging(const JsTreeStaging &);
    JsTreeStaging &operator=(const JsTreeStaging &);
  };
} // namespace smirkycard
#endif
