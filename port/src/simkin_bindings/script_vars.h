#pragma once

// Post-M120: a script object's variables, as the save file carries them.
//
// The entity root's save (`FUN_10066614`) and the level's
// (`FUN_10071ac4`) both end the same way: walk the object's SimKin tree
// node, and for every child write its label and its data as two wide
// strings behind a `1` tag -- **but only when the data contains no `{`**
// (`FUN_100677cc`), which is how a method body (`Init[ (s) { ... } ]`) is
// told apart from a field (`saved_Open [0]`). The list ends with `0xff`.
// The loaders write each pair straight back into the node before anything
// calls the object, which is what lets `door.s`'s `Init` see the
// `saved_Open` it had when the game was saved.
//
// A field written at run time lands in the same place: Simkin's
// `skTreeNodeObject::setValue` stores `value.str()` as the child's data,
// so collecting the children is collecting the script's whole state.

#include <vector>

#include "assets/save_records.h"

class skTreeNodeObject;

namespace sk_bindings {

std::vector<sk::SavedScriptVar> CollectScriptVars(skTreeNodeObject& object);

// Overwrites each named child's data, adding the child if the script
// never declared it (a field first written by another script's
// `Level.saved_X = 1`, for instance).
void RestoreScriptVars(skTreeNodeObject& object, const std::vector<sk::SavedScriptVar>& vars);

}  // namespace sk_bindings
