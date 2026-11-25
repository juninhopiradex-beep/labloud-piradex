// =============================================================================
//  Piradex — kit de interface partilhado (1.0.2)
//
//  LoginScreen  : o mesmo ecra de entrada em todos os plugins Piradex (identidade
//                 unica). O plugin liga onSubmit (senha/chave + "lembrar") e
//                 chama setStatus() com o resultado; esconde-o quando ficar desbloqueado.
//  Welcome      : pop-up "Tutorial e novidades" nas 3 primeiras aberturas de cada
//                 versao (contador no ficheiro Piradex partilhado). Fecha com o botao,
//                 com Esc ou com um clique fora do cartao.
// =============================================================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PiradexLanguageUI.h"

namespace piradex::kit
{
using piradex::i18n::tr;

namespace colours
{
    inline const juce::Colour bg       { 0xf20b0d11 };
    inline const juce::Colour card     { 0xff15181e };
    inline const juce::Colour cardEdge { 0xff2a2f38 };
    inline const juce::Colour field    { 0xff0f1115 };
    inline const juce::Colour text     { 0xffe9ecf1 };
    inline const juce::Colour dim      { 0xff8d94a1 };
    inline const juce::Colour faint    { 0xff5c6370 };
    inline const juce::Colour error    { 0xffff6b6b };
    inline const juce::Colour ok       { 0xff5be3a3 };
}

inline juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

// cabecalho comum: "PIRADEX" espacado + nome do plugin
inline void drawBrand (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& pluginName, juce::Colour accent)
{
    g.setColour (accent);
    g.setFont (font (11.f, true));
    juce::String spaced; for (auto c : juce::String ("PIRADEX")) { spaced << juce::String::charToString (c) << " "; }
    g.drawText (spaced.trimEnd(), r.removeFromTop (16), juce::Justification::centred);
    g.setColour (colours::text);
    g.setFont (font (28.f, true));
    g.drawText (pluginName, r.removeFromTop (36), juce::Justification::centred);
}

// botao "olho" para mostrar / esconder a senha
class EyeButton : public juce::Button
{
public:
    juce::Colour on { 0xff40e0c0 }, off { colours::dim };
    EyeButton() : juce::Button ("eye") { setClickingTogglesState (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto e = getLocalBounds().toFloat().reduced (9.f, 13.f);
        g.setColour (getToggleState() ? on : (over ? colours::text : off));
        juce::Path p; p.startNewSubPath (e.getX(), e.getCentreY());
        p.quadraticTo (e.getCentreX(), e.getY() - e.getHeight() * 0.35f, e.getRight(), e.getCentreY());
        p.quadraticTo (e.getCentreX(), e.getBottom() + e.getHeight() * 0.35f, e.getX(), e.getCentreY());
        g.strokePath (p, juce::PathStrokeType (1.5f));
        const float d = e.getHeight() * 0.62f; g.fillEllipse (e.getCentreX() - d * 0.5f, e.getCentreY() - d * 0.5f, d, d);
    }
};

// ---------------------------------------------------------------------------------------------
// Aparencia propria dos componentes do kit: o LookAndFeel de cada plugin nao muda os botoes do login nem do pop-up
class KitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bg; if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f); else if (down) c = c.darker (0.15f); else if (over) c = c.brighter (0.08f);
        g.setColour (c); g.fillRoundedRectangle (r, 8.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour (b.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.6f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
    }
    static KitLookAndFeel& get() { static KitLookAndFeel l; return l; }
};

class LoginScreen : public juce::Component, private piradex::i18n::Listener
{
public:
    std::function<void (const juce::String& secret, bool remember)> onSubmit;

