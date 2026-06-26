// ─────────────────────────────────────────────────────────────────────────────
// ImagePool.cpp — the shared image pool (see ImagePool.hpp). Pure; std-lib +
// nlohmann::json only, mirroring JournalLog's serialization discipline.
// ─────────────────────────────────────────────────────────────────────────────

#include "ImagePool.hpp"

#include <nlohmann/json.hpp>

#include "Iid.hpp"

namespace Folio {

namespace {
using json = nlohmann::json;
}  // namespace

std::string ImagePool::add(ImageFragment f) {
  if (f.iid.empty())
    f.iid = make_iid(IidKind::Asset);
  const std::string iid = f.iid;
  m_fragments.push_back(std::move(f));
  return iid;
}

void ImagePool::soft_delete(const std::string& iid) {
  if (ImageFragment* f = find(iid))
    f->deleted = true;
}

void ImagePool::restore(const std::string& iid) {
  if (ImageFragment* f = find(iid))
    f->deleted = false;
}

bool ImagePool::purge(const std::string& iid) {
  for (auto it = m_fragments.begin(); it != m_fragments.end(); ++it)
    if (it->iid == iid) {
      m_fragments.erase(it);
      return true;
    }
  return false;
}

bool ImagePool::link_object(const std::string& image_iid,
                            const std::string& object_iid) {
  if (object_iid.empty())
    return false;
  ImageFragment* f = find(image_iid);
  if (!f)
    return false;
  for (const std::string& l : f->links)
    if (l == object_iid)
      return false;  // already linked
  f->links.push_back(object_iid);
  return true;
}

bool ImagePool::unlink_object(const std::string& image_iid,
                              const std::string& object_iid) {
  ImageFragment* f = find(image_iid);
  if (!f)
    return false;
  for (auto it = f->links.begin(); it != f->links.end(); ++it)
    if (*it == object_iid) {
      f->links.erase(it);
      return true;
    }
  return false;
}

ImageFragment* ImagePool::find(const std::string& iid) {
  for (ImageFragment& f : m_fragments)
    if (f.iid == iid)
      return &f;
  return nullptr;
}

const ImageFragment* ImagePool::find(const std::string& iid) const {
  for (const ImageFragment& f : m_fragments)
    if (f.iid == iid)
      return &f;
  return nullptr;
}

std::vector<std::size_t> ImagePool::live_view() const {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < m_fragments.size(); ++i)
    if (!m_fragments[i].deleted)
      out.push_back(i);
  return out;
}

json ImagePool::to_json() const {
  json arr = json::array();
  for (const ImageFragment& f : m_fragments) {
    if (f.iid.empty())  // a fragment with no identity has no asset; never write it
      continue;
    arr.push_back({{"iid", f.iid},
                   {"ext", f.ext},
                   {"caption", f.caption},
                   {"accent", f.accent},
                   {"mood", f.mood},
                   {"source_url", f.source_url},
                   {"format", f.format},
                   {"width", f.width},
                   {"height", f.height},
                   {"bytes", f.bytes},
                   {"hash", f.hash},
                   {"flattened_animation", f.flattened_animation},
                   {"deleted", f.deleted},
                   {"links", f.links}});
  }
  return arr;
}

ImagePool ImagePool::from_json(const json& arr) {
  ImagePool pool;
  if (!arr.is_array())  // absent / null / object → empty; assets recovered via §9
    return pool;

  for (const auto& jf : arr) {
    if (!jf.is_object())
      continue;
    ImageFragment f;
    f.iid = jf.value("iid", std::string{});
    if (f.iid.empty())  // an entry with no identity references no asset
      continue;
    f.ext = jf.value("ext", std::string{});
    f.caption = jf.value("caption", std::string{});
    f.accent = jf.value("accent", std::string{});
    f.mood = jf.value("mood", std::string{});
    f.source_url = jf.value("source_url", std::string{});
    f.format = jf.value("format", std::string{});
    f.width = jf.value("width", 0);
    f.height = jf.value("height", 0);
    f.bytes = jf.value("bytes", static_cast<long long>(0));
    f.hash = jf.value("hash", std::string{});
    f.flattened_animation = jf.value("flattened_animation", false);
    f.deleted = jf.value("deleted", false);
    if (jf.contains("links") && jf["links"].is_array())
      for (const auto& l : jf["links"])
        if (l.is_string())
          f.links.push_back(l.get<std::string>());
    pool.m_fragments.push_back(std::move(f));
  }
  return pool;
}

}  // namespace Folio
