// ─────────────────────────────────────────────────────────────────────────────
// ObjectImage.cpp — pure helpers for the s44-unified object portrait field.
// See ObjectImage.hpp. GTK-free; sandbox-proven (test_object_image_pure.cpp).
// ─────────────────────────────────────────────────────────────────────────────
#include "ObjectImage.hpp"

#include "Iid.hpp"            // iid_kind_of / IidKind
#include "ProjectBundle.hpp"  // asset_path (pure fs path builder)

namespace Folio {

bool image_value_is_asset(const std::string& value) {
    return iid_kind_of(value) == IidKind::Asset;
}

std::string image_display_path(const std::string& value,
                               const ImagePool& pool, const fs::path& root) {
    if (!image_value_is_asset(value))
        return value;   // legacy external path (or "" unset) — passthrough

    const ImageFragment* f = pool.find(value);
    if (!f || f->deleted)
        return std::string{};   // dangling or hidden portrait → nothing to show
    return asset_path(root, f->iid, f->ext).string();
}

}  // namespace Folio