    LoginScreen (juce::String pluginName_, juce::Colour accent_, juce::String version_)
        : pluginName (std::move (pluginName_)), accent (accent_), version (std::move (version_))
    {
        piradex::i18n::addListener (this);
        setLookAndFeel (&KitLookAndFeel::get());
        addAndMakeVisible (pw);
        pw.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        pw.setFont (font (15.f));
        pw.setJustification (juce::Justification::centred);
        pw.setColour (juce::TextEditor::backgroundColourId, colours::field);
        pw.setColour (juce::TextEditor::outlineColourId, colours::cardEdge);
        pw.setColour (juce::TextEditor::focusedOutlineColourId, accent);
        pw.setColour (juce::TextEditor::textColourId, colours::text);
        pw.setColour (juce::CaretComponent::caretColourId, accent);
        pw.onReturnKey = [this] { submit(); };
        addAndMakeVisible (remember);
        remember.setToggleState (true, juce::dontSendNotification);
        remember.setColour (juce::ToggleButton::textColourId, colours::dim);
        remember.setColour (juce::ToggleButton::tickColourId, accent);
        remember.setColour (juce::ToggleButton::tickDisabledColourId, colours::faint);
        addAndMakeVisible (enter);
        enter.setColour (juce::TextButton::buttonColourId, accent);
        enter.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff0b0d11));
        enter.onClick = [this] { submit(); };
        addAndMakeVisible (langSwitch);
        langSwitch.accent = accent;
        showHide.on = accent;
        addAndMakeVisible (showHide);
        showHide.onClick = [this] { pw.setPasswordCharacter (showHide.getToggleState() ? 0 : (juce::juce_wchar) 0x2022); repaint(); };
        refreshTexts();
        setWantsKeyboardFocus (false);
    }
    ~LoginScreen() override { setLookAndFeel (nullptr); piradex::i18n::removeListener (this); }

    void setStatus (const juce::String& message, bool isError) { status = message; statusError = isError; repaint(); }
    void setBusy (bool b) { busy = b; enter.setEnabled (! b); pw.setEnabled (! b); repaint(); }
    void focusField() { pw.grabKeyboardFocus(); }
    juce::String getSecret() const { return pw.getText().trim(); }
    void clearField() { pw.clear(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bg);
        auto c = cardBounds();
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 28, {}).drawForRectangle (g, c);
        g.setColour (colours::card); g.fillRoundedRectangle (c.toFloat(), 14.f);
        g.setColour (colours::cardEdge); g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.f, 1.f);
        g.setColour (accent); g.fillRoundedRectangle (c.toFloat().removeFromTop (3.f).reduced (24.f, 0.f), 1.5f);
        auto r = c.reduced (28, 22);
        drawBrand (g, r.removeFromTop (56), pluginName, accent);
        r.removeFromTop (4);
        g.setColour (colours::dim); g.setFont (font (13.f));
        g.drawFittedText (tr ("Introduza a senha ou a chave de licen\xc3\xa7" "a", "Enter your password or licence key", "\xe8\xaf\xb7\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81\xe6\x88\x96\xe8\xae\xb8\xe5\x8f\xaf\xe8\xaf\x81\xe5\xaf\x86\xe9\x92\xa5"), r.removeFromTop (20), juce::Justification::centred, 1);
        // estado
        auto st = juce::Rectangle<int> (c.getX() + 24, enter.getBottom() + 10, c.getWidth() - 48, 34);
        g.setColour (statusError ? colours::error : colours::ok); g.setFont (font (12.5f));
        g.drawFittedText (busy ? tr ("A validar\xe2\x80\xa6", "Checking\xe2\x80\xa6", "\xe6\xad\xa3\xe5\x9c\xa8\xe9\xaa\x8c\xe8\xaf\x81\xe2\x80\xa6") : status, st, juce::Justification::centred, 2);
        // rodape
        auto f = juce::Rectangle<int> (c.getX() + 16, c.getBottom() - 46, c.getWidth() - 32, 40);
        g.setColour (colours::faint); g.setFont (font (11.f));
        g.drawFittedText (tr ("Suporte: juninhopiradex@hotmail.com  \xc2\xb7  Loja: beatfreakstudio.com/loja", "Support: juninhopiradex@hotmail.com  \xc2\xb7  Store: beatfreakstudio.com/loja", "\xe6\x94\xaf\xe6\x8c\x81\xef\xbc\x9ajuninhopiradex@hotmail.com  \xc2\xb7  \xe5\x95\x86\xe5\xba\x97\xef\xbc\x9a" "beatfreakstudio.com/loja"),
                          f.removeFromTop (18), juce::Justification::centred, 1);
        g.drawFittedText (tr ("Criador e Desenvolvedor Ricardo Amorim / Juninho Piradex", "Creator and Developer Ricardo Amorim / Juninho Piradex", "\xe5\x88\x9b\xe4\xbd\x9c\xe8\x80\x85\xe4\xb8\x8e\xe5\xbc\x80\xe5\x8f\x91\xe8\x80\x85 Ricardo Amorim / Juninho Piradex") + "  \xc2\xb7  v" + version,
                          f, juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto c = cardBounds();
        auto r = c.reduced (40, 0);
        int y = c.getY() + 22 + 56 + 4 + 20 + 16;
        pw.setBounds (r.getX(), y, r.getWidth(), 40);
        showHide.setBounds (pw.getRight() - 40, y, 40, 40);
        y += 50;
        remember.setBounds (r.getX(), y, r.getWidth() - 150, 26);
        langSwitch.setBounds (r.getRight() - 140, y, 140, 26);
        y += 36;
        enter.setBounds (r.getX(), y, r.getWidth(), 40);
    }

    void mouseDown (const juce::MouseEvent&) override { pw.grabKeyboardFocus(); }

