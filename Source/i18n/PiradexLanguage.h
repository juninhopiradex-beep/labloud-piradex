// =============================================================================
//  Piradex — idioma da interface (PT / EN / ZH), modulo partilhado por todos os
//  plugins Piradex (kit 1.0.2).
//
//  - Textos lado a lado no codigo:  tr ("Guardar", "Save")
//    O chines (mandarim simplificado) vem de uma tabela por plugin (ZhTable.inc),
//    procurada pelo texto em ingles (ou pelo portugues); sem traducao -> ingles.
//  - Textos que so existem em portugues:  trPt ("Guardar")  (EN e ZH pela tabela)
//  - A escolha fica num ficheiro comum a todos os plugins Piradex
//    (macOS: ~/Library/Application Support/Piradex/Piradex.settings,
//     Windows: %APPDATA%\Piradex\Piradex.settings), chave "language": pt / en / zh.
//  - Primeira utilizacao: segue o idioma do sistema (pt* -> PT, zh* -> ZH, resto -> EN).
//  - Mudar o idioma avisa todas as janelas abertas (troca imediata).
//  - Os nomes dos parametros vistos pelo DAW (automacao) NAO mudam.
// =============================================================================
#pragma once

#include <juce_core/juce_core.h>

namespace piradex::i18n
{
enum class Language { PT = 0, EN = 1, ZH = 2 };

Language current() noexcept;
void     set (Language, bool persist = true);   // chamar na thread da mensagem
void     reload();                               // reler o ficheiro (outro plugin pode ter mudado)
bool     isPortuguese() noexcept;
bool     isChinese() noexcept;
juce::String code();                             // "pt" / "en" / "zh"
juce::String languageName (Language);            // "Português" / "English" / "中文"

// Tabela de traducoes de um plugin: { pt, en, zh } (UTF-8). Registar uma vez (antes de criar a interface).
struct Entry { const char* pt; const char* en; const char* zh; };
void registerTable (const Entry* entries, int count);
void registerPluginTable();                       // definida no ZhTable.cpp de cada plugin

// Texto conforme o idioma actual (UTF-8)
juce::String tr (const char* pt, const char* en);
juce::String tr (const char* pt, const char* en, const char* zh);
juce::String trPt (const char* pt);              // so ha portugues no codigo: EN/ZH pela tabela
juce::String trString (const juce::String& pt, const juce::String& en);

// Maiusculas que tratam sempre os acentos portugueses (a->A, c->C...), independentemente do "locale"
juce::String upper (const juce::String&);

struct Listener
{
    virtual ~Listener() = default;
    virtual void languageChanged() = 0;
};
void addListener (Listener*);
void removeListener (Listener*);

// Definicoes partilhadas Piradex (o mesmo ficheiro do idioma), para o kit (contadores do pop-up, etc.)
juce::String settingValue (const juce::String& key, const juce::String& def = {});
void         setSettingValue (const juce::String& key, const juce::String& value);
} // namespace piradex::i18n
