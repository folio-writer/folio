// ─────────────────────────────────────────────────────────────────────────────
// Folio — SpellChecker.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "SpellChecker.hpp"
#include <cstring>
#include <glib.h>

namespace Folio {

SpellChecker::SpellChecker() {
    m_broker = enchant_broker_init();
}

SpellChecker::~SpellChecker() {
    release_dict();
    if (m_broker) {
        enchant_broker_free(m_broker);
        m_broker = nullptr;
    }
}

bool SpellChecker::load(const std::string& lang_tag) {
    if (!m_broker) return false;
    release_dict();

    std::string lang = lang_tag;
    if (lang.empty()) {
        // Use GLib to get the system language — first entry is most specific
        const gchar* const* names = g_get_language_names();
        if (names && names[0] && g_strcmp0(names[0], "C") != 0)
            lang = names[0];
    }
    if (lang.empty()) lang = "en_US";

    m_dict = enchant_broker_request_dict(m_broker, lang.c_str());
    if (!m_dict) return false;

    m_language = lang;
    return true;
}

void SpellChecker::release_dict() {
    if (m_dict && m_broker) {
        enchant_broker_free_dict(m_broker, m_dict);
        m_dict = nullptr;
    }
    m_language.clear();
}

bool SpellChecker::check(const std::string& word) const {
    if (!m_dict || word.empty()) return true;
    int result = enchant_dict_check(m_dict, word.c_str(), (ssize_t)word.size());
    return result == 0;
}

std::vector<std::string> SpellChecker::suggest(const std::string& word) const {
    std::vector<std::string> results;
    if (!m_dict || word.empty()) return results;
    size_t count = 0;
    char** raw = enchant_dict_suggest(m_dict, word.c_str(),
                                      (ssize_t)word.size(), &count);
    if (!raw) return results;
    results.reserve(count);
    for (size_t i = 0; i < count; ++i)
        results.emplace_back(raw[i]);
    enchant_dict_free_string_list(m_dict, raw);
    return results;
}

void SpellChecker::add_to_session(const std::string& word) {
    if (m_dict && !word.empty())
        enchant_dict_add_to_session(m_dict, word.c_str(), (ssize_t)word.size());
}

void SpellChecker::add_to_user_dict(const std::string& word) {
    if (m_dict && !word.empty())
        enchant_dict_add(m_dict, word.c_str(), (ssize_t)word.size());
}

void SpellChecker::remove_from_user_dict(const std::string& word) {
    if (m_dict && !word.empty())
        enchant_dict_remove(m_dict, word.c_str(), (ssize_t)word.size());
}

std::string SpellChecker::last_error() const {
    if (!m_broker) return "no broker";
    const char* e = enchant_broker_get_error(m_broker);
    return e ? std::string(e) : "";
}

// ─────────────────────────────────────────────────────────────────────────────
// available_languages  (static)
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    void dict_describe_cb(const char* lang_tag,
                          const char* /*provider_name*/,
                          const char* /*provider_desc*/,
                          const char* /*provider_file*/,
                          void*        user_data) {
        auto* langs = static_cast<std::vector<std::string>*>(user_data);
        if (lang_tag) langs->emplace_back(lang_tag);
    }
}

std::vector<std::string> SpellChecker::available_languages() {
    std::vector<std::string> langs;
    EnchantBroker* broker = enchant_broker_init();
    if (!broker) return langs;
    enchant_broker_list_dicts(broker, dict_describe_cb, &langs);
    enchant_broker_free(broker);
    return langs;
}

} // namespace Folio
