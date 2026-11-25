// =============================================================================
//  Piradex — componentes de UI do idioma (PT / EN / 中文), partilhados pelos plugins.
//
//  LanguageSwitch : pilula "PT | EN | 中文" (troca imediata). Cores configuraveis.
//  Binder         : bind ([this] { botao.setButtonText (tr (..)); }) aplica ja e
//                   volta a aplicar a cada troca de idioma; onChange = repaint.
//  addLanguageItems / handleLanguageItem : submenu "Idioma" para a roda dentada.
// =============================================================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PiradexLanguage.h"

namespace piradex::i18n
{
class Binder : private Listener
{
public:
    Binder()  { addListener (this); }
    ~Binder() override { removeListener (this); }
    void bind (std::function<void()> f) { f(); fns.push_back (std::move (f)); }
    std::function<void()> onChange;
private:
    std::vector<std::function<void()>> fns;
    void languageChanged() override { for (auto& f : fns) f(); if (onChange) onChange(); }
};

// ---- roda dentada: submenu de idioma (ids 9101..9103)
enum { kLanguageMenuBase = 9100 };
inline juce::String languageMenuTitle() { return tr ("Idioma", "Language", "\xe8\xaf\xad\xe8\xa8\x80"); }
inline juce::PopupMenu languageMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < 3; ++i) m.addItem (kLanguageMenuBase + 1 + i, languageName ((Language) i), true, (int) current() == i);
    return m;
}
inline void addLanguageItems (juce::PopupMenu& parent) { parent.addSubMenu (languageMenuTitle(), languageMenu()); }
inline bool handleLanguageItem (int result)
{
    if (result > kLanguageMenuBase && result <= kLanguageMenuBase + 3) { set ((Language) (result - kLanguageMenuBase - 1)); return true; }
    return false;
}

class LanguageSwitch : public juce::Component, public juce::SettableTooltipClient, private Listener
{
public:
    juce::Colour accent { 0xff40e0c0 }, background { 0xff101216 }, outline { 0xff3a3f4a },
                 text { 0xffe6e9ef }, textDim { 0xff8a919e }, hoverFill { 0xff1c2027 };
    float fontHeight = 13.0f;
    bool bold = true;

    LanguageSwitch()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (juce::String::fromUTF8 ("Idioma da interface  \xc2\xb7  Interface language  \xc2\xb7  \xe7\x95\x8c\xe9\x9d\xa2\xe8\xaf\xad\xe8\xa8\x80"));
        addListener (this);
    }
    ~LanguageSwitch() override { removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = b.getHeight() * 0.5f;
        g.setColour (background);
        g.fillRoundedRectangle (b, rad);
        g.setColour (outline);
        g.drawRoundedRectangle (b, rad, 1.0f);
        const int sel = (int) current();
        for (int i = 0; i < 3; ++i)
        {
            auto r = part (i).reduced (2.0f);
            if (i == sel)
            {
                g.setColour (accent.withAlpha (0.18f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (accent.withAlpha (0.9f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
            }
            else if (i == hover)
            {
                g.setColour (hoverFill);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (i == sel ? text : textDim);
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (fontHeight, r.getHeight() * 0.6f), bold ? juce::Font::bold : juce::Font::plain)));
            g.drawText (i == 0 ? juce::String ("PT") : (i == 1 ? juce::String ("EN") : juce::String::fromUTF8 ("\xe4\xb8\xad\xe6\x96\x87")), r, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 3; ++i)
            if (part (i).contains (e.position)) set ((Language) i);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < 3; ++i) if (part (i).contains (e.position)) h = i;
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    int hover = -1;
    juce::Rectangle<float> part (int i) const
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float w = r.getWidth() / 3.0f;
        return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
    }
    void languageChanged() override { repaint(); }
};
} // namespace piradex::i18n
