//
// folioedit :: Passphrase -- see Passphrase.hpp. Pure STL only.
//
// The pools below are a STARTER set: genuinely absurd words that are funny
// because they collide (disgruntled livestock, damp office equipment, unionizing
// otters). They are DATA -- grow each toward ~200 to lift the entropy floor
// (see phrase_entropy_bits); the generator + canonicalizer don't change. Words
// are single-token, lowercase ASCII, and chosen to read cleanly (no l/1, rn/m
// lookalike traps). Keep them lowercase ASCII so canonicalization is total.
//
#include "folioedit/Passphrase.hpp"

#include <array>
#include <cmath>
#include <random>

namespace folioedit {

namespace {

// subject: a plural noun that can be the agent of an absurd little sentence.
const std::vector<std::string> kSubject = {
    "lambs", "otters", "magpies", "penguins", "walruses", "hedgehogs",
    "raccoons", "llamas", "toddlers", "accountants", "pirates", "wizards",
    "nuns", "clowns", "philosophers", "goldfish", "grandmothers", "lobsters",
    "pigeons", "hamsters", "bishops", "squirrels", "ferrets", "weasels",
    "gnomes", "wombats", "badgers", "herons", "newts", "geese", "moths",
    "beetles", "sparrows", "minnows", "foxes", "puffins", "seagulls",
    "senators", "lemurs", "monks", "jugglers", "plumbers", "librarians",
    "tourists", "mimes", "yodelers", "barons", "interns", "hippos", "yaks",
    "ducklings", "octopuses", "peacocks", "marmots", "muskrats", "auditors",
    "poets", "sommeliers", "cartographers", "beekeepers", "trombonists",
    "understudies", "custodians", "acrobats",
};

// verb: a transitive-ish present-tense action, the sillier the better.
const std::vector<std::string> kVerb = {
    "chase", "juggle", "interrogate", "outrun", "bribe", "headbutt",
    "serenade", "misplace", "impersonate", "befriend", "evict", "smuggle",
    "tickle", "overthrow", "reorganize", "photograph", "confront", "elbow",
    "marinate", "hypnotize", "unionize", "lecture", "heckle", "audition",
    "flummox", "bamboozle", "deputize", "upstage", "waltz", "smother",
    "pickpocket", "ambush", "gaslight", "micromanage", "negotiate", "startle",
    "outwit", "moonwalk", "filibuster", "chaperone", "harass", "worship",
    "outbid", "nudge", "unsettle", "quarantine", "recruit", "dethrone",
    "cross-examine", "outvote", "flambe", "reschedule", "counsel", "provoke",
    "escort", "swindle", "haggle", "yodel", "outmaneuver", "annoy",
    "compliment", "arrest", "outdance", "tolerate",
};

// adjective: a modifier that makes the object more preposterous.
const std::vector<std::string> kAdjective = {
    "speeding", "suspicious", "velvet", "inflatable", "disappointed",
    "radioactive", "tiny", "smug", "damp", "aristocratic", "feral",
    "flammable", "haunted", "discount", "bewildered", "unlicensed",
    "sarcastic", "enormous", "soggy", "majestic", "reluctant", "caffeinated",
    "ornery", "translucent", "lopsided", "pompous", "drowsy", "immortal",
    "counterfeit", "gelatinous", "overdue", "theatrical", "wholesome",
    "vengeful", "gullible", "brittle", "molten", "patient", "tidal",
    "argumentative", "sunburnt", "unemployed", "invisible", "clumsy",
    "melancholy", "combustible", "gigantic", "sticky", "nervous", "regal",
    "moldy", "punctual", "furious", "bashful", "waterproof", "sentient",
    "expired", "smoldering", "delinquent", "buoyant", "crusty", "hysterical",
    "ceremonial",
};

// object: a plural inanimate noun to be chased, juggled, or overthrown.
const std::vector<std::string> kObject = {
    "cars", "kettles", "ladders", "accordions", "lawnmowers", "escalators",
    "turnips", "trombones", "bureaucrats", "umbrellas", "mannequins",
    "lighthouses", "waffles", "helicopters", "spreadsheets", "bagpipes",
    "trampolines", "chandeliers", "casseroles", "harmonicas", "ottomans",
    "pianos", "staplers", "thermostats", "gargoyles", "vuvuzelas", "kazoos",
    "submarines", "wheelbarrows", "tubas", "doorknobs", "pamphlets",
    "flamingos", "baguettes", "hovercrafts", "typewriters", "monocles",
    "canoes", "pinatas", "footstools", "harpsichords", "unicycles",
    "beanbags", "chandlers", "gondolas", "birdbaths", "cummerbunds",
    "chesterfields", "meringues", "windmills", "teapots", "banisters",
    "gramophones", "trebuchets", "spatulas", "zeppelins", "toboggans",
    "clarinets", "walkers", "sundials", "haystacks", "petticoats", "gazebos",
};

}  // namespace

const std::vector<std::string>& pool(Slot s) {
    switch (s) {
        case Slot::Subject:   return kSubject;
        case Slot::Verb:      return kVerb;
        case Slot::Adjective: return kAdjective;
        case Slot::Object:    return kObject;
    }
    return kSubject;   // unreachable; keeps the compiler happy under -Werror
}

std::string canonicalize_passphrase(const std::string& phrase) {
    std::string out;
    out.reserve(phrase.size());
    for (unsigned char c : phrase) {
        // strip the separators the human might type between words
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '-' || c == '_') {
            continue;
        }
        // lowercase ASCII A-Z only; leave any non-ASCII byte untouched
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string generate_passphrase(const IndexRng& pick) {
    const std::array<Slot, 4> order = {
        Slot::Subject, Slot::Verb, Slot::Adjective, Slot::Object};
    std::string phrase;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::vector<std::string>& p = pool(order[i]);
        const std::size_t idx = pick(p.size());   // caller guarantees [0, size)
        if (i != 0) phrase.push_back(' ');
        phrase += p[idx % p.size()];               // defensive clamp
    }
    return phrase;
}

std::string generate_passphrase() {
    // Platform CSPRNG per draw (urandom-backed on Fedora). Kept behind the
    // injectable overload so tests stay deterministic and this TU stays pure.
    static thread_local std::random_device rd;
    IndexRng pick = [](std::size_t n) -> std::size_t {
        std::uniform_int_distribution<std::size_t> dist(0, n - 1);
        return dist(rd);
    };
    return generate_passphrase(pick);
}

double phrase_entropy_bits() {
    const double combos =
        static_cast<double>(kSubject.size()) *
        static_cast<double>(kVerb.size()) *
        static_cast<double>(kAdjective.size()) *
        static_cast<double>(kObject.size());
    return std::log2(combos);
}

}  // namespace folioedit
