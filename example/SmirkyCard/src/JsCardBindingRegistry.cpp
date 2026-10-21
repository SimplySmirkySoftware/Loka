#include "JsCardBindingRegistry.hpp"
#include "CardNodes.hpp"
#include "SmirkyMarkup.hpp"
#include "JsOwnProperties.hpp"
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
    if (!lowering || !lowering->name || lowering->kind)
      return false;
    for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
      if (entry->value_->kind == nextKind_ || !strcmp(entry->value_->name, lowering->name))
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
      if (!strcmp(entry->value_->name, global.name))
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
  bool JsCardBindingRegistry::install(JSContext *context) const
  {
    JSValue global = JS_GetGlobalObject(context);
    for (LoweringEntry *entry = lowerings_; entry; entry = entry->next_)
      JS_SetPropertyStr(context,
                        global,
                        entry->value_->name,
                        JS_NewCFunctionMagic(context,
                                             &JsCardBindingRegistry::build,
                                             entry->value_->name,
                                             1,
                                             JS_CFUNC_generic_magic,
                                             entry->value_->kind));
    for (GlobalEntry *entry = globals_; entry; entry = entry->next_)
      JS_SetPropertyStr(context,
                        global,
                        entry->value_.name,
                        JS_NewCFunction(context, entry->value_.fn, entry->value_.name, entry->value_.length));
    JS_FreeValue(context, global);
    return true;
  }
  JSValue JsCardBindingRegistry::build(JSContext *context, JSValueConst, int argc, JSValueConst *argv, int magic)
  {
    ScriptRuntime *runtime = static_cast<ScriptRuntime *>(JS_GetContextOpaque(context));
    IJsNodeLowering *lowering = runtime ? runtime->registry_.lookup(magic) : 0;
    return lowering ? lowering->build(context, argc, argv) : JS_ThrowTypeError(context, "unknown tree helper");
  }
  namespace
  {
    JSValue newTree(JSContext *ctx, int kind)
    {
      JSValue node = JS_NewObject(ctx);
      JS_DefinePropertyValueStr(ctx, node, "kind", JS_NewInt32(ctx, kind), JS_PROP_ENUMERABLE);
      JS_SetPropertyStr(ctx, node, "TEST_ID", JS_NewCFunction(ctx, &ScriptRuntime::testId, "TEST_ID", 1));
      JS_SetPropertyStr(
          ctx, node, "naturalWidth", JS_NewCFunction(ctx, &ScriptRuntime::naturalWidth, "naturalWidth", 0));
      JS_SetPropertyStr(
          ctx, node, "naturalHeight", JS_NewCFunction(ctx, &ScriptRuntime::naturalHeight, "naturalHeight", 0));
      JS_SetPropertyStr(ctx,
                        node,
                        "expStaticBlurUnit",
                        JS_NewCFunction(ctx, &ScriptRuntime::expStaticBlurUnit, "expStaticBlurUnit", 0));
      JS_SetPropertyStr(ctx, node, "expBlurUnit", JS_NewCFunction(ctx, &ScriptRuntime::expBlurUnit, "expBlurUnit", 0));
      return node;
    }
    /** Reads a tree policy from the tree's own data property, the one its modifier
        wrote (`naturalWidth()` writes "rowWidth", `expStaticBlurUnit()` writes
        "staticBlurUnit"), so a value placed on Object.prototype is not a claim. */
    bool ownTreePolicy(JSContext *ctx, JSValueConst tree, const char *name)
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
    JSValue collectChildren(JSContext *ctx, const char *name, int argc, JSValueConst *argv, uint32_t limit, bool exact)
    {
      JSValue children = JS_NewArray(ctx);
      if (JS_IsException(children))
        return children;
      uint32_t count = 0;
      for (int i = 0; i < argc; ++i)
      {
        int64_t length = 0;
        const bool array = JS_IsArray(argv[i]);
        if (array && JS_GetLength(ctx, argv[i], &length))
        {
          JS_FreeValue(ctx, children);
          return JS_ThrowTypeError(ctx, "%s children must be tree nodes", name);
        }
        const uint32_t entries = array ? static_cast<uint32_t>(length) : 1;
        for (uint32_t j = 0; j < entries; ++j)
        {
          JSValue child = array ? JS_GetPropertyUint32(ctx, argv[i], j) : JS_DupValue(ctx, argv[i]);
          JSValue childKind = JS_GetPropertyStr(ctx, child, "kind");
          if (!JS_IsObject(child) || JS_IsArray(child) || !JS_IsNumber(childKind))
          {
            JS_FreeValue(ctx, childKind);
            JS_FreeValue(ctx, child);
            JS_FreeValue(ctx, children);
            return JS_ThrowTypeError(ctx, "%s children must be tree nodes (nested arrays are not allowed)", name);
          }
          JS_FreeValue(ctx, childKind);
          if (count == limit)
          {
            JS_FreeValue(ctx, child);
            JS_FreeValue(ctx, children);
            return exact ? JS_ThrowRangeError(ctx, "%s requires exactly rows * cols children (%u)", name, static_cast<unsigned>(limit))
                         : JS_ThrowRangeError(ctx, "%s accepts at most %u children", name, static_cast<unsigned>(limit));
          }
          if (JS_SetPropertyUint32(ctx, children, count++, child) < 0)
          {
            JS_FreeValue(ctx, children);
            return JS_EXCEPTION;
          }
        }
      }
      if (exact && count != limit)
      {
        JS_FreeValue(ctx, children);
        return JS_ThrowRangeError(ctx, "%s requires exactly rows * cols children (%u)", name, static_cast<unsigned>(limit));
      }
      return children;
    }
    bool lowerChildren(JsCardNode &node,
                       JSContext *ctx,
                       JSValueConst tree,
                       int depth,
                       loka::app::scene::INestableDefinition &nest,
                       int limit,
                       bool exact,
                       const char *name)
    {
      JSValue children = JS_GetPropertyStr(ctx, tree, "children");
      int64_t length = 0;
      if (!JS_IsArray(children) || JS_GetLength(ctx, children, &length) || length < 0 || length > limit
          || (exact && length != limit))
      {
        JS_FreeValue(ctx, children);
        node.fail(name);
        return false;
      }
      for (uint32_t i = 0; i < static_cast<uint32_t>(length); ++i)
      {
        JSValue child = JS_GetPropertyUint32(ctx, children, i);
        loka::app::scene::NodeDefinitionBase *definition = node.lowerChild(ctx, child, depth + 1);
        JS_FreeValue(ctx, child);
        if (!definition)
        {
          JS_FreeValue(ctx, children);
          return false;
        }
        nest.addOwnedChild(definition);
      }
      JS_FreeValue(ctx, children);
      return true;
    }
    class StackLowering : public IJsNodeLowering
    {
    public:
      enum Axis
      {
        STACK_ROW,
        STACK_COLUMN,
        STACK_DEPTH
      };

      StackLowering(const char *n, Axis axis)
          : axis_(axis)
      {
        name = n;
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        JSValue children = collectChildren(ctx, name, argc, argv, 16, false);
        if (JS_IsException(children))
          return children;
        JSValue tree = newTree(ctx, kind);
        if (JS_IsException(tree))
        {
          JS_FreeValue(ctx, children);
          return tree;
        }
        JS_SetPropertyStr(ctx, tree, "children", children);
        JS_FreezeObject(ctx, tree);
        return tree;
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        loka::app::scene::NodeDefinitionBase *stack = 0;
        loka::app::scene::INestableDefinition *nest = 0;
        switch (this->axis_)
        {
        case STACK_ROW:
        {
          loka::app::Row *row = new (std::nothrow) loka::app::Row();
          if (row && ownTreePolicy(ctx, tree, "rowWidth"))
            row->naturalWidth();
          stack = row;
          nest = row;
          break;
        }
        case STACK_COLUMN:
        {
          loka::app::VStack *column = new (std::nothrow) loka::app::VStack();
          if (column && ownTreePolicy(ctx, tree, "columnHeight"))
            column->naturalHeight();
          stack = column;
          nest = column;
          break;
        }
        case STACK_DEPTH:
        {
          loka::app::ZStack *depthStack = new (std::nothrow) loka::app::ZStack();
          if (depthStack && ownTreePolicy(ctx, tree, "staticBlurUnit"))
            depthStack->expStaticBlurUnit();
          else if (depthStack && ownTreePolicy(ctx, tree, "blurUnit"))
            depthStack->expBlurUnit();
          stack = depthStack;
          nest = depthStack;
          break;
        }
        }
        if (!stack)
        {
          node.fail("JavaScript stack allocation refused.");
          return 0;
        }
        if (!lowerChildren(node, ctx, tree, depth, *nest, 16, false, "JavaScript stack has invalid children."))
        {
          delete stack;
          return 0;
        }
        return stack;
      }

    private:
      const Axis axis_;
    };
    bool readGridDimension(JSContext *ctx, JSValueConst value, short &out)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || !(number >= 1 && number <= 16)
          || number != static_cast<short>(number))
        return false;
      out = static_cast<short>(number);
      return true;
    }
    class GridLowering : public IJsNodeLowering
    {
    public:
      GridLowering()
      {
        name = "Grid";
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc != 3)
          return JS_ThrowTypeError(ctx, "Grid(rows, cols, children) requires three arguments");
        short rows = 0, cols = 0;
        if (!readGridDimension(ctx, argv[0], rows) || !readGridDimension(ctx, argv[1], cols))
          return JS_ThrowRangeError(ctx, "Grid rows and cols must be integers in 1..16");
        JSValue children = collectChildren(ctx, name, 1, argv + 2, rows * cols, true);
        if (JS_IsException(children))
          return children;
        JSValue tree = newTree(ctx, kind);
        if (JS_IsException(tree))
        {
          JS_FreeValue(ctx, children);
          return tree;
        }
        if (JS_SetPropertyStr(ctx, tree, "children", children) < 0
            || JS_SetPropertyStr(ctx, tree, "rows", JS_NewInt32(ctx, rows)) < 0
            || JS_SetPropertyStr(ctx, tree, "cols", JS_NewInt32(ctx, cols)) < 0 || JS_FreezeObject(ctx, tree) < 0)
        {
          JS_FreeValue(ctx, tree);
          return JS_EXCEPTION;
        }
        return tree;
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        short rows = 0, cols = 0;
        JSValue rowValue = JS_GetPropertyStr(ctx, tree, "rows");
        JSValue colValue = JS_GetPropertyStr(ctx, tree, "cols");
        const bool valid = readGridDimension(ctx, rowValue, rows) && readGridDimension(ctx, colValue, cols);
        JS_FreeValue(ctx, rowValue);
        JS_FreeValue(ctx, colValue);
        if (!valid)
        {
          node.fail("Grid rows and cols must be integers in 1..16");
          return 0;
        }
        loka::app::Grid *grid = new (std::nothrow) loka::app::Grid();
        if (!grid)
        {
          node.fail("JavaScript Grid allocation refused.");
          return 0;
        }
        grid->rows(rows).cols(cols);
        if (!lowerChildren(
                node, ctx, tree, depth, *grid, rows * cols, true, "Grid requires exactly rows * cols children"))
        {
          delete grid;
          return 0;
        }
        return grid;
      }
    };
    class TextLowering : public IJsNodeLowering
    {
    public:
      TextLowering()
      {
        name = "Text";
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc < 1 || argc > 3)
          return JS_ThrowTypeError(ctx, "Text(value, style?, block?) requires a value and optional style and block");
        loka::app::TextStyle style;
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !readTextStyle(ctx, argv[1], style))
          return JS_EXCEPTION;
        loka::app::BlockStyle block;
        if (argc == 3 && !JS_IsUndefined(argv[2]) && !readBlockStyle(ctx, argv[2], block))
          return JS_EXCEPTION;
        JSValue n = newTree(ctx, kind);
        if (JS_IsException(n))
          return n;
        if (JS_SetPropertyStr(ctx, n, "text", JS_DupValue(ctx, argv[0])) < 0
            || (argc >= 2 && JS_SetPropertyStr(ctx, n, "style", JS_DupValue(ctx, argv[1])) < 0)
            || (argc == 3 && JS_SetPropertyStr(ctx, n, "block", JS_DupValue(ctx, argv[2])) < 0)
            || JS_FreezeObject(ctx, n) < 0)
        {
          JS_FreeValue(ctx, n);
          return JS_EXCEPTION;
        }
        return n;
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        return node.lowerText(ctx, tree);
      }
    };
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

    class MarkupLowering : public IJsNodeLowering
    {
    public:
      MarkupLowering()
      {
        name = "Markup";
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc < 1 || argc > 3)
          return JS_ThrowTypeError(ctx,
                                   "Markup(markup, style?, block?) requires a string and optional style and block");
        loka::app::AttributedString parsed;
        if (!readMarkup(ctx, argv[0], argc >= 2 ? argv[1] : JS_UNDEFINED, parsed))
          return JS_EXCEPTION;
        loka::app::BlockStyle block;
        if (argc == 3 && !JS_IsUndefined(argv[2]) && !readBlockStyle(ctx, argv[2], block))
          return JS_EXCEPTION;
        JSValue n = newTree(ctx, kind);
        if (JS_IsException(n))
          return n;
        if (JS_SetPropertyStr(ctx, n, "markup", JS_DupValue(ctx, argv[0])) < 0
            || (argc >= 2 && JS_SetPropertyStr(ctx, n, "style", JS_DupValue(ctx, argv[1])) < 0)
            || (argc == 3 && JS_SetPropertyStr(ctx, n, "block", JS_DupValue(ctx, argv[2])) < 0)
            || JS_FreezeObject(ctx, n) < 0)
        {
          JS_FreeValue(ctx, n);
          return JS_EXCEPTION;
        }
        return n;
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        JSValue value = JS_GetPropertyStr(ctx, tree, "markup");
        JSValue dict = JS_GetPropertyStr(ctx, tree, "style");
        loka::app::AttributedString parsed;
        bool valid = readMarkup(ctx, value, dict, parsed);
        JS_FreeValue(ctx, dict);
        JS_FreeValue(ctx, value);
        loka::app::BlockStyle block;
        if (valid)
        {
          dict = JS_GetPropertyStr(ctx, tree, "block");
          valid = !JS_IsException(dict) && (JS_IsUndefined(dict) || readBlockStyle(ctx, dict, block));
          JS_FreeValue(ctx, dict);
        }
        if (!valid)
        {
          JS_FreeValue(ctx, JS_GetException(ctx));
          node.fail("JavaScript Markup parse, style, block or allocation refused.");
          return 0;
        }
        return new (std::nothrow)
            loka::app::AttributedTextDefinitionWithAttr(loka::app::AttributedText(parsed) + block);
      }
    };
    class EditTextLowering : public IJsNodeLowering
    {
    public:
      EditTextLowering()
      {
        name = "EditText";
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "EditText(seat) requires one seat");
        JSValue n = newTree(ctx, kind);
        JS_SetPropertyStr(ctx, n, "seat", JS_DupValue(ctx, argv[0]));
        JS_FreezeObject(ctx, n);
        return n;
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        return node.lowerEditText(ctx, tree);
      }
    };
    JSValue seatTree(JSContext *ctx, int kind, JSValueConst seat, JSValueConst child = JS_UNDEFINED)
    {
      JSValue tree = newTree(ctx, kind);
      if (JS_IsException(tree))
        return tree;
      if (JS_SetPropertyStr(ctx, tree, "seat", JS_DupValue(ctx, seat)) < 0
          || (!JS_IsUndefined(child) && JS_SetPropertyStr(ctx, tree, "child", JS_DupValue(ctx, child)) < 0)
          || JS_FreezeObject(ctx, tree) < 0)
      {
        JS_FreeValue(ctx, tree);
        return JS_EXCEPTION;
      }
      return tree;
    }
    class ShowLowering : public IJsNodeLowering
    {
    public:
      ShowLowering()
      {
        this->name = "Show";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc != 2 || !JS_IsObject(argv[1]) || JS_IsArray(argv[1]))
          return JS_ThrowTypeError(ctx, "Show(boolSeat, child) requires a seat and one tree");
        JSValue childKind = JS_GetPropertyStr(ctx, argv[1], "kind");
        const bool valid = JS_IsNumber(childKind);
        JS_FreeValue(ctx, childKind);
        if (!valid)
          return JS_ThrowTypeError(ctx, "Show child must be a tree");
        return seatTree(ctx, this->kind, argv[0], argv[1]);
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        return node.lowerShow(ctx, tree, depth);
      }
    };
    class OpenFileDialogLowering : public IJsNodeLowering
    {
    public:
      OpenFileDialogLowering()
      {
        this->name = "OpenFileDialog";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc != 1)
          return JS_ThrowTypeError(ctx, "OpenFileDialog(fileSeat) requires one seat");
        return seatTree(ctx, this->kind, argv[0]);
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        return node.lowerOpenFileDialog(ctx, tree);
      }
    };
    JSValue singleChildTree(JSContext *ctx, int kind, const char *name, int argc, JSValueConst *argv)
    {
      if (argc != 1 || !JS_IsObject(argv[0]) || JS_IsArray(argv[0]))
        return JS_ThrowTypeError(ctx, "%s(child) requires one tree", name);
      JSValue childKind = JS_GetPropertyStr(ctx, argv[0], "kind");
      const bool valid = JS_IsNumber(childKind);
      JS_FreeValue(ctx, childKind);
      if (!valid)
        return JS_ThrowTypeError(ctx, "%s(child) requires one tree", name);
      JSValue tree = newTree(ctx, kind);
      if (JS_IsException(tree))
        return tree;
      if (JS_SetPropertyStr(ctx, tree, "child", JS_DupValue(ctx, argv[0])) < 0 || JS_FreezeObject(ctx, tree) < 0)
      {
        JS_FreeValue(ctx, tree);
        return JS_EXCEPTION;
      }
      return tree;
    }
    loka::app::scene::NodeDefinitionBase *
    lowerSingleChild(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth, const char *refusal)
    {
      JSValue child = JS_GetPropertyStr(ctx, tree, "child");
      if (!JS_IsObject(child) || JS_IsArray(child))
      {
        JS_FreeValue(ctx, child);
        node.fail(refusal);
        return 0;
      }
      loka::app::scene::NodeDefinitionBase *content = node.lowerChild(ctx, child, depth + 1);
      JS_FreeValue(ctx, child);
      return content;
    }
    /** Every rail refuses a viewport scope inside another: the inner subtree
        would materialize nothing. Walk the child tree's `child` and `children`
        properties once per ScrollView lowering (bounded by the card's own tree)
        so the script sees a refusal instead of a silently empty region. */
    bool containsKind(JSContext *ctx, JSValueConst tree, int kind, int depth)
    {
      if (depth > kMaximumTreeDepth || !JS_IsObject(tree) || JS_IsArray(tree))
        return false;
      JSValue own = JS_GetPropertyStr(ctx, tree, "kind");
      int32_t value = 0;
      const bool match = JS_IsNumber(own) && !JS_ToInt32(ctx, &value, own) && value == kind;
      JS_FreeValue(ctx, own);
      if (match)
        return true;
      JSValue child = JS_GetPropertyStr(ctx, tree, "child");
      const bool inChild = containsKind(ctx, child, kind, depth + 1);
      JS_FreeValue(ctx, child);
      if (inChild)
        return true;
      JSValue children = JS_GetPropertyStr(ctx, tree, "children");
      bool inChildren = false;
      if (JS_IsArray(children))
      {
        JSValue lengthValue = JS_GetPropertyStr(ctx, children, "length");
        int32_t length = 0;
        if (JS_IsNumber(lengthValue))
          JS_ToInt32(ctx, &length, lengthValue);
        JS_FreeValue(ctx, lengthValue);
        for (int32_t i = 0; i < length && !inChildren; ++i)
        {
          JSValue entry = JS_GetPropertyUint32(ctx, children, static_cast<uint32_t>(i));
          inChildren = containsKind(ctx, entry, kind, depth + 1);
          JS_FreeValue(ctx, entry);
        }
      }
      JS_FreeValue(ctx, children);
      return inChildren;
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
    bool readScrollOptions(JSContext *ctx, JSValueConst options, JSValueConst tree, int &top, int &bottom)
    {
      if (!requirePlainObject(ctx, options, "ScrollView options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(options, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSValue key = JS_AtomToValue(ctx, names.atom(i));
        size_t length = 0;
        const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
        JS_FreeValue(ctx, key);
        const bool isInsets = name && length == 6 && !memcmp(name, "insets", 6);
        const bool isViewport = name && length == 8 && !memcmp(name, "viewport", 8);
        if (name)
          JS_FreeCString(ctx, name);
        if (!isInsets && !isViewport)
        {
          JS_ThrowTypeError(ctx, "ScrollView options accept only insets and viewport");
          return false;
        }
        JSValue value = JS_GetProperty(ctx, options, names.atom(i));
        const bool valid =
            !JS_IsException(value)
            && (isInsets
                    ? readScrollInsets(ctx, value, top, bottom)
                    : JS_DefinePropertyValueStr(ctx, tree, "viewport", JS_DupValue(ctx, value), JS_PROP_C_W_E) >= 0);
        JS_FreeValue(ctx, value);
        if (!valid)
          return false;
      }
      return true;
    }
    class ScrollViewLowering : public IJsNodeLowering
    {
    public:
      ScrollViewLowering()
      {
        this->name = "ScrollView";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc < 1 || argc > 2 || !JS_IsObject(argv[0]) || JS_IsArray(argv[0]))
          return JS_ThrowTypeError(ctx, "ScrollView(child, options?) requires one tree and an optional options object");
        JSValue childKind = JS_GetPropertyStr(ctx, argv[0], "kind");
        const bool valid = JS_IsNumber(childKind);
        JS_FreeValue(ctx, childKind);
        if (!valid)
          return JS_ThrowTypeError(ctx, "ScrollView(child, options?) requires one tree and an optional options object");
        int top = 0, bottom = 0;
        JSValue tree = newTree(ctx, this->kind);
        if (JS_IsException(tree))
          return tree;
        if (argc == 2 && !readScrollOptions(ctx, argv[1], tree, top, bottom))
        {
          JS_FreeValue(ctx, tree);
          return JS_EXCEPTION;
        }
        if (JS_SetPropertyStr(ctx, tree, "child", JS_DupValue(ctx, argv[0])) < 0
            || JS_SetPropertyStr(ctx, tree, "insetTop", JS_NewInt32(ctx, top)) < 0
            || JS_SetPropertyStr(ctx, tree, "insetBottom", JS_NewInt32(ctx, bottom)) < 0
            || JS_FreezeObject(ctx, tree) < 0)
        {
          JS_FreeValue(ctx, tree);
          return JS_EXCEPTION;
        }
        return tree;
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        JSValue child = JS_GetPropertyStr(ctx, tree, "child");
        const bool nested = containsKind(ctx, child, this->kind, depth + 1);
        JS_FreeValue(ctx, child);
        if (nested)
        {
          node.fail("JavaScript ScrollView cannot contain another ScrollView.");
          return 0;
        }
        int top = 0, bottom = 0;
        JSValue topValue = JS_GetPropertyStr(ctx, tree, "insetTop");
        JSValue bottomValue = JS_GetPropertyStr(ctx, tree, "insetBottom");
        const bool valid = (JS_IsUndefined(topValue) || scrollInsetNumber(ctx, topValue, top))
                           && (JS_IsUndefined(bottomValue) || scrollInsetNumber(ctx, bottomValue, bottom));
        JS_FreeValue(ctx, topValue);
        JS_FreeValue(ctx, bottomValue);
        if (!valid)
        {
          node.fail("JavaScript ScrollView has invalid insets.");
          return 0;
        }
        // Only a top inset has a gap to show: origin.y reaches down to -top, never below 0 for bottom alone.
        loka::app::ScrollView *scroll = node.createScrollView(
            ctx, tree, top ? loka::app::ScrollViewProps::topOrigin() : loka::core::Frame(0, 0, 0, 0));
        if (!scroll)
          return 0;
        scroll->contentInsets(top, bottom);
        loka::app::scene::NodeDefinitionBase *content =
            lowerSingleChild(node, ctx, tree, depth, "JavaScript ScrollView has an invalid child.");
        if (!content)
        {
          delete scroll;
          return 0;
        }
        scroll->asNestableDefinition()->addOwnedChild(content);
        return scroll;
      }
    };
    class CardLowering : public IJsNodeLowering
    {
    public:
      CardLowering()
      {
        this->name = "Card";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        return singleChildTree(ctx, this->kind, this->name, argc, argv);
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        loka::app::CardDefinition *card = new (std::nothrow) loka::app::CardDefinition();
        if (!card || card->childrenCount() != 1 + loka::app::CARD_BAND_COUNT)
        {
          delete card;
          node.fail("JavaScript Card allocation refused.");
          return 0;
        }
        loka::app::scene::NodeDefinitionBase *content =
            lowerSingleChild(node, ctx, tree, depth, "JavaScript Card has an invalid child.");
        if (!content)
        {
          delete card;
          return 0;
        }
        *card << content;
        return card;
      }
    };
    bool boxExtentNumber(JSContext *ctx, JSValueConst value, int &out)
    {
      double number = 0;
      if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) || !(number >= 1 && number <= 4096)
          || number != static_cast<int>(number))
        return false;
      out = static_cast<int>(number);
      return true;
    }
    bool copyBoxOptions(JSContext *ctx, JSValueConst options, JSValue tree)
    {
      if (!requirePlainObject(ctx, options, "Box options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(options, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSValue key = JS_AtomToValue(ctx, names.atom(i));
        size_t length = 0;
        const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
        JS_FreeValue(ctx, key);
        const bool isWidth = name && length == 5 && !memcmp(name, "width", 5);
        const bool isHeight = name && length == 6 && !memcmp(name, "height", 6);
        if (name)
          JS_FreeCString(ctx, name);
        if (!isWidth && !isHeight)
        {
          JS_ThrowTypeError(ctx, "Box options accept only width and height");
          return false;
        }
        JSValue value = JS_GetProperty(ctx, options, names.atom(i));
        if (JS_IsException(value))
          return false;
        int number = 0;
        const bool valid = boxExtentNumber(ctx, value, number);
        JS_FreeValue(ctx, value);
        if (!valid)
        {
          JS_ThrowTypeError(ctx, "Box width and height must be integers in 1..4096");
          return false;
        }
        if (JS_SetPropertyStr(ctx, tree, isWidth ? "width" : "height", JS_NewInt32(ctx, number)) < 0)
          return false;
      }
      return true;
    }
    /** Reads an own data property only, so a width placed on Object.prototype is
        not a claim. Absent means no claim; an accessor or an invalid value refuses. */
    bool readBoxExtent(JSContext *ctx, JSValueConst tree, const char *name, int &out)
    {
      JSAtom key = JS_NewAtom(ctx, name);
      if (key == JS_ATOM_NULL)
        return false;
      JSPropertyDescriptor property;
      const int found = JS_GetOwnProperty(ctx, &property, tree, key);
      JS_FreeAtom(ctx, key);
      if (found <= 0)
        return found == 0;
      JS_FreeValue(ctx, property.getter);
      JS_FreeValue(ctx, property.setter);
      const bool valid = boxExtentNumber(ctx, property.value, out);
      JS_FreeValue(ctx, property.value);
      return valid;
    }
    class BoxLowering : public IJsNodeLowering
    {
    public:
      BoxLowering()
      {
        this->name = "Box";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc < 1 || argc > 2 || !JS_IsObject(argv[0]) || JS_IsArray(argv[0]))
          return JS_ThrowTypeError(ctx, "Box(child, options?) requires one tree and an optional options object");
        JSValue childKind = JS_GetPropertyStr(ctx, argv[0], "kind");
        const bool valid = JS_IsNumber(childKind);
        JS_FreeValue(ctx, childKind);
        if (!valid)
          return JS_ThrowTypeError(ctx, "Box(child, options?) requires one tree and an optional options object");
        JSValue tree = newTree(ctx, this->kind);
        if (JS_IsException(tree))
          return tree;
        if (JS_SetPropertyStr(ctx, tree, "child", JS_DupValue(ctx, argv[0])) < 0
            || (argc == 2 && !copyBoxOptions(ctx, argv[1], tree)) || JS_FreezeObject(ctx, tree) < 0)
        {
          JS_FreeValue(ctx, tree);
          return JS_EXCEPTION;
        }
        return tree;
      }
      virtual loka::app::scene::NodeDefinitionBase *
      lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int depth)
      {
        loka::app::BoxDefinition *box = new (std::nothrow) loka::app::BoxDefinition();
        if (!box)
        {
          node.fail("JavaScript Box allocation refused.");
          return 0;
        }
        int width = 0, height = 0;
        const bool valid = readBoxExtent(ctx, tree, "width", width) && readBoxExtent(ctx, tree, "height", height);
        if (!valid)
        {
          delete box;
          node.fail("JavaScript Box has an invalid width or height.");
          return 0;
        }
        if (width)
          box->width(width);
        if (height)
          box->height(height);
        loka::app::scene::NodeDefinitionBase *content =
            lowerSingleChild(node, ctx, tree, depth, "JavaScript Box has an invalid child.");
        if (!content)
        {
          delete box;
          return 0;
        }
        box->asNestableDefinition()->addOwnedChild(content);
        return box;
      }
    };
    bool copyImageOptions(JSContext *ctx, JSValueConst options, JSValue tree)
    {
      if (!requirePlainObject(ctx, options, "ImageView options"))
        return false;
      JsOwnProperties names(ctx);
      if (!names.read(options, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK))
        return false;
      // Zero is outside the accepted range and denotes an absent dimension.
      int width = 0, height = 0;
      for (uint32_t i = 0; i < names.count(); ++i)
      {
        JSValue key = JS_AtomToValue(ctx, names.atom(i));
        size_t length = 0;
        const char *name = JS_IsString(key) ? JS_ToCStringLen(ctx, &length, key) : 0;
        JS_FreeValue(ctx, key);
        const bool isWidth = name && length == 5 && !memcmp(name, "width", 5);
        const bool isHeight = name && length == 6 && !memcmp(name, "height", 6);
        const bool isFit = name && length == 3 && !memcmp(name, "fit", 3);
        if (name)
          JS_FreeCString(ctx, name);
        if (!isWidth && !isHeight && !isFit)
        {
          JS_ThrowTypeError(ctx, "Unknown ImageView options key");
          return false;
        }
        JSValue value = JS_GetProperty(ctx, options, names.atom(i));
        if (JS_IsException(value))
          return false;
        int normalized = 0;
        if (isFit)
        {
          size_t size = 0;
          const char *fit = JS_IsString(value) ? JS_ToCStringLen(ctx, &size, value) : 0;
          if (fit)
          {
            if (size == 7 && !memcmp(fit, "contain", 7))
              normalized = loka::app::IMAGE_FIT_CONTAIN;
            else if (size == 5 && !memcmp(fit, "cover", 5))
              normalized = loka::app::IMAGE_FIT_COVER;
            else if (size == 7 && !memcmp(fit, "stretch", 7))
              normalized = loka::app::IMAGE_FIT_STRETCH;
            JS_FreeCString(ctx, fit);
          }
        }
        else
        {
          double number = 0;
          if (JS_IsNumber(value) && !JS_ToFloat64(ctx, &number, value) && number >= 1 && number <= 4096
              && number == static_cast<int>(number))
            normalized = static_cast<int>(number);
        }
        JS_FreeValue(ctx, value);
        if (!normalized)
        {
          JS_ThrowTypeError(ctx,
                            isFit ? "ImageView fit must be contain, cover or stretch"
                                  : "ImageView width and height must be integers in 1..4096");
          return false;
        }
        if (isWidth)
          width = normalized;
        if (isHeight)
          height = normalized;
        if (JS_SetPropertyStr(
                ctx, tree, isWidth ? "width" : (isHeight ? "height" : "fit"), JS_NewInt32(ctx, normalized))
            < 0)
          return false;
      }
      if ((width == 0) != (height == 0))
      {
        JS_ThrowTypeError(ctx, "ImageView width and height must be provided together");
        return false;
      }
      return true;
    }
    class ImageViewLowering : public IJsNodeLowering
    {
    public:
      ImageViewLowering()
      {
        this->name = "ImageView";
        this->kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc < 1 || argc > 2)
          return JS_ThrowTypeError(ctx, "ImageView(imageSeat, options?) requires a seat and optional options");
        if (argc == 1)
          return seatTree(ctx, this->kind, argv[0]);
        JSValue tree = newTree(ctx, this->kind);
        if (JS_IsException(tree))
          return tree;
        if (JS_SetPropertyStr(ctx, tree, "seat", JS_DupValue(ctx, argv[0])) < 0 || !copyImageOptions(ctx, argv[1], tree)
            || JS_FreezeObject(ctx, tree) < 0)
        {
          JS_FreeValue(ctx, tree);
          return JS_EXCEPTION;
        }
        return tree;
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        return node.lowerImageView(ctx, tree);
      }
    };
    class ClickableLowering : public IJsNodeLowering
    {
    public:
      explicit ClickableLowering(bool cell)
          : cell_(cell)
      {
        name = cell ? "Cell" : "Button";
        kind = 0;
      }
      virtual JSValue build(JSContext *ctx, int argc, JSValueConst *argv)
      {
        if (argc != 2 || !JS_IsFunction(ctx, argv[1]))
          return JS_ThrowTypeError(ctx, "%s(label, handler) requires a label and function", name);
        JSValue n = newTree(ctx, kind);
        JS_SetPropertyStr(ctx, n, "label", JS_DupValue(ctx, argv[0]));
        JS_SetPropertyStr(ctx, n, "handler", JS_DupValue(ctx, argv[1]));
        if (!this->cell_)
          JS_SetPropertyStr(ctx, n, "enabled", JS_NewCFunction(ctx, &ScriptRuntime::enabled, "enabled", 1));
        JS_FreezeObject(ctx, n);
        return n;
      }
      virtual loka::app::scene::NodeDefinitionBase *lower(JsCardNode &node, JSContext *ctx, JSValueConst tree, int)
      {
        return node.lowerClickable(ctx, tree, this->cell_);
      }

    private:
      const bool cell_;
    };
  } // namespace
  bool RegisterSmirkyCardBindings(JsCardBindingRegistry &registry)
  {
    IJsGlobal card = {"card", &ScriptRuntime::card, 2};
    return registry.registerLowering(new (std::nothrow) StackLowering("VStack", StackLowering::STACK_COLUMN))
           && registry.registerLowering(new (std::nothrow) TextLowering())
           && registry.registerLowering(new (std::nothrow) EditTextLowering())
           && registry.registerLowering(new (std::nothrow) ClickableLowering(false))
           && registry.registerLowering(new (std::nothrow) StackLowering("Row", StackLowering::STACK_ROW))
           && registry.registerLowering(new (std::nothrow) MarkupLowering())
           && registry.registerLowering(new (std::nothrow) ClickableLowering(true))
           && registry.registerLowering(new (std::nothrow) GridLowering())
           && registry.registerLowering(new (std::nothrow) ShowLowering())
           && registry.registerLowering(new (std::nothrow) OpenFileDialogLowering())
           && registry.registerLowering(new (std::nothrow) ImageViewLowering())
           && registry.registerLowering(new (std::nothrow) ScrollViewLowering())
           && registry.registerLowering(new (std::nothrow) CardLowering())
           && registry.registerLowering(new (std::nothrow) StackLowering("ZStack", StackLowering::STACK_DEPTH))
           && registry.registerLowering(new (std::nothrow) BoxLowering()) && registry.registerGlobal(card);
  }
} // namespace smirkycard
