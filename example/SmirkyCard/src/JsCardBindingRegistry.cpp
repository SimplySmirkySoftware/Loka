#include "JsCardBindingRegistry.hpp"
#include "JsTreeDescription.hpp"
#include "JsTreeFacts.hpp"
#include "CardNodes.hpp"
#include "JsToolbarNode.hpp"
#include <climits>
#include "SmirkyMarkup.hpp"
#include "JsOwnProperties.hpp"
#include "JsNativeClass.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include <cstring>
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include <new>

namespace smirkycard
{
  struct JsCardBindingRegistry::LoweringEntry
  {
    LoweringEntry(IJsNodeLowering *value)
        : value_(value),
          next_(0)
    {
    }
    IJsNodeLowering *value_;
    LoweringEntry *next_;
  };
  struct JsCardBindingRegistry::GlobalEntry
  {
    GlobalEntry(const IJsGlobal &value)
        : value_(value),
          next_(0)
    {
    }
    IJsGlobal value_;
    GlobalEntry *next_;
  };
  JsCardBindingRegistry::~JsCardBindingRegistry()
  {
    while (lowerings_)
    {
      LoweringEntry *next = lowerings_->next_;
      delete lowerings_->value_;
      delete lowerings_;
      lowerings_ = next;
    }
    while (globals_)
    {
      GlobalEntry *next = globals_->next_;
      delete globals_;
      globals_ = next;
    }
  }
  bool JsCardBindingRegistry::registerLowering(IJsNodeLowering *lowering)
  {
    if (!lowering || !lowering->name || !lowering->schema || lowering->kind)
      return false;
    for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
    {
      const char *parent = entry->value_->parent;
      const bool sameParent =
          parent && lowering->parent ? !strcmp(parent, lowering->parent) : parent == lowering->parent;
      if (entry->value_->kind == nextKind_ || (sameParent && !strcmp(entry->value_->name, lowering->name)))
        return false;
    }
    if (!lowering->parent)
      for (GlobalEntry *entry = globals_; entry; entry = entry->next_)
        if (!strcmp(entry->value_.name, lowering->name))
          return false;
    LoweringEntry *entry = new (std::nothrow) LoweringEntry(lowering);
    if (!entry)
      return false;
    lowering->kind = nextKind_++;
    entry->next_ = lowerings_;
    lowerings_ = entry;
    return true;
  }
  bool JsCardBindingRegistry::registerGlobal(const IJsGlobal &global)
  {
    if (!global.name || !global.fn || global.length < 0)
      return false;
    for (GlobalEntry *entry = globals_; entry; entry = entry->next_)
      if (!strcmp(entry->value_.name, global.name))
        return false;
    for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
      if (!entry->value_->parent && !strcmp(entry->value_->name, global.name))
        return false;
    GlobalEntry *entry = new (std::nothrow) GlobalEntry(global);
    if (!entry)
      return false;
    entry->next_ = globals_;
    globals_ = entry;
    return true;
  }
  IJsNodeLowering *JsCardBindingRegistry::lookup(int kind) const
  {
    for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
      if (entry->value_->kind == kind)
        return entry->value_;
    return 0;
  }
  IJsNodeLowering *JsCardBindingRegistry::lookup(const JsTreeSchema *schema) const
  {
    for (LoweringEntry *entry = this->lowerings_; entry; entry = entry->next_)
      if (entry->value_->schema == schema)
        return entry->value_;
    return 0;
  }
  bool JsCardBindingRegistry::install(JSContext *context) const
  {
    if (!installTreeClass(context))
      return false;
    JSValue global = JS_GetGlobalObject(context);
    if (JS_IsException(global))
      return false;
    for (int pass = 0; pass < 2; ++pass)
    {
      for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
      {
        const IJsNodeLowering &lowering = *entry->value_;
        if ((lowering.parent != 0) != (pass == 1))
          continue;
        JSValue target =
            lowering.parent ? JS_GetPropertyStr(context, global, lowering.parent) : JS_DupValue(context, global);
        if (JS_IsException(target) || (lowering.parent && !JS_IsFunction(context, target)))
        {
          JS_FreeValue(context, target);
          JS_FreeValue(context, global);
          return false;
        }
        JSValue fn = JS_NewCFunctionMagic(
            context, &JsCardBindingRegistry::build, lowering.name, 1, JS_CFUNC_generic_magic, lowering.kind);
        const bool ok = !JS_IsException(fn) && JS_SetPropertyStr(context, target, lowering.name, fn) >= 0;
        JS_FreeValue(context, target);
        if (!ok)
        {
          JS_FreeValue(context, global);
          return false;
        }
      }
      if (pass == 0)
        for (GlobalEntry *entry = globals_; entry; entry = entry->next_)
        {
          JSValue fn = JS_NewCFunction(context, entry->value_.fn, entry->value_.name, entry->value_.length);
          if (JS_IsException(fn) || JS_SetPropertyStr(context, global, entry->value_.name, fn) < 0)
          {
            JS_FreeValue(context, global);
            return false;
          }
        }
    }
    JS_FreeValue(context, global);
    return true;
  }
  JSValue JsCardBindingRegistry::build(JSContext *context, JSValueConst, int argc, JSValueConst *argv, int magic)
  {
    ScriptRuntime *runtime = static_cast<ScriptRuntime *>(JS_GetContextOpaque(context));
    IJsNodeLowering *lowering = runtime ? runtime->registry_.lookup(magic) : 0;
    return lowering ? lowering->build(context, argc, argv) : JS_ThrowTypeError(context, "unknown tree helper");
  }

