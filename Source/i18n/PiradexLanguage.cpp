#include "PiradexLanguage.h"
#include <juce_data_structures/juce_data_structures.h>
#include <unordered_map>
#include <string>

namespace piradex::i18n
{
namespace
{
    std::atomic<int> lang { -1 };

    juce::PropertiesFile& settings()
    {
        static std::unique_ptr<juce::PropertiesFile> props = []
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Piradex";
            o.folderName          = "Piradex";
            o.filenameSuffix      = ".settings";
            o.osxLibrarySubFolder = "Application Support";
            o.storageFormat       = juce::PropertiesFile::storeAsXML;
            return std::make_unique<juce::PropertiesFile> (o);
        }();
        return *props;
    }

    juce::ListenerList<Listener>& listeners()
    {
        static juce::ListenerList<Listener> l;
        return l;
    }

    struct Tables
    {
        std::unordered_map<std::string, const Entry*> byEn, byPt;
    };
    Tables& tables() { static Tables t; return t; }

    Language fromSettingsOrSystem()
    {
        const auto stored = settings().getValue ("language");
        if (stored == "pt") return Language::PT;
        if (stored == "en") return Language::EN;
        if (stored == "zh") return Language::ZH;
        const auto sys = juce::SystemStats::getUserLanguage().toLowerCase();
        if (sys.startsWith ("pt")) return Language::PT;
        if (sys.startsWith ("zh")) return Language::ZH;
        return Language::EN;
    }

    const char* codeOf (Language l) { return l == Language::PT ? "pt" : (l == Language::ZH ? "zh" : "en"); }
}

Language current() noexcept
{
    int v = lang.load();
    if (v < 0)
    {
        v = (int) fromSettingsOrSystem();
        lang.store (v);
    }
    return (Language) v;
}

bool isPortuguese() noexcept { return current() == Language::PT; }
bool isChinese() noexcept    { return current() == Language::ZH; }
juce::String code()          { return codeOf (current()); }

juce::String languageName (Language l)
{
    if (l == Language::PT) return juce::String::fromUTF8 ("Portugu\xc3\xaas");
    if (l == Language::ZH) return juce::String::fromUTF8 ("\xe4\xb8\xad\xe6\x96\x87");
    return "English";
}

void set (Language l, bool persist)
{
    if (persist)
    {
        settings().setValue ("language", codeOf (l));
        settings().saveIfNeeded();
    }
    if (lang.exchange ((int) l) != (int) l)
        listeners().call ([] (Listener& x) { x.languageChanged(); });
}

void reload()
{
    settings().reload();
    const auto l = fromSettingsOrSystem();
    if (lang.exchange ((int) l) != (int) l)
        listeners().call ([] (Listener& x) { x.languageChanged(); });
}

void registerTable (const Entry* e, int n)
{
    auto& t = tables();
    for (int i = 0; i < n; ++i)
    {
        if (e[i].en != nullptr && *e[i].en != 0) t.byEn.emplace (e[i].en, &e[i]);
        if (e[i].pt != nullptr && *e[i].pt != 0) t.byPt.emplace (e[i].pt, &e[i]);
    }
}

static const Entry* find (const char* pt, const char* en)
{
    auto& t = tables();
    if (en != nullptr) if (auto it = t.byEn.find (en); it != t.byEn.end()) return it->second;
    if (pt != nullptr) if (auto it = t.byPt.find (pt); it != t.byPt.end()) return it->second;
    return nullptr;
}

juce::String tr (const char* pt, const char* en)
{
    switch (current())
    {
        case Language::PT: return juce::String::fromUTF8 (pt);
        case Language::ZH: if (auto* e = find (pt, en); e != nullptr && e->zh != nullptr && *e->zh != 0) return juce::String::fromUTF8 (e->zh);
                           return juce::String::fromUTF8 (en);
        default:           return juce::String::fromUTF8 (en);
    }
}

juce::String tr (const char* pt, const char* en, const char* zh)
{
    const auto l = current();
    return juce::String::fromUTF8 (l == Language::PT ? pt : (l == Language::ZH && zh != nullptr && *zh != 0 ? zh : en));
}

juce::String trPt (const char* pt)
{
    const auto l = current();
    if (l == Language::PT) return juce::String::fromUTF8 (pt);
    if (auto* e = find (pt, nullptr))
    {
        if (l == Language::ZH && e->zh != nullptr && *e->zh != 0) return juce::String::fromUTF8 (e->zh);
        if (e->en != nullptr && *e->en != 0) return juce::String::fromUTF8 (e->en);
    }
    return juce::String::fromUTF8 (pt);
}

juce::String trString (const juce::String& pt, const juce::String& en)
{
    const auto l = current();
    if (l == Language::PT) return pt;
    if (l == Language::ZH) if (auto* e = find (pt.toRawUTF8(), en.toRawUTF8()); e != nullptr && e->zh != nullptr && *e->zh != 0) return juce::String::fromUTF8 (e->zh);
    return en;
}

juce::String upper (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes (s.getNumBytesAsUTF8() + 4);
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        auto c = p.getAndAdvance();
        if (c >= 'a' && c <= 'z')                         c = (juce::juce_wchar) (c - 32);
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7)     c = (juce::juce_wchar) (c - 0x20);
        else if (c > 0x7F && c < 0x2E80)                  c = juce::CharacterFunctions::toUpperCase (c);
        out += juce::String::charToString (c);
    }
    return out;
}

void addListener (Listener* l)    { listeners().add (l); }
void removeListener (Listener* l) { listeners().remove (l); }

juce::String settingValue (const juce::String& key, const juce::String& def) { return settings().getValue (key, def); }
void setSettingValue (const juce::String& key, const juce::String& value)    { settings().setValue (key, value); settings().saveIfNeeded(); }
} // namespace piradex::i18n
