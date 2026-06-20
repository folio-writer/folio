// ─────────────────────────────────────────────────────────────────────────────
// Folio — clone_template_test.cpp   (s34 — locked built-ins + clone-to-edit)
//
// The authoring model: built-in templates are LOCKED (immutable floor/seed types,
// reseeded pristine every rebuild), and the ONLY way to author a custom type is to
// CLONE one and edit the copy. This kills the empty-canvas problem (you always
// start from a real sheet) and keeps the notecard sacrosanct (Christie can't wander
// into a schema editor). This test pins:
//   • built-ins carry builtin=true and round-trip through ObjectIO;
//   • seed_builtins RESEEDS (a tampered built-in is restored to pristine);
//   • clone_template mints a fresh editable tpl_ type, copying the schema;
//   • template_is_editable gates the builder (built-in no, clone yes).
//
// PURE: ObjectStore / ObjectIO / Object / Iid / nlohmann::json — sandbox-compilable.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/clone_template_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/clone_template_test
/tmp/clone_template_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/clone_template_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/clone_template_test
/tmp/clone_template_test
*/

#include "ObjectStore.hpp"
#include "ObjectIO.hpp"
#include "TemplateEdit.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace Folio;
namespace TE = Folio::TemplateEdit;

static int checks = 0;
static void check(bool cond, const std::string& what) {
    ++checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}

int main() {
    // ── Built-ins are locked, and that flag round-trips ───────────────────────
    Template ch = built_in_character_template();
    check(ch.builtin, "built-in character is builtin");
    check(!template_is_editable(ch), "built-in is not editable");
    Template ro = ObjectIO::template_from_string(ObjectIO::template_to_string(ch));
    check(ro.builtin, "builtin flag survives round-trip");

    // A user type (no flag) round-trips as editable.
    Template user; user.id = "tpl_x"; user.type_name = "Species";
    user.fields = { { "name", FieldType::Text, "Name", json::object() } };
    check(template_is_editable(user), "user type is editable");
    Template uro = ObjectIO::template_from_string(ObjectIO::template_to_string(user));
    check(!uro.builtin, "absent builtin defaults to false");

    // ── seed_builtins RESEEDS a tampered built-in to pristine ─────────────────
    ObjectStore store;
    store.seed_builtins();
    check(store.find_template("character")->builtin, "seed marks character builtin");
    check(store.find_template("character")->fields.size() == 3, "floor has 3 fields");

    // Tamper: someone replaced the character template with a mangled, unlocked one.
    Template mangled = *store.find_template("character");
    mangled.builtin = false;
    mangled.fields.clear();
    mangled.type_name = "Hacked";
    store.upsert_template(mangled);
    check(store.find_template("character")->fields.empty(), "tamper took effect");

    store.seed_builtins();   // a rebuild reseeds
    check(store.find_template("character")->builtin, "reseed restored the lock");
    check(store.find_template("character")->fields.size() == 3, "reseed restored the floor");
    check(store.find_template("character")->type_name == "Character", "reseed restored the name");

    // ── clone_template mints a fresh editable type, copying the schema ────────
    // Build out the character a bit first (as a seed sheet might be), then clone.
    Template rich = built_in_character_template();
    TE::add_field(rich, FieldType::Text,   "Species");
    TE::add_field(rich, FieldType::Number, "Age");
    store.upsert_template(rich);   // (still id "character"; reseed would reset, but we clone now)

    std::string new_id = store.clone_template("character");
    check(!new_id.empty(), "clone returned an id");
    check(new_id.rfind("tpl_", 0) == 0, "clone id is a tpl_ id");
    check(new_id != "character", "clone id differs from source");

    const Template* clone = store.find_template(new_id);
    check(clone != nullptr, "clone is registered");
    check(!clone->builtin, "clone is editable (not locked)");
    check(template_is_editable(*clone), "clone passes editability gate");
    check(clone->type_name == "Copy of Character", "clone is named Copy of ...");
    check(clone->fields.size() == store.find_template("character")->fields.size(),
          "clone copied the full schema");
    check(clone->find_field("species") != nullptr && clone->find_field("age") != nullptr,
          "clone carries the built-out fields");

    // The source is untouched by the clone.
    check(store.find_template("character")->builtin, "source still locked after clone");

    // Cloning an unknown id is a no-op returning "".
    check(store.clone_template("nope").empty(), "clone of unknown id -> empty");

    // Two clones get distinct ids.
    std::string a = store.clone_template("place");
    std::string b = store.clone_template("place");
    check(!a.empty() && !b.empty() && a != b, "successive clones get distinct ids");

    std::cout << "clone_template_test: all " << checks << " checks passed\n";
    return 0;
}
