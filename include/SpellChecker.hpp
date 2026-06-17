// ─────────────────────────────────────────────────────────────────────────────
// Folio — SpellChecker.hpp
// Thin C++ wrapper around the enchant-2 spell checking library.
//
// Lifecycle:
//   SpellChecker sc;
//   if (sc.load("en_US")) { ... }   // or "" for system locale
//   sc.check("misspeled")           // → false
//   sc.suggest("misspeled")         // → {"misspelled", ...}
//   sc.add_to_session("Frodo")      // ignore for this session only
//   sc.add_to_user_dict("Frodo")    // persist to user dictionary
//
// Requires: libenchant-2-dev   (apt install libenchant-2-dev)
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <enchant-2/enchant.h>
#include <string>
#include <vector>

namespace Folio {

class SpellChecker {
public:
    SpellChecker();
    ~SpellChecker();

    // Non-copyable — broker/dict pointers are not reference-counted
    SpellChecker(const SpellChecker&)            = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    // Load a dictionary for the given language tag (e.g. "en_US").
    // Pass "" to use the system locale (enchant_get_user_language).
    // Returns true on success.  Safe to call again to switch languages —
    // the previous dictionary is released first.
    bool load(const std::string& lang_tag);

    // True if a dictionary is currently loaded.
    bool is_loaded() const { return m_dict != nullptr; }

    // Returns the active language tag (empty if not loaded).
    const std::string& language() const { return m_language; }

    // Returns true if the word is spelled correctly (or no dict is loaded).
    // Word should be plain text with no surrounding whitespace.
    bool check(const std::string& word) const;

    // Returns a list of suggestions for a misspelled word.
    // Returns an empty vector if the word is correct or no dict is loaded.
    std::vector<std::string> suggest(const std::string& word) const;

    // Ignore this word for the current session only (not persisted).
    void add_to_session(const std::string& word);

    // Add word to the user's personal dictionary (persisted across sessions).
    void add_to_user_dict(const std::string& word);

    // Remove a word from the user's personal dictionary.
    void remove_from_user_dict(const std::string& word);

    // Returns the last error message from enchant, or "".
    std::string last_error() const;

    // Returns all available dictionary language tags on this system.
    static std::vector<std::string> available_languages();

private:
    void release_dict();

    EnchantBroker* m_broker   = nullptr;
    EnchantDict*   m_dict     = nullptr;
    std::string    m_language;
};

} // namespace Folio