  bool JsCardBindingRegistry::installTreeClass(JSContext *ctx)
  {
    JSClassDef definition = {};
    definition.class_name = "SmirkyTree";
    definition.finalizer = &JsTreeView::finalize;
    definition.gc_mark = &JsTreeView::mark;
    if (!RegisterJsNativeClass(JS_GetRuntime(ctx), JsTreeView::classId, definition))
      return false;
    JSValue proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
      return false;
    const IJsGlobal methods[] = {{"TEST_ID", &ScriptRuntime::testId, 1},
                                 {"naturalWidth", &ScriptRuntime::naturalWidth, 0},
                                 {"naturalHeight", &ScriptRuntime::naturalHeight, 0},
                                 {"expStaticBlurUnit", &ScriptRuntime::expStaticBlurUnit, 0},
                                 {"expBlurUnit", &ScriptRuntime::expBlurUnit, 0},
                                 {"enabled", &ScriptRuntime::enabled, 1}};
    for (unsigned i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
    {
      const IJsGlobal &method = methods[i];
      JSValue function = JS_NewCFunction(ctx, method.fn, method.name, method.length);
      if (JS_IsException(function) || JS_FreezeObject(ctx, function) < 0)
      {
        JS_FreeValue(ctx, function);
        JS_FreeValue(ctx, proto);
        return false;
      }
      if (JS_DefinePropertyValueStr(ctx, proto, method.name, function, 0) < 0)
      {
        JS_FreeValue(ctx, proto);
        return false;
      }
    }
    if (JS_FreezeObject(ctx, proto) < 0)
    {
      JS_FreeValue(ctx, proto);
      return false;
    }
    // The context's class-proto table takes ownership; this API cannot fail.
    JS_SetClassProto(ctx, JsTreeView::classId, proto);
    return true;
  }

  namespace
  {
    bool readGridDimension(JSContext *ctx, JSValueConst value, short &out)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || !(number >= 1 && number <= 16)
          || number != static_cast<short>(number))
        return false;
      out = static_cast<short>(number);
      return true;
    }
    bool readMarkup(JSContext *ctx, JSValueConst value, JSValueConst dict, loka::app::AttributedString &out)
    {
      if (!JS_IsString(value))
      {
        JS_ThrowTypeError(ctx, "Markup requires a literal string; state seats are not supported");
        return false;
      }
      loka::app::TextStyle style;
      if (JS_IsException(dict) || (!JS_IsUndefined(dict) && !readTextStyle(ctx, dict, style)))
        return false;
      size_t length = 0;
      const char *bytes = JS_ToCStringLen(ctx, &length, value);
      if (!bytes)
        return false;
      const bool valid = ParseSmirkyMarkup(bytes, length, style, out);
      JS_FreeCString(ctx, bytes);
      if (!valid)
        JS_ThrowTypeError(ctx, "Markup parse or allocation refused");
      return valid;
    }

