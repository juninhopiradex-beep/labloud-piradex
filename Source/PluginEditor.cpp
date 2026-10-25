#include "PluginEditor.h"

using namespace lxui;

namespace
{
    juce::Font fontOf (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    juce::String dbText (float v) { return v <= -99.f ? juce::String ("-inf") : juce::String (v, 1); }
}

// ------------------------------------------------------------------------------------------ Look
Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (kText));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1018));
    setColour (juce::TextEditor::textColourId, juce::Colour (kText));
    setColour (juce::TextEditor::outlineColourId, juce::Colour (kLine));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1f2e));
    setColour (juce::TextButton::textColourOffId, juce::Colour (kText));
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float th = 5.f, rr = r - th * 0.5f;
    g.setColour (juce::Colour (0xff0d1018)); g.fillEllipse (c.x - r + th, c.y - r + th, 2 * (r - th), 2 * (r - th));
    juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, a1, true);
    g.setColour (juce::Colour (kLine)); g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    if (s.isEnabled())
    {
        juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, ang, true);
        g.setGradientFill (juce::ColourGradient (pal.a1, c.x - r, c.y, pal.a2, c.x + r, c.y, false));
        g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path tick; tick.addRoundedRectangle (-1.5f, -(r - th - 2.f), 3.f, r * 0.42f, 1.5f);
    g.setColour (juce::Colour (kText)); g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? pal.a1.withAlpha (0.16f) : juce::Colour (0xff141824)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? pal.a1 : (hl ? juce::Colour (kDim) : juce::Colour (kLine))); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
    const float dot = 8.f;
    g.setColour (on ? pal.a1 : juce::Colour (kDim));
    g.fillEllipse (r.getX() + 12.f, r.getCentreY() - dot * 0.5f, dot, dot);
    g.setFont (fontOf (12.f, true)); g.setColour (on ? juce::Colour (kText) : juce::Colour (kDim));
    g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (26.f).toNearestInt(), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------------ Knob
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& t, const juce::String& suffix, int decimals) : title (t)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setTextValueSuffix (suffix); slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setDoubleClickReturnValue (true, apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
}
void Knob::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (11.f, true));
    g.drawText (title.toUpperCase(), 0, 0, getWidth(), 16, juce::Justification::centred);
}
void Knob::resized() { slider.setBounds (0, 16, getWidth(), getHeight() - 16); }

// ------------------------------------------------------------------------------------------ Segmented
Segmented::Segmented (juce::AudioProcessorValueTreeState& apvts, const char* id, juce::StringArray l, Look& lk)
    : labels (std::move (l)), look (lk)
{
    param = apvts.getParameter (id);
    attach = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { selected = juce::roundToInt (v); repaint(); });
    attach->sendInitialUpdate();
}
void Segmented::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (juce::Colour (0xff0d1018)); g.fillRoundedRectangle (r, 9.f);
    g.setColour (juce::Colour (kLine)); g.drawRoundedRectangle (r, 9.f, 1.f);
    const float w = r.getWidth() / (float) labels.size();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.f);
        const bool on = i == selected;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (look.pal.a1.withAlpha (0.9f), cell.getX(), cell.getY(), look.pal.a2.withAlpha (0.9f), cell.getRight(), cell.getBottom(), false));
            g.fillRoundedRectangle (cell, 7.f);
        }
        g.setColour (on ? juce::Colour (0xff06070b) : juce::Colour (kDim)); g.setFont (fontOf (12.f, true));
        g.drawText (labels[i].toUpperCase(), cell.toNearestInt(), juce::Justification::centred);
    }
}
void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    attach->setValueAsCompleteGesture ((float) i);
}

