#include "JsTreeDescription.hpp"
#include <cstring>

namespace smirkycard
{
#ifdef TEST_BUILD
  unsigned JsTreeView::liveEnvelopes_ = 0;
  unsigned JsTreeView::liveSlots_ = 0;
#endif
  bool ReadJsTreeString(JSContext *ctx, JSValueConst value, loka::core::String &out, bool rejectNul)
  {
    if (!JS_IsString(value))
    {
      JS_ThrowTypeError(ctx, "Expected a string");
      return false;
    }
    size_t length = 0;
    const char *bytes = JS_ToCStringLen(ctx, &length, value);
    if (!bytes)
      return false;
    if (rejectNul && std::strlen(bytes) != length)
    {
      JS_FreeCString(ctx, bytes);
      JS_ThrowTypeError(ctx, "Invalid ToolbarItem payload");
      return false;
    }
    out = loka::core::String::Utf8(bytes, length);
    JS_FreeCString(ctx, bytes);
    if (length && out.empty())
    {
      JS_ThrowOutOfMemory(ctx);
      return false;
    }
    return true;
  }
  JsValueRoot::JsValueRoot(JSContext *context, JSValue value)
      : context_(context),
        value_(value)
  {
  }
  JsValueRoot::~JsValueRoot()
  {
    JS_FreeValue(this->context_, this->value_);
  }
  /** JS owns these edges; they are traced edges, never permanent external roots. */
  struct JsTreeView::Envelope
  {
    const JsTreeSchema *schema;
    void *facts;
    /** Values are individually constructed in aligned trailing storage, not
        indexed beyond a declared one-element array. Their addresses stay fixed. */
    JSValue *slot(unsigned index)
    {
      return reinterpret_cast<JSValue *>(reinterpret_cast<unsigned char *>(this) + sizeof(Envelope)
                                         + index * sizeof(JSValue));
    }
    const JSValue *slot(unsigned index) const
    {
      return reinterpret_cast<const JSValue *>(reinterpret_cast<const unsigned char *>(this) + sizeof(Envelope)
                                               + index * sizeof(JSValue));
    }
  };

  JSClassID JsTreeView::classId = 0;

  JsTreeView::JsTreeView(JSContext *context, JSValueConst value)
      : root_(context, JS_DupValue(context, value))
  {
  }

