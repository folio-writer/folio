// ─────────────────────────────────────────────────────────────────────────────
// KpPalette.cpp — resolve_kp + backfill_swatch_ids (s81 slice 2). Pure.
// See KpPalette.hpp for the contract and the "ordinal = palette position" idea.
// ─────────────────────────────────────────────────────────────────────────────

#include "KpPalette.hpp"

namespace Folio {

KpResolution resolve_kp(const std::vector<KpSwatch>& palette,
                        const std::string& kp_id) {
  if (kp_id.empty()) return {};   // untagged → no resolution.
  for (std::size_t i = 0; i < palette.size(); ++i) {
    if (palette[i].id == kp_id) {
      KpResolution r;
      r.found     = true;
      r.color_idx = static_cast<int>(i) + 1;  // 1-based position = arc ordinal.
      r.label     = palette[i].name;
      r.hex       = palette[i].hex;
      return r;                               // first match wins (determinism).
    }
  }
  return {};   // dangling id — swatch was deleted.
}

int backfill_swatch_ids(std::vector<KpSwatch>& palette,
                        const std::function<std::string()>& gen) {
  int changed = 0;
  for (auto& sw : palette) {
    if (sw.id.empty()) {
      sw.id = gen();
      ++changed;
    }
  }
  return changed;
}

std::vector<int> palette_remap(const std::vector<std::string>& old_ids,
                               const std::vector<std::string>& new_ids) {
  std::vector<int> remap(old_ids.size() + 1, 0);   // remap[0] = 0 (None → None).
  for (std::size_t i = 0; i < old_ids.size(); ++i) {
    // Find this old swatch's id in the new order → its new 1-based position.
    for (std::size_t j = 0; j < new_ids.size(); ++j) {
      if (!old_ids[i].empty() && new_ids[j] == old_ids[i]) {
        remap[i + 1] = static_cast<int>(j) + 1;
        break;
      }
    }
    // left 0 if the id is gone (deleted) or blank.
  }
  return remap;
}

int apply_palette_remap(const std::vector<int>& remap,
                        std::vector<SceneKpRef>& scenes) {
  int changed = 0;
  for (auto& s : scenes) {
    if (s.color_idx <= 0) continue;                       // None stays None.
    const int nw = (s.color_idx < static_cast<int>(remap.size()))
                       ? remap[s.color_idx] : 0;
    if (nw == 0) {                                        // swatch deleted → None
      s.kp_id.clear();
      s.color_idx    = 0;
      s.kp_label.clear();
      s.is_key_point = false;
      s.pin          = false;
      ++changed;
    } else if (nw != s.color_idx) {                       // swatch moved → follow
      s.color_idx = nw;
      ++changed;
    }
  }
  return changed;
}

}  // namespace Folio