private:
    juce::String pluginName; juce::Colour accent; juce::String version;
    juce::TextEditor pw;
    juce::ToggleButton remember;
    juce::TextButton enter;
    EyeButton showHide;
    piradex::i18n::LanguageSwitch langSwitch;
    juce::String status; bool statusError = false, busy = false;

    juce::Rectangle<int> cardBounds() const
    {
        const int w = juce::jmin (440, getWidth() - 24), h = juce::jmin (360, getHeight() - 24);
        return getLocalBounds().withSizeKeepingCentre (w, h);
    }
    void submit()
    {
        const auto s = getSecret();
        if (s.isEmpty()) { setStatus (tr ("Escreva a senha ou a chave.", "Type the password or key.", "\xe8\xaf\xb7\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81\xe6\x88\x96\xe5\xaf\x86\xe9\x92\xa5\xe3\x80\x82"), true); return; }
        if (onSubmit) onSubmit (s, remember.getToggleState());
    }
    void refreshTexts()
    {
        pw.setTextToShowWhenEmpty (tr ("Senha ou chave de licen\xc3\xa7" "a", "Password or licence key", "\xe5\xaf\x86\xe7\xa0\x81\xe6\x88\x96\xe8\xae\xb8\xe5\x8f\xaf\xe8\xaf\x81\xe5\xaf\x86\xe9\x92\xa5"), colours::faint);
        remember.setButtonText (tr ("Lembrar neste computador", "Remember on this computer", "\xe5\x9c\xa8\xe6\xad\xa4\xe7\x94\xb5\xe8\x84\x91\xe4\xb8\x8a\xe8\xae\xb0\xe4\xbd\x8f"));
        enter.setButtonText (tr ("Entrar", "Sign in", "\xe7\x99\xbb\xe5\xbd\x95"));
        showHide.setTooltip (tr ("Mostrar a senha", "Show password", "\xe6\x98\xbe\xe7\xa4\xba\xe5\xaf\x86\xe7\xa0\x81"));
        repaint();
    }
    void languageChanged() override { refreshTexts(); }
};


// ---------------------------------------------------------------------------------------------
// Roda dentada: menu com o idioma (PT / EN / 中文), "Tutorial e novidades" e o que o plugin acrescentar.
class GearButton : public juce::Button
{
public:
    enum { kWelcomeItem = 9201 };
    juce::Colour colour { colours::dim }, hoverColour { colours::text };
    std::function<void (juce::PopupMenu&)> addItems;    // itens proprios do plugin (ids < 9000)
    std::function<void (int)> handleItem;
    std::function<void()> showWelcome;                  // reabrir o pop-up