// ------------------------------------------------------------------------------------------ Display
void Display::tick()
{
    auto& m = proc.meters;
    hist[(size_t) pos] = { m.inPeakDb.load(), m.outPeakDb.load(), m.grDb.load() };
    pos = (pos + 1) % kN; repaint();
}
void Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (kPanel)); g.fillRoundedRectangle (r, 14.f);
    g.setColour (juce::Colour (kLine)); g.drawRoundedRectangle (r.reduced (0.5f), 14.f, 1.f);
    auto area = r.reduced (14.f, 12.f);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f, floorDb = -36.f;
    auto yOf = [&] (float db) { return half * juce::jlimit (0.f, 1.f, (db - floorDb) / -floorDb); };
    // grelha
    g.setFont (fontOf (10.f)); 
    for (float db : { -6.f, -12.f, -24.f })
    {
        g.setColour (juce::Colour (kLine).withAlpha (0.7f));
        g.drawHorizontalLine ((int) (mid - yOf (db)), area.getX(), area.getRight()); g.drawHorizontalLine ((int) (mid + yOf (db)), area.getX(), area.getRight());
        g.setColour (juce::Colour (kDim)); g.drawText (juce::String ((int) db), (int) area.getX(), (int) (mid - yOf (db)) - 12, 30, 12, juce::Justification::left);
    }
    const float step = area.getWidth() / (float) (kN - 1);
    // redução de ganho (de cima para baixo)
    juce::Path gr; gr.startNewSubPath (area.getX(), area.getY());
    for (int i = 0; i < kN; ++i)
    {
        const auto& p = hist[(size_t) ((pos + i) % kN)];
        gr.lineTo (area.getX() + step * (float) i, area.getY() + juce::jlimit (0.f, 1.f, -p.gr / 18.f) * area.getHeight() * 0.5f);
    }
    gr.lineTo (area.getRight(), area.getY()); gr.closeSubPath();
    g.setColour (look.pal.a2.withAlpha (0.35f)); g.fillPath (gr);
    // entrada e saída (espelhadas)
    for (int pass = 0; pass < 2; ++pass)
    {
        g.setColour (pass == 0 ? juce::Colour (0xff4a5168).withAlpha (0.8f) : look.pal.a1.withAlpha (0.9f));
        for (int i = 0; i < kN; ++i)
        {
            const auto& p = hist[(size_t) ((pos + i) % kN)];
            const float h = yOf (pass == 0 ? p.in : p.out), x = area.getX() + step * (float) i;
            g.fillRect (x, mid - h, std::max (1.5f, step), 2.f * h);
        }
    }
    // linha do teto
    const float ceilDb = *proc.apvts.getRawParameterValue (ids::ceiling);
    g.setColour (juce::Colour (kText).withAlpha (0.85f));
    const float dash[] = { 5.f, 4.f };
    for (int s = -1; s <= 1; s += 2)
        g.drawDashedLine (juce::Line<float> (area.getX(), mid + (float) s * yOf (ceilDb), area.getRight(), mid + (float) s * yOf (ceilDb)), dash, 2, 1.2f);
    g.setColour (juce::Colour (kText)); g.setFont (fontOf (11.f, true));
    g.drawText ("TETO " + juce::String (ceilDb, 1) + " dB", (int) area.getRight() - 120, (int) area.getY() - 4, 120, 14, juce::Justification::right);
}