  JsTreeView::Envelope *JsTreeView::envelope() const
  {
    if (!classId || JS_GetClassID(this->root_.value()) != classId)
      return 0;
    return static_cast<Envelope *>(JS_GetOpaque(this->root_.value(), classId));
  }
  bool JsTreeView::valid() const
  {
    const Envelope *envelope = this->envelope();
    return envelope && envelope->schema && envelope->facts;
  }
  const JsTreeSchema *JsTreeView::schema() const
  {
    return this->valid() ? this->envelope()->schema : 0;
  }
  const void *JsTreeView::facts() const
  {
    assert(this->valid());
    return this->envelope()->facts;
  }
  void JsTreeView::slot(unsigned index, JsValueRoot &out) const
  {
    assert(this->valid() && index < this->schema()->slotCount && out.context() == this->context());
    out.adopt(JS_DupValue(this->context(), *this->envelope()->slot(index)));
  }
  void JsTreeView::edge(JsTreeEdge edge, JsValueRoot &out) const
  {
    assert(this->valid());
    for (unsigned i = 0; i < this->schema()->slotCount; ++i)
      if (this->schema()->slots[i].edge == edge)
      {
        this->slot(i, out);
        return;
      }
    out.adopt(JS_UNDEFINED);
  }
  unsigned JsTreeView::childCount() const
  {
    JsValueRoot children(this->context());
    this->edge(EDGE_CHILDREN, children);
    int64_t count = 0;
    if (!JS_IsUndefined(children.value()))
    {
      const int result = JS_GetLength(this->context(), children.value(), &count);
      assert(result == 0);
      (void)result;
    }
    return static_cast<unsigned>(count);
  }
  void JsTreeView::childAt(unsigned index, JsValueRoot &out) const
  {
    JsValueRoot children(this->context());
    this->edge(EDGE_CHILDREN, children);
    out.adopt(JS_GetPropertyUint32(this->context(), children.value(), index));
  }
  const loka::core::String *JsTreeView::testId() const
  {
    return this->schema()->readId ? this->schema()->readId(this->facts()) : 0;
  }
  void JsTreeView::release(JSRuntime *runtime, Envelope *envelope)
  {
    if (!envelope)
      return;
    for (unsigned i = 0; i < envelope->schema->slotCount; ++i)
      JS_FreeValueRT(runtime, *envelope->slot(i));
    if (envelope->facts)
      envelope->schema->freeFacts(runtime, envelope->facts);
#ifdef TEST_BUILD
    --liveEnvelopes_;
    liveSlots_ -= envelope->schema->slotCount;
#endif
    js_free_rt(runtime, envelope);
  }
  void JsTreeView::finalize(JSRuntime *runtime, JSValue value)
  {
    release(runtime, static_cast<Envelope *>(JS_GetOpaque(value, classId)));
  }
  void JsTreeView::mark(JSRuntime *runtime, JSValueConst value, JS_MarkFunc *mark)
  {
    const Envelope *envelope = static_cast<Envelope *>(JS_GetOpaque(value, classId));
    if (envelope)
      for (unsigned i = 0; i < envelope->schema->slotCount; ++i)
        JS_MarkValue(runtime, *envelope->slot(i), mark);
  }
  void JsTreeStaging::initialize(const JsTreeSchema &schema, const void *facts)
  {
    if (JS_IsException(this->destination_.value()))
    {
      this->phase_ = REFUSED;
      return;
    }
#if __cplusplus >= 201103L
    static_assert(sizeof(JsTreeView::Envelope) % alignof(JSValue) == 0, "Envelope tail must align JSValue");
#endif
    const size_t extra = schema.slotCount * sizeof(JSValue);
    this->envelope_ = static_cast<JsTreeView::Envelope *>(
        js_malloc(this->destination_.context(), sizeof(JsTreeView::Envelope) + extra));
    if (!this->envelope_)
    {
      this->phase_ = REFUSED;
      return;
    }
    new (this->envelope_) JsTreeView::Envelope;
#ifdef TEST_BUILD
    ++JsTreeView::liveEnvelopes_;
    JsTreeView::liveSlots_ += schema.slotCount;
#endif
    this->envelope_->schema = &schema;
    this->envelope_->facts = 0;
    for (unsigned i = 0; i < schema.slotCount; ++i)
    {
      new (this->envelope_->slot(i)) JSValue(JS_UNDEFINED);
    }
    this->envelope_->facts = schema.copyFacts(this->destination_.context(), facts);
    if (!this->envelope_->facts)
    {
      this->phase_ = REFUSED;
    }
  }
  JsTreeStaging::JsTreeStaging(const JsTreeView &source)
      : destination_(source.context(), JS_NewObjectClass(source.context(), JsTreeView::classId)),
        envelope_(0),
        phase_(BUILDING)
  {
    assert(source.valid());
    this->initialize(*source.schema(), source.facts());
    this->copySlots(source);
  }
  JsTreeStaging::~JsTreeStaging()
  {
    JsTreeView::release(JS_GetRuntime(this->destination_.context()), this->envelope_);
  }
  void JsTreeStaging::slot(unsigned index, JSValue value)
  {
    if (JS_IsException(value))
    {
      this->phase_ = REFUSED;
      return;
    }
    if (!this->valid())
    {
      JS_FreeValue(this->destination_.context(), value);
      return;
    }
    assert(index < this->envelope_->schema->slotCount);
    JS_FreeValue(this->destination_.context(), *this->envelope_->slot(index));
    *this->envelope_->slot(index) = value;
  }
  void *JsTreeStaging::mutableFacts(const JsTreeSchema &schema)
  {
    assert(this->envelope_ && this->envelope_->schema == &schema);
    return this->envelope_ && this->envelope_->schema == &schema ? this->envelope_->facts : 0;
  }
  void JsTreeStaging::edge(JsTreeEdge edge, JSValue value)
  {
    if (this->envelope_)
      for (unsigned i = 0; i < this->envelope_->schema->slotCount; ++i)
        if (this->envelope_->schema->slots[i].edge == edge)
        {
          this->slot(i, value);
          return;
        }
    JS_FreeValue(this->destination_.context(), value);
    if (this->valid())
    {
      assert(false);
      this->phase_ = REFUSED;
    }
  }
  void JsTreeStaging::copySlots(const JsTreeView &source)
  {
    if (!this->valid())
      return;
    assert(this->envelope_->schema == source.schema());
    for (unsigned i = 0; i < source.schema()->slotCount; ++i)
    {
      JsValueRoot edge(source.context());
      source.slot(i, edge);
      this->slot(i, edge.take());
    }
  }
  void JsTreeStaging::testId(const loka::core::String &id)
  {
    if (this->valid())
    {
      assert(this->envelope_->schema->writeId);
      this->envelope_->schema->writeId(this->envelope_->facts, id);
    }
  }
  JSValue JsTreeStaging::publish()
  {
    if (!this->valid())
      return JS_EXCEPTION;
    if (JS_IsException(this->destination_.value())
        || JS_FreezeObject(this->destination_.context(), this->destination_.value()) < 0)
      return JS_EXCEPTION;
    if (JS_GetClassID(this->destination_.value()) != JsTreeView::classId)
      return JS_ThrowInternalError(this->destination_.context(), "Tree publication class mismatch");
    const int attached = JS_SetOpaque(this->destination_.value(), this->envelope_);
    assert(attached == 0);
    if (attached != 0)
      return JS_ThrowInternalError(this->destination_.context(), "Tree publication refused");
    this->phase_ = PUBLISHED;
    this->envelope_ = 0;
    return this->destination_.take();
  }
} // namespace smirkycard