    GearButton() : juce::Button ("settings")
    {
        setTooltip (tr ("Defini\xc3\xa7\xc3\xb5" "es e idioma", "Settings and language", "\xe8\xae\xbe\xe7\xbd\xae\xe4\xb8\x8e\xe8\xaf\xad\xe8\xa8\x80"));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        onClick = [this] { openMenu(); };
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.f);
        const float s = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (s, s);
        const auto c = b.getCentre(); const float ro = s * 0.5f, ri = s * 0.36f, rh = s * 0.15f;
        juce::Path p;
        for (int i = 0; i < 16; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 16.f - juce::MathConstants<float>::halfPi;
            const float r = (i % 2 == 0) ? ro : ri;
            const float a2 = a + juce::MathConstants<float>::twoPi / 16.f;
            if (i == 0) p.startNewSubPath (c.getPointOnCircumference (r, a + juce::MathConstants<float>::halfPi));
            p.lineTo (c.getPointOnCircumference (r, a + juce::MathConstants<float>::halfPi));
            p.lineTo (c.getPointOnCircumference (r, a2 + juce::MathConstants<float>::halfPi));
        }
        p.closeSubPath();
        p.addEllipse (c.x - rh, c.y - rh, rh * 2.f, rh * 2.f);
        p.setUsingNonZeroWinding (false);
        g.setColour ((over || down) ? hoverColour : colour);
        g.fillPath (p);
    }
    void openMenu()
    {
        juce::PopupMenu m;
        piradex::i18n::addLanguageItems (m);
        if (showWelcome) m.addItem (kWelcomeItem, tr ("Tutorial e novidades", "Tutorial and what's new", "\xe6\x95\x99\xe7\xa8\x8b\xe4\xb8\x8e\xe6\x96\xb0\xe5\x8a\x9f\xe8\x83\xbd"));
        if (addItems) { m.addSeparator(); addItems (m); }
        juce::Component::SafePointer<GearButton> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (piradex::i18n::handleLanguageItem (r)) return;
            if (r == kWelcomeItem) { if (safe->showWelcome) safe->showWelcome(); return; }
            if (safe->handleItem) safe->handleItem (r);
        });
    }
};

// ---------------------------------------------------------------------------------------------
// Pop-up "Tutorial e novidades": 3 primeiras aberturas de cada versao.
struct WelcomeItem { const char* titlePt; const char* titleEn; const char* bodyPt; const char* bodyEn; };

class Welcome : public juce::Component, private piradex::i18n::Listener
{
public:
    // devolve true se deve aparecer nesta abertura (e conta-a). pluginId sem espacos, ex. "burnix".
    // Conta uma vez por sessao do DAW (os anfitrioes podem criar o editor varias vezes); depois de fechado nao volta nesta sessao.
    static bool shouldShow (const juce::String& pluginId, const juce::String& version, int maxTimes = 3)
    {
        auto& st = sessionState();
        if (st < 0)
        {
            const auto key = "welcome." + pluginId + "." + version;
            const int n = piradex::i18n::settingValue (key, "0").getIntValue();
            st = n < maxTimes ? 1 : 0;
            if (st == 1) piradex::i18n::setSettingValue (key, juce::String (n + 1));
        }
        return st == 1;
    }
    static int& sessionState() { static int s = -1; return s; }

    std::function<void()> onClose;

