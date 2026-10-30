#include "simkin_bindings/script_vars.h"

#include "simkin_bindings/native_binding_common.h"
#include "skTreeNode.h"
#include "skTreeNodeObject.h"

namespace sk_bindings {

std::vector<sk::SavedScriptVar> CollectScriptVars(skTreeNodeObject& object) {
    std::vector<sk::SavedScriptVar> vars;
    skTreeNode* node = object.getNode();
    if (!node) return vars;
    skTreeNodeListIterator it(*node);
    while (skTreeNode* child = it()) {
        std::string value = ToStdString(child->data());
        // FUN_100677cc: a value with a `{` in it is code, not state.
        if (value.find('{') != std::string::npos) continue;
        vars.push_back({ToStdString(child->label()), std::move(value)});
    }
    return vars;
}

void RestoreScriptVars(skTreeNodeObject& object, const std::vector<sk::SavedScriptVar>& vars) {
    skTreeNode* node = object.getNode();
    if (!node) return;
    for (const sk::SavedScriptVar& var : vars) {
        const skString label(var.name.c_str());
        const skString data(var.value.c_str());
        if (skTreeNode* child = node->findChild(label)) {
            child->data(data);
        } else {
            node->addChild(skNEW(skTreeNode(label, data)));
        }
    }
}

}  // namespace sk_bindings