    bool scrollInsetNumber(JSContext *ctx, JSValueConst value, int &out)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || !(number >= 0 && number <= SHRT_MAX)
          || number != static_cast<int>(number))
        return false;
      out = static_cast<int>(number);
      return true;
    }
    bool readScrollInsets(JSContext *ctx, JSValueConst object, int &top, int &bottom)
    {
      if (!requirePlainObject(ctx, object, "ScrollView insets"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(object, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSValue key = JS_AtomToValue(ctx, names.atom(i));
        size_t length = 0;
        const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
        JS_FreeValue(ctx, key);
        const bool isTop = name && length == 3 && !memcmp(name, "top", 3);
        const bool isBottom = name && length == 6 && !memcmp(name, "bottom", 6);
        if (name)
          JS_FreeCString(ctx, name);
        if (!isTop && !isBottom)
        {
          JS_ThrowTypeError(ctx, "ScrollView insets accept only top and bottom");
          return false;
        }
        JSValue value = JS_GetProperty(ctx, object, names.atom(i));
        if (JS_IsException(value))
          return false;
        int number = 0;
        const bool valid = scrollInsetNumber(ctx, value, number);
        JS_FreeValue(ctx, value);
        if (!valid)
        {
          JS_ThrowTypeError(ctx, "ScrollView insets must be integers in 0..32767");
          return false;
        }
        (isTop ? top : bottom) = number;
      }
      return true;
    }
    bool boxExtentNumber(JSContext *ctx, JSValueConst value, int &out, int minimum = 1)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || !(number >= minimum && number <= 4096)
          || number != static_cast<int>(number))
        return false;
      out = static_cast<int>(number);
      return true;
    }
    bool boxPaddingValue(JSContext *ctx, JSValueConst value, loka::app::layout::LayoutGap &out)
    {
      if (!JS_IsString(value))
      {
        int number = 0;
        if (!boxExtentNumber(ctx, value, number, 0))
          return false;
        out = loka::app::layout::LayoutGap::of(number);
        return true;
      }
      size_t length = 0;
      const char *text = JS_ToCStringLen(ctx, &length, value);
      const bool standard = text && length == 8 && !memcmp(text, "standard", 8);
      if (text)
        JS_FreeCString(ctx, text);
      if (standard)
        out = loka::app::layout::LayoutGap::standardWindowMargin();
      return standard;
    }
    bool textValue(JSContext *ctx,
                   JSValueConst value,
                   JsTreeTextSource &source,
                   loka::core::String &text,
                   bool rejectNul = false,
                   bool literalOnly = false)
    {
      if (JS_IsString(value))
      {
        source = TREE_LITERAL;
        return ReadJsTreeString(ctx, value, text, rejectNul);
      }
      if (!literalOnly && JS_IsObject(value))
      {
        source = TREE_LIVE_SEAT;
        return true;
      }
      JS_ThrowTypeError(ctx, "Tree text requires a literal string or state seat");
      return false;
    }
    bool childValue(JSContext *ctx, JSValueConst value, JsTreeCategory category)
    {
      JsTreeView child(ctx, value);
      if (child.valid() && child.schema()->category == category)
        return true;
      JS_ThrowTypeError(ctx,
                        category == TREE_TOOLBAR_ITEM
                            ? "Toolbar requires ToolbarItem descriptors"
                            : "Child must be a drawable tree (nested arrays are not allowed)");
      return false;
    }
    bool seatValue(JSContext *ctx, JSValueConst value)
    {
      if (JS_IsObject(value))
        return true;
      JS_ThrowTypeError(ctx, "Tree requires a state seat object");
      return false;
    }
    bool collectionArray(JSContext *ctx, JSValueConst input)
    {
      JsValueRoot target(ctx, JS_DupValue(ctx, input));
      while (JS_IsProxy(target.value()))
        target.adopt(JS_GetProxyTarget(ctx, target.value()));
      return JS_IsArray(target.value());
    }
    JSValue collectChildren(JSContext *ctx,
                            const char *name,
                            int argc,
                            JSValueConst *argv,
                            unsigned limit,
                            bool exact,
                            JsTreeCategory category)
    {
      JsValueRoot children(ctx, JS_NewArray(ctx));
      if (JS_IsException(children.value()) || JS_SetPrototype(ctx, children.value(), JS_NULL) < 0)
        return JS_EXCEPTION;
      unsigned count = 0;
      for (int i = 0; i < argc; ++i)
      {
        JsValueRoot input(ctx, JS_DupValue(ctx, argv[i]));
        const bool array = collectionArray(ctx, input.value());
        int64_t length = 1;
        if (array && JS_GetLength(ctx, input.value(), &length) < 0)
          return JS_EXCEPTION;
        if (length < 0 || length > static_cast<int64_t>(limit - count))
          return category == TREE_TOOLBAR_ITEM
                     ? JS_ThrowTypeError(ctx, "Toolbar accepts at most 16 descriptors")
                     : (exact ? JS_ThrowRangeError(ctx, "%s requires exactly rows * cols children (%u)", name, limit)
                              : JS_ThrowRangeError(ctx, "%s accepts at most %u children", name, limit));
        for (int64_t j = 0; j < length; ++j)
        {
          JsValueRoot child(ctx,
                            array ? JS_GetPropertyUint32(ctx, input.value(), static_cast<uint32_t>(j))
                                  : JS_DupValue(ctx, input.value()));
          if (JS_IsException(child.value()))
            return JS_EXCEPTION;
          if (!childValue(ctx, child.value(), category))
            return JS_EXCEPTION;
          if (JS_DefinePropertyValueUint32(ctx, children.value(), count++, child.take(), JS_PROP_C_W_E) < 0)
            return JS_EXCEPTION;
        }
      }
      if (exact && count != limit)
        return JS_ThrowRangeError(ctx, "%s requires exactly rows * cols children (%u)", name, limit);
      if (JS_FreezeObject(ctx, children.value()) < 0)
        return JS_EXCEPTION;
      return children.take();
    }
    template <class T>
    JSValue childrenTree(JSContext *ctx,
                         int argc,
                         JSValueConst *argv,
                         const T &facts,
                         unsigned limit = kMaximumTreeChildren,
                         bool exact = false)
    {
      JsValueRoot children(ctx,
                           collectChildren(ctx,
                                           T::schema.name,
                                           argc,
                                           argv,
                                           limit,
                                           exact,
                                           T::schema.kind == TREE_TOOLBAR ? TREE_TOOLBAR_ITEM : TREE_DRAWABLE));
      if (JS_IsException(children.value()))
        return JS_EXCEPTION;
      if (T::schema.kind == TREE_TOOLBAR)
      {
        int64_t length = 0;
        if (JS_GetLength(ctx, children.value(), &length) < 0)
          return JS_EXCEPTION;
        unsigned buttons = 0;
        for (int64_t i = 0; i < length; ++i)
        {
          JsValueRoot value(ctx, JS_GetPropertyUint32(ctx, children.value(), static_cast<uint32_t>(i)));
          JsTreeView item(ctx, value.value());
          if (item.schema()->kind == TREE_ITEM_BUTTON && ++buttons > JS_TOOLBAR_SLOT_COUNT)
            return JS_ThrowTypeError(ctx, "Toolbar accepts at most eight buttons");
        }
      }
      JsTreeStaging staging(ctx, T::schema, facts);
      staging.edge(EDGE_CHILDREN, children.take());
      return staging.publish();
    }
    template <class T> JSValue seatTree(JSContext *ctx, JSValueConst seat, const T &facts)
    {
      if (!seatValue(ctx, seat))
        return JS_EXCEPTION;
      JsTreeStaging staging(ctx, T::schema, facts);
      staging.edge(EDGE_SEAT, JS_DupValue(ctx, seat));
      return staging.publish();
    }
    template <class T> JSValue singleTree(JSContext *ctx, JSValueConst child, const T &facts)
    {
      JsTreeStaging staging(ctx, T::schema, facts);
      staging.edge(EDGE_CHILD, JS_DupValue(ctx, child));
      return staging.publish();
    }
    template <class T> bool boxOptions(JSContext *ctx, JSValueConst dict, T &facts)
    {
      if (!requirePlainObject(ctx, dict, T::schema.kind == TREE_BOX ? "Box options" : "ClipBox options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(dict, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (unsigned i = 0; i < names.count(); ++i)
      {
        JsValueRoot key(ctx, JS_AtomToValue(ctx, names.atom(i)));
        if (JS_IsException(key.value()))
          return false;
        size_t keyLength = 0;
        const char *name = JS_IsString(key.value()) ? JS_ToCStringLen(ctx, &keyLength, key.value()) : 0;
        if (JS_IsString(key.value()) && !name)
          return false;
        const bool width = name && strlen(name) == keyLength && !strcmp(name, "width"),
                   height = name && strlen(name) == keyLength && !strcmp(name, "height"),
                   padding = name && strlen(name) == keyLength && !strcmp(name, "padding");
        if (name)
          JS_FreeCString(ctx, name);
        if (!width && !height && !padding)
        {
          JS_ThrowTypeError(ctx, "%s options accept only width, height and padding", T::schema.name);
          return false;
        }
        JsValueRoot value(ctx, JS_GetProperty(ctx, dict, names.atom(i)));
        if (JS_IsException(value.value()))
          return false;
        if (!(padding ? boxPaddingValue(ctx, value.value(), facts.padding)
                      : boxExtentNumber(ctx, value.value(), width ? facts.width : facts.height)))
        {
          JS_ThrowTypeError(ctx, "%s has invalid width, height or padding", T::schema.name);
          return false;
        }
      }
      return true;
    }
    bool scrollOptions(JSContext *ctx, JSValueConst dict, ScrollFacts &facts, JsValueRoot &viewport)
    {
      if (!requirePlainObject(ctx, dict, "ScrollView options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(dict, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (unsigned i = 0; i < names.count(); ++i)
      {
        JsValueRoot key(ctx, JS_AtomToValue(ctx, names.atom(i)));
        if (JS_IsException(key.value()))
          return false;
        size_t keyLength = 0;
        const char *name = JS_IsString(key.value()) ? JS_ToCStringLen(ctx, &keyLength, key.value()) : 0;
        if (JS_IsString(key.value()) && !name)
          return false;
        const bool insets = name && strlen(name) == keyLength && !strcmp(name, "insets"),
                   vp = name && strlen(name) == keyLength && !strcmp(name, "viewport");
        if (name)
          JS_FreeCString(ctx, name);
        if (!insets && !vp)
        {
          JS_ThrowTypeError(ctx, "ScrollView options accept only insets and viewport");
          return false;
        }
        JsValueRoot value(ctx, JS_GetProperty(ctx, dict, names.atom(i)));
        if (JS_IsException(value.value()))
          return false;
        if (insets)
        {
          if (!readScrollInsets(ctx, value.value(), facts.insetTop, facts.insetBottom))
            return false;
        }
        else
        {
          if (!seatValue(ctx, value.value()))
            return false;
          viewport.adopt(value.take());
        }
      }
      return true;
    }
    bool imageOptions(JSContext *ctx, JSValueConst dict, ImageFacts &facts)
    {
      if (!requirePlainObject(ctx, dict, "ImageView options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(dict, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (unsigned i = 0; i < names.count(); ++i)
      {
        JsValueRoot key(ctx, JS_AtomToValue(ctx, names.atom(i)));
        if (JS_IsException(key.value()))
          return false;
        size_t keyLength = 0;
        const char *name = JS_IsString(key.value()) ? JS_ToCStringLen(ctx, &keyLength, key.value()) : 0;
        if (JS_IsString(key.value()) && !name)
          return false;
        const bool width = name && strlen(name) == keyLength && !strcmp(name, "width"),
                   height = name && strlen(name) == keyLength && !strcmp(name, "height"),
                   fit = name && strlen(name) == keyLength && !strcmp(name, "fit");
        if (name)
          JS_FreeCString(ctx, name);
        if (!width && !height && !fit)
        {
          JS_ThrowTypeError(ctx, "Unknown ImageView options key");
          return false;
        }
        JsValueRoot value(ctx, JS_GetProperty(ctx, dict, names.atom(i)));
        if (JS_IsException(value.value()))
          return false;
        if (fit)
        {
          size_t n = 0;
          const char *text = JS_IsString(value.value()) ? JS_ToCStringLen(ctx, &n, value.value()) : 0;
          bool ok = false;
          if (JS_IsString(value.value()) && !text)
            return false;
          if (text)
          {
            if (n == 7 && !memcmp(text, "contain", 7))
            {
              facts.fit = loka::app::IMAGE_FIT_CONTAIN;
              ok = true;
            }
            else if (n == 5 && !memcmp(text, "cover", 5))
            {
              facts.fit = loka::app::IMAGE_FIT_COVER;
              ok = true;
            }
            else if (n == 7 && !memcmp(text, "stretch", 7))
            {
              facts.fit = loka::app::IMAGE_FIT_STRETCH;
              ok = true;
            }
            JS_FreeCString(ctx, text);
          }
          if (!ok)
          {
            JS_ThrowTypeError(ctx, "ImageView fit must be contain, cover or stretch");
            return false;
          }
          facts.hasFit = true;
        }
        else if (!boxExtentNumber(ctx, value.value(), width ? facts.width : facts.height))
        {
          JS_ThrowTypeError(ctx, "ImageView width and height must be integers in 1..4096");
          return false;
        }
      }
      if ((facts.width == 0) != (facts.height == 0))
      {
        JS_ThrowTypeError(ctx, "ImageView width and height must be provided together");
        return false;
      }
      return true;
    }
    template <class T> JSValue clickableTree(JSContext *ctx, int argc, JSValueConst *argv)
    {
      if (argc != 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "%s(label, handler) requires a label and function", T::schema.name);
      T facts;
      if (!textValue(ctx, argv[0], facts.source, facts.label))
        return JS_EXCEPTION;
      JsTreeStaging staging(ctx, T::schema, facts);
      if (facts.source == TREE_LIVE_SEAT)
        staging.edge(EDGE_LABEL, JS_DupValue(ctx, argv[0]));
      staging.edge(EDGE_HANDLER, JS_DupValue(ctx, argv[1]));
      return staging.publish();
    }
    JSValue buildTree(JSContext *ctx, const JsTreeSchema &schema, int argc, JSValueConst *argv)
    {
      switch (schema.kind)
      {
      case TREE_VSTACK:
        return childrenTree(ctx, argc, argv, VStackFacts());
      case TREE_ROW:
        return childrenTree(ctx, argc, argv, RowFacts());
      case TREE_ZSTACK:
        return childrenTree(ctx, argc, argv, ZStackFacts());
      case TREE_TOOLBAR:
        return childrenTree(ctx, argc, argv, ToolbarFacts());
      case TREE_GRID:
      {
        short rows = 0, cols = 0;
        if (argc != 3)
          return JS_ThrowTypeError(ctx, "Grid(rows, cols, children) requires three arguments");
        if (!readGridDimension(ctx, argv[0], rows) || !readGridDimension(ctx, argv[1], cols))
          return JS_ThrowRangeError(ctx, "Grid rows and cols must be integers in 1..16");
        GridFacts facts;
        facts.rows = rows;
        facts.cols = cols;
        return childrenTree(ctx, 1, argv + 2, facts, rows * cols, true);
      }
      case TREE_TEXT:
      {
        if (argc < 1 || argc > 3)
          return JS_ThrowTypeError(ctx, "Text(value, style?, block?) requires a value and optional style and block");
        TextFacts facts;
        if (!textValue(ctx, argv[0], facts.source, facts.text)
            || (argc >= 2 && !JS_IsUndefined(argv[1]) && !readTextStyle(ctx, argv[1], facts.style))
            || (argc == 3 && !JS_IsUndefined(argv[2]) && !readBlockStyle(ctx, argv[2], facts.block)))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, TextFacts::schema, facts);
        if (facts.source == TREE_LIVE_SEAT)
          staging.edge(EDGE_TEXT, JS_DupValue(ctx, argv[0]));
        return staging.publish();
      }
      case TREE_MARKUP:
      {
        if (argc < 1 || argc > 3)
          return JS_ThrowTypeError(ctx,
                                   "Markup(markup, style?, block?) requires a string and optional style and block");
        MarkupFacts facts;
        if (!readMarkup(ctx, argv[0], argc >= 2 ? argv[1] : JS_UNDEFINED, facts.text)
            || (argc == 3 && !JS_IsUndefined(argv[2]) && !readBlockStyle(ctx, argv[2], facts.block)))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, MarkupFacts::schema, facts);
        return staging.publish();
      }
      case TREE_EDIT:
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "EditText(seat) requires one seat");
        return seatTree(ctx, argv[0], EditFacts());
      case TREE_FILE:
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "OpenFileDialog(fileSeat) requires one seat");
        return seatTree(ctx, argv[0], FileFacts());
      case TREE_IMAGE:
      {
        if (argc < 1 || argc > 2)
          return JS_ThrowTypeError(ctx, "ImageView(imageSeat, options?) requires a seat and optional options");
        ImageFacts facts;
        if (!seatValue(ctx, argv[0]) || (argc == 2 && !imageOptions(ctx, argv[1], facts)))
          return JS_EXCEPTION;
        return seatTree(ctx, argv[0], facts);
      }
      case TREE_SHOW:
      {
        if (argc != 2)
          return JS_ThrowTypeError(ctx, "Show(boolSeat, child) requires a seat and one tree");
        if (!seatValue(ctx, argv[0]) || !childValue(ctx, argv[1], TREE_DRAWABLE))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, ShowFacts::schema, ShowFacts());
        staging.edge(EDGE_SEAT, JS_DupValue(ctx, argv[0]));
        staging.edge(EDGE_CHILD, JS_DupValue(ctx, argv[1]));
        return staging.publish();
      }
      case TREE_BUTTON:
        return clickableTree<ButtonFacts>(ctx, argc, argv);
      case TREE_CELL:
        return clickableTree<CellFacts>(ctx, argc, argv);
      case TREE_CARD:
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "Card(child) requires one tree");
        if (!childValue(ctx, argv[0], TREE_DRAWABLE))
          return JS_EXCEPTION;
        return singleTree(ctx, argv[0], CardFacts());
      case TREE_BOX:
      case TREE_CLIPBOX:
        if (argc < 1 || argc > 2)
          return JS_ThrowTypeError(
              ctx, "%s(child, options?) requires one tree and an optional options object", schema.name);
        if (!childValue(ctx, argv[0], TREE_DRAWABLE))
          return JS_EXCEPTION;
        if (schema.kind == TREE_BOX)
        {
          BoxFacts facts;
          if (argc == 2 && !boxOptions(ctx, argv[1], facts))
            return JS_EXCEPTION;
          return singleTree(ctx, argv[0], facts);
        }
        else
        {
          ClipBoxFacts facts;
          if (argc == 2 && !boxOptions(ctx, argv[1], facts))
            return JS_EXCEPTION;
          return singleTree(ctx, argv[0], facts);
        }
      case TREE_SCROLL:
      {
        if (argc < 1 || argc > 2)
          return JS_ThrowTypeError(ctx, "ScrollView(child, options?) requires one tree and an optional options object");
        if (!childValue(ctx, argv[0], TREE_DRAWABLE))
          return JS_EXCEPTION;
        ScrollFacts facts;
        JsValueRoot viewport(ctx);
        if (argc == 2 && !scrollOptions(ctx, argv[1], facts, viewport))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, ScrollFacts::schema, facts);
        staging.edge(EDGE_CHILD, JS_DupValue(ctx, argv[0]));
        staging.edge(EDGE_VIEWPORT, viewport.take());
        return staging.publish();
      }
      case TREE_ITEM_BUTTON:
      {
        if (argc != 2 || !JS_IsFunction(ctx, argv[1]))
          return JS_ThrowTypeError(ctx, "Invalid ToolbarItem arguments");
        ItemButtonFacts facts;
        JsTreeTextSource source = TREE_LITERAL;
        if (!textValue(ctx, argv[0], source, facts.label, true, true))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, ItemButtonFacts::schema, facts);
        staging.edge(EDGE_HANDLER, JS_DupValue(ctx, argv[1]));
        return staging.publish();
      }
      case TREE_ITEM_LABEL:
      {
        if (argc < 1 || argc > 2)
          return JS_ThrowTypeError(ctx, "Invalid ToolbarItem arguments");
        ItemLabelFacts facts;
        if (!textValue(ctx, argv[0], facts.source, facts.label, true)
            || (argc == 2 && !JS_IsUndefined(argv[1]) && !readTextStyle(ctx, argv[1], facts.style)))
          return JS_EXCEPTION;
        JsTreeStaging staging(ctx, ItemLabelFacts::schema, facts);
        if (facts.source == TREE_LIVE_SEAT)
          staging.edge(EDGE_LABEL, JS_DupValue(ctx, argv[0]));
        return staging.publish();
      }
      case TREE_ITEM_FLEX:
      {
        if (argc)
          return JS_ThrowTypeError(ctx, "Invalid ToolbarItem arguments");
        JsTreeStaging staging(ctx, ItemFlexFacts::schema, ItemFlexFacts());
        return staging.publish();
      }
      case TREE_ITEM_FIXED:
      {
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "Invalid ToolbarItem arguments");
        ItemFixedFacts facts;
        if (!scrollInsetNumber(ctx, argv[0], facts.width) || !facts.width)
          return JS_ThrowTypeError(ctx, "Invalid ToolbarItem payload");
        JsTreeStaging staging(ctx, ItemFixedFacts::schema, facts);
        return staging.publish();
      }
      case TREE_CUSTOM:
      case TREE_KIND_COUNT:
        break;
      }
      return JS_ThrowTypeError(ctx, "unknown tree helper");
    }
    bool lowerChildren(JsTreeLowering &lowering,
                       const JsTreeView &tree,
                       int depth,
                       loka::app::scene::INestableDefinition &nest)
    {
      for (unsigned i = 0; i < tree.childCount(); ++i)
      {
        JsValueRoot child(lowering.context);
        tree.childAt(i, child);
        loka::app::scene::NodeDefinitionBase *definition = lowering.card.lowerChild(lowering, child.value(), depth + 1);
        if (!definition)
          return false;
        nest.addOwnedChild(definition);
      }
      return true;
    }
    loka::app::scene::NodeDefinitionBase *lowerSingle(JsTreeLowering &lowering, const JsTreeView &tree, int depth)
    {
      JsValueRoot child(lowering.context);
      tree.edge(EDGE_CHILD, child);
      return lowering.card.lowerChild(lowering, child.value(), depth + 1);
    }
    bool containsScroll(const JsTreeView &tree, int depth)
    {
      if (depth > kMaximumTreeDepth || !tree.valid())
        return false;
      if (tree.schema()->kind == TREE_SCROLL)
        return true;
      for (unsigned i = 0; i < tree.schema()->slotCount; ++i)
      {
        if (tree.schema()->slots[i].role == TREE_CHILD)
        {
          JsValueRoot child(tree.context());
          tree.slot(i, child);
          JsTreeView view(tree.context(), child.value());
          if (containsScroll(view, depth + 1))
            return true;
        }
        else if (tree.schema()->slots[i].role == TREE_CHILDREN)
          for (unsigned j = 0; j < tree.childCount(); ++j)
          {
            JsValueRoot child(tree.context());
            tree.childAt(j, child);
            JsTreeView view(tree.context(), child.value());
            if (containsScroll(view, depth + 1))
              return true;
          }
      }
      return false;
    }
    template <class T>
    loka::app::scene::NodeDefinitionBase *
    lowerBox(JsTreeLowering &lowering, const JsTreeView &tree, int depth, bool clip)
    {
      const T &facts = *tree.facts(T::schema);
      loka::app::BoxDefinition *box =
          new (std::nothrow) loka::app::BoxDefinition(clip ? loka::app::ClipBox() : loka::app::Box());
      if (!box)
        return 0;
      box->padding(facts.padding);
      if (facts.width)
        box->width(facts.width);
      if (facts.height)
        box->height(facts.height);
      loka::app::scene::NodeDefinitionBase *child = lowerSingle(lowering, tree, depth);
      if (!child)
      {
        delete box;
        return 0;
      }
      box->asNestableDefinition()->addOwnedChild(child);
      return box;
    }
    loka::app::scene::NodeDefinitionBase *lowerTree(JsTreeLowering &lowering, const JsTreeView &tree, int depth)
    {
      using namespace loka::app;
      switch (tree.schema()->kind)
      {
      case TREE_ROW:
      {
        Row *row = new (std::nothrow) Row();
        if (!row)
          return 0;
        if (tree.facts(RowFacts::schema)->rowWidth == ROW_UNDECLARED_WIDTH_NATURAL)
          row->naturalWidth();
        if (!lowerChildren(lowering, tree, depth, *row))
        {
          delete row;
          return 0;
        }
        return row;
      }
      case TREE_VSTACK:
      {
        VStack *column = new (std::nothrow) VStack();
        if (!column)
          return 0;
        if (tree.facts(VStackFacts::schema)->columnHeight == COLUMN_UNDECLARED_HEIGHT_NATURAL)
          column->naturalHeight();
        if (!lowerChildren(lowering, tree, depth, *column))
        {
          delete column;
          return 0;
        }
        return column;
      }
      case TREE_ZSTACK:
      {
        ZStack *stack = new (std::nothrow) ZStack();
        if (!stack)
          return 0;
        switch (tree.facts(ZStackFacts::schema)->unitGround)
        {
        case ZSTACK_UNIT_GROUND_WINDOW:
          break;
        case ZSTACK_UNIT_GROUND_EXP_BLUR:
          stack->expBlurUnit();
          break;
        case ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR:
          stack->expStaticBlurUnit();
          break;
        }
        if (!lowerChildren(lowering, tree, depth, *stack))
        {
          delete stack;
          return 0;
        }
        return stack;
      }
      case TREE_GRID:
      {
        const GridFacts &facts = *tree.facts(GridFacts::schema);
        Grid *grid = new (std::nothrow) Grid();
        if (!grid)
          return 0;
        grid->rows(static_cast<short>(facts.rows)).cols(static_cast<short>(facts.cols));
        if (!lowerChildren(lowering, tree, depth, *grid))
        {
          delete grid;
          return 0;
        }
        return grid;
      }
      case TREE_MARKUP:
      {
        const MarkupFacts &facts = *tree.facts(MarkupFacts::schema);
        return new (std::nothrow) AttributedTextDefinitionWithAttr(AttributedText(facts.text) + facts.block);
      }
      case TREE_CARD:
      {
        CardDefinition *card = new (std::nothrow) CardDefinition();
        if (!card || card->childrenCount() != 1 + CARD_BAND_COUNT)
        {
          delete card;
          return 0;
        }
        scene::NodeDefinitionBase *child = lowerSingle(lowering, tree, depth);
        if (!child)
        {
          delete card;
          return 0;
        }
        *card << child;
        return card;
      }
      case TREE_BOX:
        return lowerBox<BoxFacts>(lowering, tree, depth, false);
      case TREE_CLIPBOX:
        return lowerBox<ClipBoxFacts>(lowering, tree, depth, true);
      case TREE_SCROLL:
      {
        JsValueRoot child(lowering.context);
        tree.edge(EDGE_CHILD, child);
        JsTreeView view(lowering.context, child.value());
        if (containsScroll(view, depth + 1))
        {
          lowering.card.fail("JavaScript ScrollView cannot contain another ScrollView.");
          return 0;
        }
        const ScrollFacts &facts = *tree.facts(ScrollFacts::schema);
        ScrollView *scroll = lowering.card.createScrollView(
            lowering, tree, facts.insetTop ? ScrollViewProps::topOrigin() : loka::core::Frame(0, 0, 0, 0));
        if (!scroll)
          return 0;
        scroll->contentInsets(facts.insetTop, facts.insetBottom);
        scene::NodeDefinitionBase *content = lowerSingle(lowering, tree, depth);
        if (!content)
        {
          delete scroll;
          return 0;
        }
        scroll->asNestableDefinition()->addOwnedChild(content);
        return scroll;
      }
      case TREE_TEXT:
        return lowering.card.lowerText(lowering, tree);
      case TREE_EDIT:
        return lowering.card.lowerEditText(lowering, tree);
      case TREE_BUTTON:
        return lowering.card.lowerClickable(lowering, tree, false);
      case TREE_CELL:
        return lowering.card.lowerClickable(lowering, tree, true);
      case TREE_SHOW:
        return lowering.card.lowerShow(lowering, tree, depth);
      case TREE_FILE:
        return lowering.card.lowerOpenFileDialog(lowering, tree);
      case TREE_IMAGE:
        return lowering.card.lowerImageView(lowering, tree);
      case TREE_TOOLBAR:
        return lowering.card.lowerToolbar(lowering, tree, depth);
      case TREE_ITEM_BUTTON:
      case TREE_ITEM_LABEL:
      case TREE_ITEM_FLEX:
      case TREE_ITEM_FIXED:
        lowering.card.fail("ToolbarItem descriptors require a Toolbar.");
        return 0;
      case TREE_CUSTOM:
      case TREE_KIND_COUNT:
        break;
      }
      lowering.card.fail("JavaScript tree has an invalid node.");
      return 0;
    }
    /** Registry identity points to the static schema, never to a numeric facts cast. */
    class TreeBinding : public IJsNodeLowering
    {
    public:
      TreeBinding(const JsTreeSchema &schema, const char *parent = 0)
      {
        this->schema = &schema;
        this->name = schema.name;
        this->parent = parent;
      }
      virtual bool queryToolbarRowKind(JsToolbarRowKind &out) const
      {
        if (this->schema == &ItemButtonFacts::schema)
          out = JS_TOOLBAR_ROW_BUTTON;
        else if (this->schema == &ItemLabelFacts::schema)
          out = JS_TOOLBAR_ROW_LABEL;
        else if (this->schema == &ItemFlexFacts::schema)
          out = JS_TOOLBAR_ROW_FLEXIBLE_SPACE;
        else if (this->schema == &ItemFixedFacts::schema)
          out = JS_TOOLBAR_ROW_FIXED_SPACE;
        else
          return false;
        return true;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        return buildTree(ctx, *this->schema, argc, argv);
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsTreeLowering &lowering, const JsTreeView &tree, int depth)
      {
        if (tree.schema() != this->schema)
        {
          lowering.card.fail("JavaScript tree has an invalid node.");
          return 0;
        }
        return lowerTree(lowering, tree, depth);
      }
    };
  } // namespace
  bool RegisterSmirkyCardBindings(JsCardBindingRegistry &registry)
  {
    const JsTreeSchema *schemas[] = {&VStackFacts::schema,    &TextFacts::schema,     &EditFacts::schema,
                                     &ButtonFacts::schema,    &RowFacts::schema,      &MarkupFacts::schema,
                                     &CellFacts::schema,      &GridFacts::schema,     &ShowFacts::schema,
                                     &FileFacts::schema,      &ImageFacts::schema,    &ScrollFacts::schema,
                                     &CardFacts::schema,      &ZStackFacts::schema,   &BoxFacts::schema,
                                     &ClipBoxFacts::schema,   &ToolbarFacts::schema,  &ItemButtonFacts::schema,
                                     &ItemLabelFacts::schema, &ItemFlexFacts::schema, &ItemFixedFacts::schema};
    for (unsigned i = 0; i < sizeof(schemas) / sizeof(schemas[0]); ++i)
    {
      TreeBinding *binding = new (std::nothrow) TreeBinding(*schemas[i], i >= 18 ? "ToolbarItem" : 0);
      if (!registry.registerLowering(binding))
      {
        delete binding;
        return false;
      }
    }
    IJsGlobal card = {"card", &ScriptRuntime::card, 2};
    return registry.registerGlobal(card);
  }
} // namespace smirkycard