    Welcome (juce::String pluginName_, juce::Colour accent_, juce::String version_, std::vector<WelcomeItem> items_)
        : pluginName (std::move (pluginName_)), accent (accent_), version (std::move (version_)), items (std::move (items_))
    {
        piradex::i18n::addListener (this);
        setLookAndFeel (&KitLookAndFeel::get());
        addAndMakeVisible (close);
        close.setColour (juce::TextButton::buttonColourId, accent);
        close.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff0b0d11));
        close.onClick = [this] { dismiss(); };
        addAndMakeVisible (langSwitch); langSwitch.accent = accent;
        refreshTexts();
        setWantsKeyboardFocus (true);
    }
    ~Welcome() override { setLookAndFeel (nullptr); piradex::i18n::removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::bg);
        auto c = cardBounds();
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 28, {}).drawForRectangle (g, c);
        g.setColour (colours::card); g.fillRoundedRectangle (c.toFloat(), 14.f);
        g.setColour (colours::cardEdge); g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.f, 1.f);
        g.setColour (accent); g.fillRoundedRectangle (c.toFloat().removeFromTop (3.f).reduced (24.f, 0.f), 1.5f);
        auto r = c.reduced (30, 20);
        auto head = r.removeFromTop (30);
        g.setColour (colours::text); g.setFont (font (19.f, true));
        g.drawText (tr ("Tutorial e novidades", "Tutorial and what's new", "\xe6\x95\x99\xe7\xa8\x8b\xe4\xb8\x8e\xe6\x96\xb0\xe5\x8a\x9f\xe8\x83\xbd") + "  \xe2\x80\x94  " + pluginName + " " + version, head.withTrimmedRight (150), juce::Justification::centredLeft);
        r.removeFromTop (10);
        r.removeFromBottom (54);
        const bool zh = piradex::i18n::isChinese(), pt = piradex::i18n::isPortuguese();
        const int n = (int) items.size();
        const int rowH = kRowH;
        for (auto& it : items)
        {
            auto row = r.removeFromTop (rowH);
            auto left = row.removeFromLeft (juce::jmin (210, row.getWidth() / 3));
            g.setColour (accent); g.setFont (font (13.f, true));
            g.drawFittedText (pick (it.titlePt, it.titleEn, pt, zh), left.reduced (0, 2), juce::Justification::topLeft, 3);
            g.setColour (colours::text.withAlpha (0.86f)); g.setFont (font (13.f));
            g.drawFittedText (pick (it.bodyPt, it.bodyEn, pt, zh), row.reduced (6, 2), juce::Justification::topLeft, juce::jmax (1, rowH / 16));
        }
    }

    void resized() override
    {
        auto c = cardBounds();
        close.setBounds (c.getRight() - 30 - 220, c.getBottom() - 20 - 40, 220, 40);
        langSwitch.setBounds (c.getRight() - 30 - 140, c.getY() + 22, 140, 26);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! cardBounds().contains (e.getPosition())) dismiss(); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { dismiss(); return true; } return false; }

private:
    juce::String pluginName; juce::Colour accent; juce::String version;
    std::vector<WelcomeItem> items;
    static constexpr int kRowH = 58;
    juce::TextButton close;
    piradex::i18n::LanguageSwitch langSwitch;

    static juce::String pick (const char* pt, const char* en, bool isPt, bool isZh)
    {
        if (isPt) return juce::String::fromUTF8 (pt);
        return isZh ? piradex::i18n::tr (pt, en) : juce::String::fromUTF8 (en);
    }
    juce::Rectangle<int> cardBounds() const
    {
        const int want = 20 + 30 + 10 + (int) items.size() * kRowH + 74;
        const int w = juce::jmin (860, getWidth() - 40), h = juce::jmin (want, getHeight() - 40);
        return getLocalBounds().withSizeKeepingCentre (w, h);
    }
    void dismiss() { sessionState() = 0; setVisible (false); if (onClose) onClose(); }
    void refreshTexts() { close.setButtonText (tr ("Percebi \xe2\x80\x94 fechar", "Got it \xe2\x80\x94 close", "\xe7\x9f\xa5\xe9\x81\x93\xe4\xba\x86 \xe2\x80\x94 \xe5\x85\xb3\xe9\x97\xad")); repaint(); }
    void languageChanged() override { refreshTexts(); }
};
} // namespace piradex::kit
