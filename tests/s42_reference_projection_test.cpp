// s42 "The Reference" — pure tests for bringing References into the form world.
//
// Verifies the projection refactor that generalized the binary `bool is_place`
// seam into a floor-type ("character"/"place"/"reference"), the new Reference
// floor built-in, and that the character/place paths are unregressed. The store
// IS the projection of binder leaves; these tests exercise the pure intake seam
// (add_migrated_leaf / migrate_legacy_leaf / resolve_leaf_type) directly, the
// same seam DocumentModel::rebuild_object_store feeds for all three sections.
//
/*
g++ -std=c++20 -I../include -I/home/claude/sbox s42_reference_projection_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/s42_test
/tmp/s42_test
clang++ -std=c++20 -I../include -I/home/claude/sbox s42_reference_projection_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/s42_test_clang
/tmp/s42_test_clang
*/

#include "Object.hpp"
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (cond) { ++g_pass; }                                                     \
    else { ++g_fail;                                                            \
      std::cerr << "FAIL: " << (msg) << "  [" << __LINE__ << "]\n"; }           \
  } while (0)

static std::string sval(const Object& o, const std::string& k) {
  json v = o.value_or(k, json(""));
  return v.is_string() ? v.get<std::string>() : std::string{};
}

int main() {
  // ── 1. The Reference floor built-in exists and is well-shaped ──────────────
  {
    Template t = built_in_reference_template();
    CHECK(t.id == "reference",            "reference floor id");
    CHECK(t.type_name == "Reference",     "reference floor type_name");
    CHECK(t.category == "reference",      "reference floor category");
    CHECK(t.builtin == true,              "reference floor is builtin/locked");
    // Same floor shape as character/place: name/image/description.
    bool has_name = false, has_img = false, has_desc = false;
    for (const auto& f : t.fields) {
      if (f.id == "name")        has_name = true;
      if (f.id == "image")       has_img  = true;
      if (f.id == "description") has_desc = true;
    }
    CHECK(has_name && has_img && has_desc, "reference floor has the 3 floor fields");
  }

  // ── 2. seed_builtins installs all three floors ─────────────────────────────
  {
    ObjectStore store;
    store.seed_builtins();
    CHECK(store.has_template("character"), "seed: character present");
    CHECK(store.has_template("place"),     "seed: place present");
    CHECK(store.has_template("reference"), "seed: reference present");
  }

  // ── 3. resolve_leaf_type honours the floor-type string ─────────────────────
  {
    ObjectStore store;
    store.seed_builtins();
    CHECK(store.resolve_leaf_type("reference", "") == "reference", "resolve ref floor");
    CHECK(store.resolve_leaf_type("character", "") == "character", "resolve char floor");
    CHECK(store.resolve_leaf_type("place", "")     == "place",     "resolve place floor");
    // Empty floor_type defaults to character (defensive).
    CHECK(store.resolve_leaf_type("", "") == "character", "empty floor_type -> character");
    // A built-in id is never an adopted type — falls to the floor.
    CHECK(store.resolve_leaf_type("reference", "reference") == "reference",
          "builtin id resolves to floor, not itself");
  }

  // ── 4. A Reference leaf projects with floor fields mapped ──────────────────
  {
    ObjectStore store;
    store.seed_builtins();
    store.add_migrated_leaf("ref1", "reference",
                            "On Photography",                 // title -> name
                            "<p>Sontag, 1977</p>",            // content -> description
                            "/img/cover.png",                 // image_path -> image
                            "",                               // tagline (orphan)
                            "",                               // role (orphan)
                            "");                              // template_id (floor)
    const Object* o = store.find_object("ref1");
    CHECK(o != nullptr,                            "ref projected");
    CHECK(o && o->type == "reference",             "ref object type is reference");
    CHECK(o && o->projected,                       "ref marked projected");
    CHECK(o && sval(*o, "name") == "On Photography",        "ref name<-title");
    CHECK(o && sval(*o, "description") == "<p>Sontag, 1977</p>", "ref description<-content");
    CHECK(o && sval(*o, "image") == "/img/cover.png",       "ref image<-image_path");
    // objects_of_type filters by the reference type.
    CHECK(store.objects_of_type("reference").size() == 1, "one reference of type");
    CHECK(store.objects_of_type("character").empty(),     "no characters yet");
  }

  // ── 5. Regression: character + place still project through the string seam ─
  {
    ObjectStore store;
    store.seed_builtins();
    store.add_migrated_leaf("c1", "character", "Mara", "<p>bio</p>", "",
                            "", "Protagonist", "");
    store.add_migrated_leaf("p1", "place", "The Citadel", "<p>desc</p>", "",
                            "Remote fortress", "", "");
    const Object* c = store.find_object("c1");
    const Object* p = store.find_object("p1");
    CHECK(c && c->type == "character",            "char type");
    CHECK(c && sval(*c, "name") == "Mara",        "char name<-title");
    CHECK(c && sval(*c, "role") == "Protagonist", "char role orphan carried");
    CHECK(p && p->type == "place",                "place type");
    CHECK(p && sval(*p, "tagline") == "Remote fortress", "place tagline orphan carried");
  }

  // ── 6. Orphan-and-keep: a custom field on a cloned reference survives ──────
  {
    ObjectStore store;
    store.seed_builtins();
    // Clone the reference floor to an editable type, add a custom field.
    const std::string clone = store.clone_template("reference");
    CHECK(!clone.empty(), "reference cloned to editable type");
    Template t = *store.find_template(clone);
    t.fields.push_back({ "year", FieldType::Text, "Year", json::object() });
    store.upsert_template(t);

    // First projection: leaf adopts the clone, custom field seeded (empty).
    store.add_migrated_leaf("ref2", "reference", "Title", "<p>x</p>", "",
                            "", "", clone);
    Object* o = store.find_object("ref2");
    CHECK(o && o->type == clone, "ref adopted the clone type");
    o->set_value("year", "1977");   // user fills the custom field

    // Re-project (rebuild) — floor fields restamp from the leaf, custom survives.
    store.add_migrated_leaf("ref2", "reference", "Title (edited)", "<p>x</p>", "",
                            "", "", clone);
    const Object* o2 = store.find_object("ref2");
    CHECK(o2 && sval(*o2, "name") == "Title (edited)", "floor restamped on rebuild");
    CHECK(o2 && sval(*o2, "year") == "1977",           "custom field survives reconcile");
  }

  std::cout << "s42_reference_projection_test: " << g_pass << " passed, "
            << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