// ------------------------------------------------------------------------------------------ Login
Login::Login (std::function<void()> onSuccess) : onOk (std::move (onSuccess))
{
    pw.setPasswordCharacter ((juce::juce_wchar) 0x2022); pw.setTextToShowWhenEmpty ("Senha de acesso", juce::Colour (kDim));
    pw.setJustification (juce::Justification::centred); pw.setFont (fontOf (16.f)); pw.onReturnKey = [this] { submit(); };
    remember.setToggleState (true, juce::dontSendNotification);
    enter.onClick = [this] { submit(); };
    msg.setJustificationType (juce::Justification::centred); msg.setFont (fontOf (12.f)); msg.setColour (juce::Label::textColourId, juce::Colour (0xffff6b7a));
    addAndMakeVisible (pw); addAndMakeVisible (remember); addAndMakeVisible (enter); addAndMakeVisible (msg);
}
void Login::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (380, 300).reduced (30, 0);
    pw.setBounds (r.getX(), r.getY() + 110, r.getWidth(), 40);
    remember.setBounds (r.getX(), r.getY() + 160, r.getWidth(), 24);
    enter.setBounds (r.getX(), r.getY() + 194, r.getWidth(), 40);
    msg.setBounds (r.getX() - 20, r.getY() + 240, r.getWidth() + 40, 20);
}
void Login::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBg).withAlpha (0.94f));
    auto card = getLocalBounds().withSizeKeepingCentre (380, 300).toFloat();
    g.setColour (juce::Colour (kPanel)); g.fillRoundedRectangle (card, 18.f);
    g.setColour (juce::Colour (kLine)); g.drawRoundedRectangle (card.reduced (0.5f), 18.f, 1.f);
    const auto p = paletteFor (0);
    g.setGradientFill (juce::ColourGradient (p.a1, card.getX(), card.getY(), p.a2, card.getRight(), card.getY(), false));
    g.setFont (fontOf (34.f, true)); g.drawText ("LABLOUD", card.withTrimmedTop (28.f).withHeight (46.f).toNearestInt(), juce::Justification::centred);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (12.f));
    g.drawText ("by Piradex  ·  Limiter + Clipper", card.withTrimmedTop (76.f).withHeight (18.f).toNearestInt(), juce::Justification::centred);
}
void Login::submit()
{
    if (busy) return;
    const juce::String pass = pw.getText();
    if (pass.trim().isEmpty()) { msg.setText ("Escreva a senha.", juce::dontSendNotification); return; }
    busy = true; enter.setEnabled (false); msg.setColour (juce::Label::textColourId, juce::Colour (kDim)); msg.setText ("A verificar...", juce::dontSendNotification);
    const bool rem = remember.getToggleState();
    juce::Component::SafePointer<Login> safe (this);
    juce::Thread::launch ([safe, pass, rem]
    {
        const auto m = AccessManager::matchPassword (pass);      // PBKDF2: fora da thread de mensagens
        juce::MessageManager::callAsync ([safe, m, rem]
        {
            if (safe == nullptr) return;
            const auto res = AccessManager::get().applyLogin (m, rem);
            safe->busy = false; safe->enter.setEnabled (true);
            safe->msg.setColour (juce::Label::textColourId, juce::Colour (0xffff6b7a));
            if (res == AccessManager::LoginResult::Ok) { safe->pw.clear(); safe->msg.setText ({}, juce::dontSendNotification); if (safe->onOk) safe->onOk(); }
            else if (res == AccessManager::LoginResult::Expired) safe->msg.setText (juce::CharPointer_UTF8 ("Esta senha beta j\xC3\xA1 expirou."), juce::dontSendNotification);
            else safe->msg.setText ("Senha incorreta.", juce::dontSendNotification);
        });
    });
}

// ------------------------------------------------------------------------------------------ Editor
LoudnexEditor::LoudnexEditor (LoudnexProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (p, look),
      modeSeg (p.apvts, ids::mode, { "Limiter", "Clipper", "Hybrid" }, look),
      characterSeg (p.apvts, ids::character, { "Clean", "Punch", "Loud" }, look),
      shapeSeg (p.apvts, ids::shape, { "Soft", "Cubic", "Hard", "Custom" }, look),
      osSeg (p.apvts, ids::os, { "1x", "4x", "16x", "32x" }, look),
      gain (p.apvts, ids::gain, "Ganho", " dB", 1), ceiling (p.apvts, ids::ceiling, "Teto", " dB", 1),
      release (p.apvts, ids::release, "Release", " ms", 1), lookahead (p.apvts, ids::lookahead, "Lookahead", " ms", 2),
      assist (p.apvts, ids::assist, "Clip Assist", " %", 0), link (p.apvts, ids::link, "Stereo Link", " %", 0),
      mixL (p.apvts, ids::mix, "Mix", " %", 0), kneeC (p.apvts, ids::knee, "Joelho", " %", 0), mixC (p.apvts, ids::mix, "Mix", " %", 0),
      login ([this] { timerCallback(); })
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    for (auto* c : std::initializer_list<juce::Component*> { &display, &modeSeg, &characterSeg, &shapeSeg, &osSeg, &gain, &ceiling, &release, &lookahead,
                                                              &assist, &link, &mixL, &kneeC, &mixC, &tpBtn, &bassBtn, &logoutBtn })
        content.addAndMakeVisible (c);
    content.addAndMakeVisible (login);
    tpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::tp, tpBtn);
    bassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::bass, bassBtn);
    logoutBtn.onClick = [this] { AccessManager::get().logout(); timerCallback(); };

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1000.0 / 600.0);
    setResizeLimits (700, 420, 1800, 1080);
    setSize (1000, 600);
    startTimerHz (30);
    timerCallback();
}

