#ifndef SMIRKYCARD_JS_CARD_BINDING_REGISTRY_HPP
#define SMIRKYCARD_JS_CARD_BINDING_REGISTRY_HPP

#include "quickjs.h"

namespace loka
{
  namespace app
  {
    struct TextStyle;
    struct BlockStyle;
    namespace scene
    {
      struct NodeDefinitionBase;
    }
  } // namespace app
} // namespace loka

namespace smirkycard
{
  class JsCardNode;
  struct JsToolbarRow;

  /** Read a plain style dictionary; leave out unchanged and throw on refusal. */
  bool readTextStyle(JSContext *context, JSValueConst dict, loka::app::TextStyle &out);

  /** Read a separate block dictionary; leave out unchanged and throw on refusal. */
  bool readBlockStyle(JSContext *context, JSValueConst dict, loka::app::BlockStyle &out);

  /** Require an ordinary or null-prototype dictionary, as used by style readers. */
  bool requirePlainObject(JSContext *context, JSValueConst dict, const char *label);

  struct IJsNodeLowering
  {
    const char *name;
    const char *parent;
    int kind;
    virtual ~IJsNodeLowering() {}
    virtual JSValue build(JSContext *context, int argc, JSValueConst *argv) = 0;
    virtual loka::app::scene::NodeDefinitionBase *
    lower(JsCardNode &node, JSContext *context, JSValueConst tree, int depth) = 0;
  };

  struct IJsGlobal
  {
    const char *name;
    JSCFunction *fn;
    int length;
  };

  /** Per-runtime map between the JavaScript tree ABI and Loka lowerings. */
  class JsCardBindingRegistry
  {
  public:
    JsCardBindingRegistry()
        : lowerings_(0),
          globals_(0),
          nextKind_(1)
    {
    }
    ~JsCardBindingRegistry();
    bool registerLowering(IJsNodeLowering *lowering);
    bool registerGlobal(const IJsGlobal &global);
    IJsNodeLowering *lookup(int kind) const;
    bool install(JSContext *context) const;
    /** Validate descriptor facts at build and lower; seat provenance is card-owned. */
    static bool readToolbarDescriptor(JSContext *context, JSValueConst tree, JsToolbarRow &row);

  private:
    struct LoweringEntry;
    struct GlobalEntry;
    LoweringEntry *lowerings_;
    GlobalEntry *globals_;
    int nextKind_;
    static JSValue build(JSContext *context, JSValueConst, int argc, JSValueConst *argv, int magic);
    JsCardBindingRegistry(const JsCardBindingRegistry &);
    JsCardBindingRegistry &operator=(const JsCardBindingRegistry &);
  };

  bool RegisterSmirkyCardBindings(JsCardBindingRegistry &registry);
} // namespace smirkycard

#endif