LoudnexEditor::~LoudnexEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LoudnexEditor::resized()
{
    const float s = (float) getWidth() / 1000.f;
    content.setBounds (0, 0, 1000, 600);
    content.setTransform (juce::AffineTransform::scale (s));
    layoutControls();
}

void LoudnexEditor::layoutControls()
{
    display.setBounds (24, 84, 640, 270);
    modeSeg.setBounds (390, 20, 220, 36);
    logoutBtn.setBounds (904, 22, 72, 30);
    login.setBounds (0, 0, 1000, 600);

    const int y = 372, h = 150, w = 108;
    const bool lim = shownMode != 1;
    // zona de botões (esquerda): 7 no limiter, 4 no clipper
    gain.setBounds (24, y, w, h); ceiling.setBounds (24 + w + 6, y, w, h);
    release.setBounds (24 + 2 * (w + 6), y, w, h); lookahead.setBounds (24 + 3 * (w + 6), y, w, h);
    assist.setBounds (24 + 4 * (w + 6), y, w, h); link.setBounds (24 + 5 * (w + 6), y, w, h); mixL.setBounds (24 + 6 * (w + 6), y, w, h);
    kneeC.setBounds (24 + 2 * (w + 6), y, w, h); mixC.setBounds (24 + 3 * (w + 6), y, w, h);
    characterSeg.setBounds (24, 536, 200, 34); shapeSeg.setBounds (24, 536, 200, 34); osSeg.setBounds (236, 536, 150, 34);
    tpBtn.setBounds (lim ? 236 : 398, 536, 130, 34); bassBtn.setBounds (378, 536, 140, 34);
}

void LoudnexEditor::applyMode (int mode)
{
    shownMode = mode;
    look.setPalette (paletteFor (mode));
    const bool lim = mode == 0;
    for (auto* c : std::initializer_list<juce::Component*> { &release, &lookahead, &assist, &link, &mixL, &characterSeg, &bassBtn })
        c->setVisible (lim);
    for (auto* c : std::initializer_list<juce::Component*> { &kneeC, &mixC, &shapeSeg, &osSeg })
        c->setVisible (! lim);
    layoutControls();
    content.repaint();
}

void LoudnexEditor::timerCallback()
{
    const bool unlocked = AccessManager::get().isUnlocked();
    if (unlocked != wasUnlocked || shownMode < 0) { wasUnlocked = unlocked; login.setVisible (! unlocked); logoutBtn.setVisible (unlocked); content.repaint(); }
    const int mode = juce::roundToInt (proc.apvts.getRawParameterValue (ids::mode)->load());
    if (mode != shownMode) applyMode (mode);
    if (! unlocked) return;
    display.tick();
    auto& m = proc.meters;
    inDisp = std::max (m.inPeakDb.load(), inDisp - 1.2f); outDisp = std::max (m.outPeakDb.load(), outDisp - 1.2f);
    repaint (content.getBounds().withLeft (getWidth() * 66 / 100));
}

void LoudnexEditor::paint (juce::Graphics& g)
{
    // o fundo e os medidores são desenhados na escala de 1000x600
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / 1000.f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (kBg), 0.f, 0.f, juce::Colour (0xff0f1322), 1000.f, 600.f, false)); g.fillAll();
    const auto pal = paletteFor (std::max (0, shownMode));
    g.setGradientFill (juce::ColourGradient (pal.a1, 24.f, 20.f, pal.a2, 220.f, 20.f, false));
    g.setFont (fontOf (28.f, true)); g.drawText ("LABLOUD", 24, 14, 200, 40, juce::Justification::centredLeft);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (11.f)); g.drawText ("by Piradex", 24, 50, 120, 14, juce::Justification::centredLeft);

    const auto& m = proc.meters;
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (11.f, true));
    g.drawText (AccessManager::get().roleText(), 700, 28, 196, 18, juce::Justification::centredRight);

    // painel direito: medidores
    juce::Rectangle<float> panel (684.f, 84.f, 292.f, 438.f);
    g.setColour (juce::Colour (kPanel)); g.fillRoundedRectangle (panel, 14.f);
    g.setColour (juce::Colour (kLine)); g.drawRoundedRectangle (panel.reduced (0.5f), 14.f, 1.f);
    auto bar = [&] (float x, float db, const char* name, juce::Colour c)
    {
        const float top = 112.f, hgt = 290.f, v = juce::jlimit (0.f, 1.f, (db + 36.f) / 36.f);
        g.setColour (juce::Colour (0xff0d1018)); g.fillRoundedRectangle (x, top, 26.f, hgt, 6.f);
        g.setColour (c); g.fillRoundedRectangle (x, top + hgt * (1.f - v), 26.f, hgt * v, 6.f);
        g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText (name, (int) x - 10, (int) (top + hgt + 4.f), 46, 12, juce::Justification::centred);
        g.setColour (juce::Colour (kText)); g.drawText (dbText (db), (int) x - 14, (int) (top - 18.f), 54, 14, juce::Justification::centred);
    };
    bar (712.f, inDisp, "ENTRADA", juce::Colour (0xff4a5168));
    bar (766.f, outDisp, "SAIDA", pal.a1);
    const float gr = m.grDb.load();
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText ("REDUCAO", 820, 94, 140, 12, juce::Justification::centred);
    g.setColour (pal.a2); g.setFont (fontOf (34.f, true)); g.drawText (juce::String (gr, 1), 820, 110, 140, 42, juce::Justification::centred);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (11.f)); g.drawText ("dB", 820, 150, 140, 14, juce::Justification::centred);

    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText ("LUFS MOMENTANEO", 820, 200, 140, 12, juce::Justification::centred);
    g.setColour (juce::Colour (kText)); g.setFont (fontOf (24.f, true)); g.drawText (dbText (m.momentary.load()), 820, 214, 140, 30, juce::Justification::centred);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText ("LUFS CURTO PRAZO", 820, 262, 140, 12, juce::Justification::centred);
    g.setColour (juce::Colour (kText)); g.setFont (fontOf (24.f, true)); g.drawText (dbText (m.shortTerm.load()), 820, 276, 140, 30, juce::Justification::centred);

    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText ("LATENCIA", 820, 330, 140, 12, juce::Justification::centred);
    const double lat = proc.reportedLatency() * 1000.0 / std::max (1.0, proc.currentSampleRate());
    g.setColour (juce::Colour (kText)); g.setFont (fontOf (16.f, true)); g.drawText (juce::String (lat, 1) + " ms", 820, 344, 140, 22, juce::Justification::centred);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f, true)); g.drawText ("OVERS EVITADOS", 820, 384, 140, 12, juce::Justification::centred);
    const int clamps = m.clamps.load();
    g.setColour (clamps == 0 ? pal.a1 : juce::Colour (0xffff6b7a)); g.setFont (fontOf (16.f, true));
    g.drawText (clamps == 0 ? "0 grampos" : juce::String (clamps) + " grampos", 820, 398, 140, 22, juce::Justification::centred);

    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (10.f));
    g.drawText (juce::String ("v") + LX_VERSION_STRING + "  ·  Piradex", 684, 540, 292, 14, juce::Justification::centredRight);
}
