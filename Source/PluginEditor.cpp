#include "PluginEditor.h"

using namespace lxui;

namespace
{
    juce::String dbText (float v) { return v <= -99.f ? juce::String ("-inf") : juce::String (v, 1); }
    float R (LoudnexProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
    const char* kOsNames[] = { "1x", "4x", "16x", "32x" };
    const char* kCharNames[] = { "Clean", "Warm", "Punch", "Aggressive" };

    void drawCard (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (cPanel()); g.fillRoundedRectangle (r, 14.f);
        g.setColour (cLine()); g.drawRoundedRectangle (r.reduced (0.5f), 14.f, 1.f);
    }
    void caption (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
    {
        juce::String u = t.toUpperCase();
        static const char* lo[] = { "\xC3\xA1","\xC3\xA0","\xC3\xA2","\xC3\xA3","\xC3\xA9","\xC3\xAA","\xC3\xAD","\xC3\xB3","\xC3\xB4","\xC3\xB5","\xC3\xBA","\xC3\xA7" };
        static const char* up[] = { "\xC3\x81","\xC3\x80","\xC3\x82","\xC3\x83","\xC3\x89","\xC3\x8A","\xC3\x8D","\xC3\x93","\xC3\x94","\xC3\x95","\xC3\x9A","\xC3\x87" };
        for (int k = 0; k < 12; ++k) u = u.replace (juce::String::fromUTF8 (lo[k]), juce::String::fromUTF8 (up[k]));
        g.setColour (cMu()); g.setFont (fontOf (11.f, true)); g.drawText (u, r, j);
    }
}

// ------------------------------------------------------------------------------------------ Look
void Look::refresh()
{
    setColour (juce::Slider::textBoxTextColourId, cTx());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, cAm().withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d1018));
    setColour (juce::TextEditor::textColourId, juce::Colour (kText));
    setColour (juce::TextEditor::outlineColourId, juce::Colour (kLine));
    setColour (juce::Label::textColourId, cTx());
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto col = accent ((int) s.getProperties().getWithDefault ("acc", 0));
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float th = 5.f, rr = r - th * 0.5f;
    g.setColour (cPanel2()); g.fillEllipse (c.x - r + th, c.y - r + th, 2 * (r - th), 2 * (r - th));
    juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, a1, true);
    g.setColour (cLine()); g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    if (s.isEnabled())
    {
        juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0.f, a0, ang, true);
        g.setColour (col); g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path tick; tick.addRoundedRectangle (-1.6f, -(r - th - 2.f), 3.2f, r * 0.40f, 1.6f);
    g.setColour (cTx()); g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto col = accent ((int) s.getProperties().getWithDefault ("acc", 0));
    const float cy = (float) y + (float) h * 0.5f, x0 = (float) x + 10.f, x1 = (float) (x + w) - 10.f;
    g.setColour (cLine()); g.fillRoundedRectangle (x0, cy - 4.f, x1 - x0, 8.f, 4.f);
    g.setColour (col); g.fillRoundedRectangle (x0, cy - 4.f, juce::jmax (0.f, pos - x0), 8.f, 4.f);
    g.setColour (cTx()); g.fillEllipse (pos - 11.f, cy - 11.f, 22.f, 22.f);
    g.setColour (cPanel()); g.drawEllipse (pos - 11.f, cy - 11.f, 22.f, 22.f, 3.f);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    const auto col = accent ((int) b.getProperties().getWithDefault ("acc", 0));
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? col.withAlpha (0.16f) : cPanel()); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? col : (hl ? cMu() : cLine())); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.3f);
    const float dot = 8.f;
    g.setColour (on ? col : cGray()); g.fillEllipse (r.getX() + 14.f, r.getCentreY() - dot * 0.5f, dot, dot);
    g.setFont (fontOf (12.f, true)); g.setColour (on ? cTx() : cMu());
    g.drawText (b.getButtonText(), r.withTrimmedLeft (30.f).toNearestInt(), juce::Justification::centredLeft);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down)
{
    const auto col = accent ((int) b.getProperties().getWithDefault ("acc", 0));
    const bool primary = (bool) b.getProperties().getWithDefault ("primary", false), on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (primary) { g.setColour (down ? col.darker (0.2f) : col); g.fillRoundedRectangle (r, 9.f); return; }
    g.setColour (on ? col.withAlpha (0.16f) : (down ? cLine() : cPanel2())); g.fillRoundedRectangle (r, 9.f);
    g.setColour (on ? col : (hl ? cMu() : cLine())); g.drawRoundedRectangle (r, 9.f, 1.2f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto col = accent ((int) b.getProperties().getWithDefault ("acc", 0));
    const bool primary = (bool) b.getProperties().getWithDefault ("primary", false), on = b.getToggleState();
    g.setFont (fontOf (12.f, true));
    g.setColour (! b.isEnabled() ? cGray() : (primary ? cOnAcc() : (on ? col : cTx())));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    if ((bool) b.getProperties().getWithDefault ("used", false))
    {
        g.setColour (primary ? cOnAcc() : col); g.fillEllipse ((float) b.getWidth() - 11.f, 5.f, 5.f, 5.f);
    }
}

// ------------------------------------------------------------------------------------------ Knob / Seg / Pill / HSlider
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& t, const juce::String& suffix, int decimals, int acc) : title (t)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
    slider.setTextValueSuffix (suffix); slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setDoubleClickReturnValue (true, apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getDefaultValue()));
    slider.getProperties().set ("acc", acc);
    addAndMakeVisible (slider);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
}
void Knob::paint (juce::Graphics& g) { caption (g, title, { 0, 0, getWidth(), 16 }, juce::Justification::centred); }
void Knob::resized() { slider.setBounds (0, 16, getWidth(), getHeight() - 16); }

Seg::Seg (juce::AudioProcessorValueTreeState& apvts, const char* id, juce::StringArray l, int a) : labels (std::move (l)), acc (a)
{
    auto* param = apvts.getParameter (id);
    attach = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { selected = juce::roundToInt (v); repaint(); });
    attach->sendInitialUpdate();
}
Seg::Seg (juce::StringArray l, int a, std::function<void (int)> cb) : labels (std::move (l)), acc (a), onChange (std::move (cb)) {}
void Seg::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (cBg()); g.fillRoundedRectangle (r, 10.f);
    g.setColour (cLine()); g.drawRoundedRectangle (r, 10.f, 1.f);
    const float w = r.getWidth() / (float) labels.size();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.f);
        const bool on = i == selected;
        if (on)
        {
            if (acc == 2)   // seletor de modo: âmbar / ciano / degradê
            {
                if (i == 2) g.setGradientFill (juce::ColourGradient (cAm(), cell.getX(), 0.f, cCy(), cell.getRight(), 0.f, false));
                else g.setColour (i == 0 ? cAm() : cCy());
            }
            else g.setColour (accent (acc));
            g.fillRoundedRectangle (cell, 8.f);
        }
        g.setColour (on ? cOnAcc() : cMu()); g.setFont (fontOf (12.f, true));
        g.drawText (labels[i], cell.toNearestInt(), juce::Justification::centred);
    }
}
void Seg::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    if (attach != nullptr) attach->setValueAsCompleteGesture ((float) i);
    else { selected = i; repaint(); if (onChange) onChange (i); }
}

Pill::Pill (juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& text, int acc) : juce::ToggleButton (text)
{
    getProperties().set ("acc", acc);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *this);
}

HSlider::HSlider (juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& t, const juce::String& sfx, int dec, int acc) : title (t), suffix (sfx), decimals (dec)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.getProperties().set ("acc", acc); s.onValueChange = [this] { repaint(); };
    s.setDoubleClickReturnValue (true, apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (s);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, s);
}
void HSlider::resized() { s.setBounds (0, 22, getWidth(), getHeight() - 22); }
void HSlider::paint (juce::Graphics& g)
{
    caption (g, title, { 0, 0, getWidth() / 2, 18 });
    g.setColour (cTx()); g.setFont (fontOf (15.f, true));
    g.drawText (juce::String (s.getValue(), decimals) + suffix, getWidth() / 2, 0, getWidth() / 2, 18, juce::Justification::centredRight);
}

// ------------------------------------------------------------------------------------------ GrView
void GrView::tick()
{
    auto& m = proc.meters;
    hist[(size_t) pos] = { m.inPeakDb.load(), m.outPeakDb.load(), m.grDb.load() };
    pos = (pos + 1) % kN; repaint();
}
void GrView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.f, 2.f);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f - 6.f, floorDb = -36.f;
    auto yOf = [&] (float db) { return half * juce::jlimit (0.f, 1.f, (db - floorDb) / -floorDb); };
    g.setFont (fontOf (10.f));
    for (float db : { -6.f, -12.f, -24.f })
    {
        g.setColour (cLine());
        g.drawHorizontalLine ((int) (mid - yOf (db)), area.getX(), area.getRight()); g.drawHorizontalLine ((int) (mid + yOf (db)), area.getX(), area.getRight());
        g.setColour (cMu()); g.drawText (juce::String ((int) db), (int) area.getX() + 2, (int) (mid - yOf (db)) - 12, 30, 12, juce::Justification::left);
    }
    const float step = area.getWidth() / (float) (kN - 1);
    juce::Path gr; gr.startNewSubPath (area.getX(), area.getY());
    for (int i = 0; i < kN; ++i)
    {
        const auto& p = hist[(size_t) ((pos + i) % kN)];
        gr.lineTo (area.getX() + step * (float) i, area.getY() + juce::jlimit (0.f, 1.f, -p.gr / 18.f) * area.getHeight() * 0.5f);
    }
    gr.lineTo (area.getRight(), area.getY()); gr.closeSubPath();
    g.setColour (cBad().withAlpha (0.30f)); g.fillPath (gr);
    for (int pass = 0; pass < 2; ++pass)
    {
        g.setColour (pass == 0 ? cGray().withAlpha (0.55f) : accent (acc).withAlpha (0.85f));
        for (int i = 0; i < kN; ++i)
        {
            const auto& p = hist[(size_t) ((pos + i) % kN)];
            const float h = yOf (pass == 0 ? p.in : p.out), x = area.getX() + step * (float) i;
            g.fillRect (x, mid - h, std::max (1.5f, step), 2.f * h);
        }
    }
    const float ceilDb = R (proc, ids::ceiling);
    g.setColour (cTx().withAlpha (0.85f));
    const float dash[] = { 5.f, 4.f };
    for (int s = -1; s <= 1; s += 2)
        g.drawDashedLine (juce::Line<float> (area.getX(), mid + (float) s * yOf (ceilDb), area.getRight(), mid + (float) s * yOf (ceilDb)), dash, 2, 1.2f);
    g.setColour (cTx()); g.setFont (fontOf (11.f, true));
    g.drawText ("TETO " + juce::String (ceilDb, 1) + " dB", (int) area.getRight() - 130, (int) area.getY() + 2, 126, 14, juce::Justification::right);
}

// ------------------------------------------------------------------------------------------ Curva e harmónicos reais do Clipper
namespace
{
    lx::dsp::ClipperParams clipperParamsOf (LoudnexProcessor& p, double& hash)
    {
        lx::dsp::ClipperParams cp;
        cp.gainDb = R (p, ids::gain); cp.ceilingDb = R (p, ids::ceiling); cp.knee = R (p, ids::knee) * 0.01; cp.mix = 1.0;
        cp.shape = (lx::dsp::ClipShape) juce::jlimit (0, 3, juce::roundToInt (R (p, ids::shape))); cp.cexp = R (p, ids::cexp); cp.bias = R (p, ids::bias) * 0.01;
        cp.posOffsetDb = R (p, ids::clippos); cp.negOffsetDb = R (p, ids::clipneg); cp.dual = R (p, ids::dual) > 0.5f; cp.dualOffsetDb = R (p, ids::dualoff);
        cp.postGainDb = R (p, ids::postgain);
        const int hc = juce::roundToInt (R (p, ids::hchar));
        if (hc > 0) cp = lx::dsp::applyCharacter (cp, (lx::dsp::HarmChar) juce::jlimit (0, 3, hc - 1));
        cp.transSel = 0.0; cp.dcBlock = false;      // a curva estática não inclui as partes dependentes do tempo
        hash = cp.gainDb * 1.1 + cp.ceilingDb * 2.3 + cp.knee * 3.7 + (int) cp.shape * 5.1 + cp.cexp * 7.3 + cp.bias * 11.0 + cp.posOffsetDb * 13.0 + cp.negOffsetDb * 17.0
             + (cp.dual ? 19.0 : 0.0) + cp.dualOffsetDb * 23.0 + cp.postGainDb * 29.0;
        return cp;
    }
}

void CurveView::refresh()
{
    double h = 0; auto cp = clipperParamsOf (proc, h);
    if (h == keyHash) return;
    keyHash = h;
    lx::dsp::Clipper cl; cl.prepare (48000.0); cl.setParams (cp); cl.settle();
    for (int i = 0; i < 64; ++i) (void) cl.process (0.f);
    pts.clear(); ceilLin = std::pow (10.0, cp.ceilingDb / 20.0);
    for (int i = 0; i <= 240; ++i)
    {
        const float x = -1.5f + 3.f * (float) i / 240.f;
        pts.push_back ({ x, cl.process (x) });
    }
    repaint();
}
void CurveView::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat().reduced (8.f);
    const float s = std::min (a.getWidth(), a.getHeight()) / 3.2f; const auto c = a.getCentre();
    auto X = [&] (float v) { return c.x + v * s; };
    auto Y = [&] (float v) { return c.y - v * s; };
    g.setColour (cLine()); g.drawVerticalLine ((int) c.x, a.getY(), a.getBottom()); g.drawHorizontalLine ((int) c.y, a.getX(), a.getRight());
    g.setColour (cAm().withAlpha (0.6f));
    const float dash[] = { 4.f, 4.f };
    for (int sgn = -1; sgn <= 1; sgn += 2)
        g.drawDashedLine (juce::Line<float> (a.getX(), Y ((float) sgn * (float) ceilLin), a.getRight(), Y ((float) sgn * (float) ceilLin)), dash, 2, 1.f);
    g.setColour (cGray().withAlpha (0.6f));
    g.drawLine (X (-1.5f), Y (-1.5f), X (1.5f), Y (1.5f), 1.f);
    juce::Path p; bool first = true;
    for (auto& q : pts) { const float yy = juce::jlimit (-1.55f, 1.55f, q.y); if (first) { p.startNewSubPath (X (q.x), Y (yy)); first = false; } else p.lineTo (X (q.x), Y (yy)); }
    g.setColour (cCy()); g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (cMu()); g.setFont (fontOf (10.f, true));
    g.drawText ("ENTRADA", (int) a.getRight() - 60, (int) c.y + 4, 60, 12, juce::Justification::right);
    g.drawText ("SAIDA", (int) c.x + 4, (int) a.getY(), 60, 12, juce::Justification::left);
    g.setColour (cAm()); g.drawText ("teto", (int) a.getX() + 2, (int) Y ((float) ceilLin) - 14, 40, 12, juce::Justification::left);
}

void HarmView::refresh()
{
    double h = 0; auto cp = clipperParamsOf (proc, h);
    if (h == keyHash) return;
    keyHash = h;
    constexpr int P = 192, N = P * 20; const double fs = 192000.0;
    lx::dsp::Clipper cl; cl.prepare (fs); cl.setParams (cp); cl.settle();
    std::vector<float> y ((size_t) N);
    for (int i = 0; i < P * 4; ++i) (void) cl.process (0.5f * (float) std::sin (2.0 * lx::dsp::kPi * i / P));
    for (int i = 0; i < N; ++i) y[(size_t) i] = cl.process (0.5f * (float) std::sin (2.0 * lx::dsp::kPi * i / P));
    double mag[10] = {};
    for (int k = 1; k <= 9; ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i) { const double ph = 2.0 * lx::dsp::kPi * k * (i % P) / P; re += y[(size_t) i] * std::sin (ph); im += y[(size_t) i] * std::cos (ph); }
        mag[k] = std::sqrt (re * re + im * im) * 2.0 / N;
    }
    double sum = 0;
    for (int k = 2; k <= 9; ++k) { db[k - 2] = (float) (20.0 * std::log10 (mag[k] / std::max (mag[1], 1e-9) + 1e-9)); sum += mag[k] * mag[k]; }
    thd = (float) (100.0 * std::sqrt (sum) / std::max (mag[1], 1e-9));
    repaint();
}
void HarmView::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat().reduced (6.f, 4.f);
    caption (g, T8 ("THD ≈ ") + juce::String (thd, 1) + " %", a.removeFromTop (14.f).toNearestInt(), juce::Justification::centredRight);
    const float labH = 16.f; auto bars = a.withTrimmedBottom (labH + 14.f).withTrimmedTop (14.f);
    const float w = a.getWidth() / 8.f;
    for (int i = 0; i < 8; ++i)
    {
        const float v = juce::jlimit (0.f, 1.f, (db[i] + 90.f) / 90.f), hh = bars.getHeight() * v;
        auto col = cCy().withAlpha ((i % 2 == 1) ? 1.f : 0.5f);
        g.setColour (col); g.fillRoundedRectangle (a.getX() + w * (float) i + 6.f, bars.getBottom() - hh, w - 12.f, hh, 3.f);
        g.setColour (cMu()); g.setFont (fontOf (10.f));
        g.drawText (juce::String (juce::roundToInt (db[i])), (int) (a.getX() + w * (float) i), (int) (bars.getBottom() - hh) - 14, (int) w, 12, juce::Justification::centred);
        g.setColour (cTx()); g.setFont (fontOf (11.f, true));
        g.drawText ("H" + juce::String (i + 2), (int) (a.getX() + w * (float) i), (int) bars.getBottom() + 4, (int) w, 14, juce::Justification::centred);
    }
}

// ------------------------------------------------------------------------------------------ Espectro
void SpectrumView::refresh()
{
    std::vector<float> buf (8192);
    proc.snapshotOutput (buf.data(), 8192);
    auto f = lx::dsp::computeSpectrumFrame (buf.data(), 8192, proc.currentSampleRate());
    float mx = -200.f;
    for (int b = 0; b < 64; ++b) { band[b] = band[b] * 0.5f + f.bandDb[b] * 0.5f; mx = std::max (mx, band[b]); }
    have = mx > -120.f; repaint();
}
void SpectrumView::paint (juce::Graphics& g)
{
    const float X0 = 38.f, W = (float) getWidth() - 50.f, T = 8.f, Hh = (float) getHeight() - 34.f;
    auto fx = [&] (float f) { return X0 + W * std::log10 (f / 20.f) / 3.f; };
    auto Y = [&] (float db) { return T + Hh * juce::jlimit (0.f, 1.f, (-10.f - db) / 90.f); };
    g.setFont (fontOf (10.f));
    for (float db : { -10.f, -30.f, -50.f, -70.f, -90.f }) { g.setColour (cLine()); g.drawHorizontalLine ((int) Y (db), X0, X0 + W); g.setColour (cMu()); g.drawText (juce::String ((int) db), 0, (int) Y (db) - 6, 32, 12, juce::Justification::right); }
    const std::pair<float, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    for (auto& q : fl) { g.setColour (cLine().withAlpha (0.6f)); g.drawVerticalLine ((int) fx (q.first), T, T + Hh); g.setColour (cMu()); g.drawText (q.second, (int) fx (q.first) - 16, (int) (T + Hh) + 6, 32, 12, juce::Justification::centred); }
    const bool mbOn = R (proc, ids::mb) > 0.5f; const float x1 = R (proc, ids::xo1), x2 = R (proc, ids::xo2);
    const float al = mbOn ? 1.f : 0.45f;
    const float ex[] = { fx (20.f), fx (x1), fx (x2), fx (20000.f) }; const char* nm[] = { "GRAVES", "MEDIOS", "AGUDOS" }; const float op[] = { 0.13f, 0.06f, 0.13f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (cCy().withAlpha (op[i] * al)); g.fillRect (ex[i], T, ex[i + 1] - ex[i], Hh);
        g.setColour (cCy().withAlpha (al)); g.setFont (fontOf (11.f, true)); g.drawText (nm[i], (int) ex[i] + 6, (int) T + 4, 80, 14, juce::Justification::left);
    }
    if (have)
    {
        juce::Path p;
        for (int b = 0; b < 64; ++b)
        {
            const float x = X0 + W * ((float) b + 0.5f) / 64.f; const float y = Y (band[b]);
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        juce::Path fill (p); fill.lineTo (X0 + W * 63.5f / 64.f, T + Hh); fill.lineTo (X0 + W * 0.5f / 64.f, T + Hh); fill.closeSubPath();
        g.setColour (cCy().withAlpha (0.14f)); g.fillPath (fill);
        g.setColour (cCy()); g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else { g.setColour (cMu()); g.setFont (fontOf (13.f)); g.drawText (T8 ("Toque áudio para ver o espectro da saída"), getLocalBounds().reduced (40), juce::Justification::centred); }
    for (int i = 1; i <= 2; ++i)
    {
        const float x = ex[i]; const juce::String t = (i == 1 ? x1 : x2) >= 1000.f ? juce::String ((i == 1 ? x1 : x2) / 1000.f, 1) + "k" : juce::String ((int) (i == 1 ? x1 : x2));
        g.setColour (cTx().withAlpha (al)); g.drawVerticalLine ((int) x, T, T + Hh);
        g.setColour (cTx()); g.fillRoundedRectangle (x - 22.f, T + Hh * 0.45f, 44.f, 22.f, 6.f);
        g.setColour (cBg()); g.setFont (fontOf (11.f, true)); g.drawText (t, (int) x - 22, (int) (T + Hh * 0.45f), 44, 22, juce::Justification::centred);
    }
}

// ------------------------------------------------------------------------------------------ Medidores
void MeterView::tick()
{
    auto& m = proc.meters;
    inD = std::max (m.inPeakDb.load(), inD - 1.2f); outD = std::max (m.outPeakDb.load(), outD - 1.2f);
    repaint();
}
void MeterView::paint (juce::Graphics& g)
{
    const auto& m = proc.meters; const int ac = mode == 0 ? 0 : 1;
    caption (g, T8 ("Níveis · pico"), { 16, 12, 200, 16 });
    auto bar = [&] (float x, float db, const juce::String& name, juce::Colour c)
    {
        const float top = 66.f, hgt = 250.f, v = juce::jlimit (0.f, 1.f, (db + 36.f) / 36.f);
        g.setColour (cBg()); g.fillRoundedRectangle (x, top, 30.f, hgt, 6.f);
        g.setColour (c); g.fillRoundedRectangle (x, top + hgt * (1.f - v), 30.f, hgt * v, 6.f);
        g.setColour (cMu()); g.setFont (fontOf (10.f, true)); g.drawText (name, (int) x - 12, (int) (top + hgt + 4.f), 54, 12, juce::Justification::centred);
        g.setColour (cTx()); g.drawText (dbText (db), (int) x - 12, (int) (top - 18.f), 54, 14, juce::Justification::centred);
    };
    bar (30.f, inD, "ENTRADA", cGray()); bar (92.f, outD, T8 ("SAÍDA"), accent (ac));
    auto big = [&] (const juce::String& label, const juce::String& val, int y, juce::Colour c, float size)
    {
        caption (g, label, { 150, y, 150, 14 }, juce::Justification::centred);
        g.setColour (c); g.setFont (fontOf (size, true)); g.drawText (val, 150, y + 14, 150, (int) (size + 8), juce::Justification::centred);
    };
    big (T8 ("Redução"), juce::String (m.grDb.load(), 1) + " dB", 50, cBad(), 30.f);
    big (T8 ("LUFS momentâneo"), dbText (m.momentary.load()), 126, cTx(), 24.f);
    big ("LUFS curto prazo", dbText (m.shortTerm.load()), 188, cTx(), 24.f);
    big ("LUFS integrado", dbText (m.integrated.load()), 250, accent (ac), 24.f);
    int y = 350;
    auto row = [&] (const juce::String& l, const juce::String& v, juce::Colour c)
    {
        g.setColour (cLine()); g.drawHorizontalLine (y - 3, 16.f, (float) getWidth() - 16.f);
        g.setColour (cMu()); g.setFont (fontOf (12.f)); g.drawText (l, 18, y, 170, 22, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (fontOf (13.f, true)); g.drawText (v, 150, y, getWidth() - 168, 22, juce::Justification::centredRight); y += 26;
    };
    const double lat = proc.reportedLatency() * 1000.0 / std::max (1.0, proc.currentSampleRate());
    row ("True Peak max.", juce::String (m.tpMaxDb.load(), 2) + " dBTP", cTx());
    row ("GR max.", juce::String (m.grMaxDb.load(), 1) + " dB", cTx());
    row ("LRA", juce::String (m.lra.load(), 1) + " LU", cTx());
    row (T8 ("Latência"), juce::String (lat, 2) + " ms", cTx());
    const int clamps = m.clamps.load();
    row ("Barreira de pico", clamps == 0 ? juce::String ("sem intervencoes") : juce::String (clamps) + " intervencoes", clamps == 0 ? cOk() : cBad());
    if (mode != 0)
    {
        y += 8; caption (g, "Clipper", { 16, y, 200, 16 }); y += 22;
        row ("Densidade de picos", juce::String (m.clipDensityPct.load(), 1) + " %", cTx());
        row (T8 ("Corte máximo"), juce::String (m.clipMaxDb.load(), 1) + " dB", cTx());
        row ("Eventos", juce::String (m.clipEvents.load()), cTx());
        row (T8 ("Duração média"), juce::String (m.clipAvgSamples.load(), 1) + " amostras", cTx());
        row ("Oversampling ativo", kOsNames[juce::jlimit (0, 3, m.osStage.load())], cCy());
    }
}

// ------------------------------------------------------------------------------------------ Auto-Dose
DoseBar::DoseBar (LoudnexProcessor& p) : proc (p)
{
    start.onClick = [this] { proc.startAutoDose(); };
    apply.onClick = [this] { proc.applyAutoDose(); };
    apply.getProperties().set ("primary", true);
    start.setButtonText (T8 ("Auto-Dose · ouvir 8 s"));
    addAndMakeVisible (start); addAndMakeVisible (apply);
}
void DoseBar::resized() { start.setBounds (14, 12, 190, 40); apply.setBounds (212, 12, 90, 40); }
void DoseBar::tick() { apply.setEnabled (proc.dose.state.load() == 2); repaint(); }
void DoseBar::paint (juce::Graphics& g)
{
    const int st = proc.dose.state.load(); auto& d = proc.dose;
    juce::String msg;
    if (st == 0) msg = T8 ("Toque o áudio e carregue em Auto-Dose: ouve 8 s e sugere ganho, release e lookahead.");
    else if (st == 1) msg = T8 ("A ouvir... ") + juce::String ((int) (d.progress.load() * 100.f)) + " %";
    else
    {
        msg = T8 ("Sugestão: ganho +") + juce::String (d.gainDb.load(), 1) + T8 (" dB  ·  release ") + juce::String (juce::roundToInt (d.releaseMs.load())) + T8 (" ms  ·  lookahead ") + juce::String (d.lookaheadMs.load(), 1)
              + T8 (" ms  ·  GR esperado ") + juce::String (d.expectedGrDb.load(), 1) + " dB";
        if (! d.reachable.load()) msg += T8 ("  ·  alvo inalcançável com este teto");
    }
    g.setColour (cMu()); g.setFont (fontOf (12.f)); g.drawText (msg, 316, 10, getWidth() - 330, 18, juce::Justification::centredLeft);
    auto bar = juce::Rectangle<float> (316.f, 38.f, (float) getWidth() - 332.f, 7.f);
    g.setColour (cLine()); g.fillRoundedRectangle (bar, 3.5f);
    g.setColour (st == 2 ? cOk() : cAm()); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (st == 2 ? 1.f : d.progress.load())), 3.5f);
}

// ------------------------------------------------------------------------------------------ Assistentes
AssistView::AssistView (LoudnexProcessor& p) : proc (p)
{
    listen.setButtonText (T8 ("Auto-Dose · ouvir 8 s")); listen.onClick = [this] { proc.startAutoDose(); note = {}; };
    analyse.setButtonText ("Analisar material");
    analyse.onClick = [this] { note = proc.runAssistants() ? juce::String() : T8 ("Faça primeiro o Auto-Dose com áudio a tocar (8 s)."); repaint(); };
    applyAll.setButtonText ("Aplicar tudo"); applyAll.getProperties().set ("primary", true);
    applyAll.onClick = [this] { proc.applyAssistants(); };
    addAndMakeVisible (listen); addAndMakeVisible (analyse); addAndMakeVisible (applyAll);
}
void AssistView::resized() { listen.setBounds (16, 44, 190, 40); analyse.setBounds (214, 44, 170, 40); applyAll.setBounds (392, 44, 150, 40); }
void AssistView::tick() { applyAll.setEnabled (proc.assistInfo().valid); repaint(); }
void AssistView::paint (juce::Graphics& g)
{
    caption (g, "Assistentes", { 16, 14, 300, 16 });
    g.setColour (cMu()); g.setFont (fontOf (11.f)); g.drawText (T8 ("regras determinísticas, não é IA"), getWidth() - 316, 14, 300, 16, juce::Justification::centredRight);
    const auto& a = proc.assistInfo();
    const int st = proc.dose.state.load();
    g.setColour (cMu()); g.setFont (fontOf (12.f));
    g.drawText (st == 1 ? T8 ("A ouvir... ") + juce::String ((int) (proc.dose.progress.load() * 100.f)) + " %" : (note.isNotEmpty() ? note : (a.valid ? T8 ("Análise pronta (8 s capturados)") : T8 ("Sem análise: ouça 8 s de áudio e carregue em Analisar."))),
                560, 44, getWidth() - 576, 40, juce::Justification::centredLeft);
    auto item = [&] (int x, int y, const juce::String& label, const juce::String& val, juce::Colour c = cTx())
    {
        caption (g, label, { x, y, 400, 14 }); g.setColour (a.valid ? c : cGray()); g.setFont (fontOf (16.f, true));
        g.drawText (a.valid ? val : "-", x, y + 16, 410, 22, juce::Justification::centredLeft);
    };
    item (16, 112, "Material", "crest " + juce::String (a.profile.crestDb, 1) + T8 (" dB  ·  transiência ") + juce::String (a.profile.transientness, 2));
    item (16, 168, "Smart Clip Amount", "+" + juce::String (a.clipGainDb, 1) + " dB no clipper", cCy());
    item (16, 224, "Auto Routing", a.order == lx::dsp::HybridOrder::ClipThenLimit ? "Clip>Limit" : "Limit>Clip");
    item (16, 280, "Smart Distribution", "clipper " + juce::String (a.clipShareDb, 1) + T8 (" dB  ·  limiter ") + juce::String (a.limitShareDb, 1) + T8 (" dB  ·  ratio ") + juce::String (juce::roundToInt (a.ratio * 100.0)) + " %", cAm());
    caption (g, T8 ("Quality Optimizer (carácter harmónico)"), { 470, 112, 420, 14 });
    for (int i = 0; i < 4; ++i)
    {
        const int y = 138 + i * 40; const bool best = a.valid && a.qo.best == i;
        g.setColour (best ? cCy().withAlpha (0.14f) : cPanel2()); g.fillRoundedRectangle (470.f, (float) y, 418.f, 34.f, 8.f);
        g.setColour (best ? cCy() : cLine()); g.drawRoundedRectangle (470.5f, (float) y + 0.5f, 417.f, 33.f, 8.f, 1.f);
        g.setColour (cTx()); g.setFont (fontOf (13.f, true)); g.drawText (kCharNames[i], 482, y, 100, 34, juce::Justification::centredLeft);
        g.setColour (a.valid ? cTx() : cGray()); g.setFont (fontOf (12.f));
        if (a.valid)
            g.drawText (juce::String (a.qo.cand[i].loudGainDb, 1) + T8 (" dB loud  ·  DSR ") + juce::String (a.qo.cand[i].dsrDb, 1) + T8 (" dB  ·  ") + juce::String (a.qo.cand[i].score, 1),
                        570, y, 310, 34, juce::Justification::centredRight);
    }
    g.setColour (cMu()); g.setFont (fontOf (11.f));
    g.drawText (T8 ("pontuação = ganho de loudness − penalização da distorção relativa; fórmula explícita, não mede qualidade percebida."), 470, 306, 420, 14, juce::Justification::centredLeft);
}

void EffView::paint (juce::Graphics& g)
{
    const auto& m = proc.meters;
    caption (g, T8 ("Loudness efficiency · três números"), { 16, 12, getWidth() - 32, 16 });
    auto item = [&] (int y, const juce::String& val, const juce::String& name, const juce::String& txt, juce::Colour c)
    {
        g.setColour (cBg()); g.fillRoundedRectangle (14.f, (float) y, (float) getWidth() - 28.f, 104.f, 10.f);
        g.setColour (c); g.setFont (fontOf (28.f, true)); g.drawText (val, 28, y + 8, getWidth() - 56, 36, juce::Justification::centredLeft);
        g.setColour (cTx()); g.setFont (fontOf (13.f, true)); g.drawText (name, 28, y + 46, getWidth() - 56, 18, juce::Justification::centredLeft);
        g.setColour (cMu()); g.setFont (fontOf (11.f)); g.drawText (txt, 28, y + 64, getWidth() - 56, 34, juce::Justification::topLeft);
    };
    const float dl = m.effDeltaLufs.load();
    item (44, (dl >= 0 ? "+" : "") + juce::String (dl, 1) + " LU", "Ganho de loudness", T8 ("saída contra entrada, curto prazo"), cTx());
    item (160, juce::String (m.grAvgDb.load(), 1) + " dB", "GR médio", "quanto o plugin trabalhou em média", cAm());
    item (276, dbText (m.distIdxDb.load()) + " dB", T8 ("Índice de distorção"), T8 ("modulação de ganho: quanto mais baixo, melhor"), cCy());
    g.setColour (cMu()); g.setFont (fontOf (11.f)); g.drawText ("Sem nota unica: cada numero conta a sua parte.", 16, 392, getWidth() - 32, 30, juce::Justification::topLeft);
}

// ------------------------------------------------------------------------------------------ Login
Login::Login (std::function<void()> onSuccess) : onOk (std::move (onSuccess))
{
    pw.setPasswordCharacter ((juce::juce_wchar) 0x2022); pw.setTextToShowWhenEmpty ("Senha de acesso", juce::Colour (kDim));
    pw.setJustification (juce::Justification::centred); pw.setFont (fontOf (16.f)); pw.onReturnKey = [this] { submit(); };
    remember.setToggleState (true, juce::dontSendNotification);
    enter.onClick = [this] { submit(); };
    enter.getProperties().set ("primary", true);
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
    g.fillAll (juce::Colour (kBg).withAlpha (0.96f));
    auto card = getLocalBounds().withSizeKeepingCentre (380, 300).toFloat();
    g.setColour (juce::Colour (kPanel)); g.fillRoundedRectangle (card, 18.f);
    g.setColour (juce::Colour (kLine)); g.drawRoundedRectangle (card.reduced (0.5f), 18.f, 1.f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffb224), card.getX(), card.getY(), juce::Colour (0xff3fd6c6), card.getRight(), card.getY(), false));
    g.setFont (fontOf (34.f, true)); g.drawText ("LABLOUD", card.withTrimmedTop (28.f).withHeight (46.f).toNearestInt(), juce::Justification::centred);
    g.setColour (juce::Colour (kDim)); g.setFont (fontOf (12.f));
    g.drawText (T8 ("by Piradex  ·  Limiter + Clipper + Hybrid"), card.withTrimmedTop (76.f).withHeight (18.f).toNearestInt(), juce::Justification::centred);
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
    : AudioProcessorEditor (p), proc (p),
      modeSeg (p.apvts, ids::mode, { "Limiter", "Clipper", "Hybrid" }, 2),
      pageSeg ({ "Principal", T8 ("Multibanda · Análise"), T8 ("Assistentes · Delta") }, 0, [this] (int i) { page = i; layoutAll(); repaint(); }),
      advSeg ({ T8 ("Básico"), T8 ("Avançado") }, 0, [this] (int i) { adv = i == 1; layoutAll(); repaint(); }),
      gain (p.apvts, ids::gain, "Ganho", " dB", 1, 0), ceiling (p.apvts, ids::ceiling, "Teto", " dB", 1, 0),
      release (p.apvts, ids::release, "Release", " ms", 1, 0), lookahead (p.apvts, ids::lookahead, "Lookahead", " ms", 2, 0),
      punch (p.apvts, ids::punch, "Punch", " %", 0, 0), mix (p.apvts, ids::mix, "Mix", " %", 0, 0),
      knee (p.apvts, ids::knee, "Joelho", " %", 0, 1), cexp (p.apvts, ids::cexp, "Dureza", "", 1, 1),
      bias (p.apvts, ids::bias, T8 ("Viés"), " %", 0, 1), postgain (p.apvts, ids::postgain, "Post-gain", " dB", 1, 1),
      bassamt (p.apvts, ids::bassamt, "Bass Guard", " %", 0, 0), rlink (p.apvts, ids::rlink, "Release Link", " %", 0, 0),
      link (p.apvts, ids::link, "Stereo Link", " %", 0, 0), bassfreq (p.apvts, ids::bassfreq, "Bass Guard Hz", " Hz", 0, 0),
      clippos (p.apvts, ids::clippos, "Clip +", " dB", 1, 1), clipneg (p.apvts, ids::clipneg, T8 ("Clip −"), " dB", 1, 1),
      transel (p.apvts, ids::transel, "Transiente", " %", 0, 1), dualoff (p.apvts, ids::dualoff, "Offset dual", " dB", 1, 1),
      xo1 (p.apvts, ids::xo1, T8 ("Cruzamento 1"), " Hz", 0, 1), xo2 (p.apvts, ids::xo2, T8 ("Cruzamento 2"), " Hz", 0, 1),
      mbg1 (p.apvts, ids::mbg1, T8 ("Graves"), " dB", 1, 1), mbg2 (p.apvts, ids::mbg2, T8 ("Médios"), " dB", 1, 1), mbg3 (p.apvts, ids::mbg3, T8 ("Agudos"), " dB", 1, 1),
      tp (p.apvts, ids::tp, "True Peak", 0), bass (p.apvts, ids::bass, "Bass Guard", 0), gainmatch (p.apvts, ids::gainmatch, "Gain Match", 0),
      ceilock (p.apvts, ids::ceilock, "Ceiling Lock", 0), dual (p.apvts, ids::dual, "Dual-stage", 1), dcblock (p.apvts, ids::dcblock, "DC Blocker", 1),
      mb (p.apvts, ids::mb, "Multibanda", 1), delta (p.apvts, ids::delta, "Ouvir Delta", 0), ms (p.apvts, ids::ms, "Mid/Side", 0), hq (p.apvts, ids::hq, "HQ offline", 1),
      character (p.apvts, ids::character, { "Clean", "Punch", "Loud" }, 0),
      relmode (p.apvts, ids::relmode, { "Single", "Dual", "Adaptive" }, 0),
      spectral (p.apvts, ids::spectral, { "Flat", "Perceptual", "Bass-safe" }, 0),
      distguard (p.apvts, ids::distguard, { "Off", "Soft", "Normal", "Strict" }, 0),
      shape (p.apvts, ids::shape, { "Soft", "Cubic", "Hard", "Custom" }, 1),
      os (p.apvts, ids::os, { "1x", "4x", "16x", "32x" }, 1),
      alias (p.apvts, ids::alias, { "Manual", "ECO", "AUTO", "MAX" }, 1),
      hchar (p.apvts, ids::hchar, { "Manual", "Clean", "Warm", "Punch", "Aggressive" }, 1),
      route (p.apvts, ids::route, { "Clip>Limit", "Limit>Clip" }, 2),
      deltasel (p.apvts, ids::deltasel, { "Total", "Clipper", "Limiter" }, 0),
      ratio (p.apvts, ids::ratio, "Clip / Limit", " %", 0, 1), balance (p.apvts, ids::balance, T8 ("Balance · Punch ... Loud"), " %", 0, 0),
      grView (p, 0), curve (p), harm (p), spectrum (p), meterView (p), doseBar (p), assistView (p), effView (p),
      login ([this] { timerCallback(); })
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    const char* sn[] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i) { slotBtn[i].setButtonText (sn[i]); slotBtn[i].onClick = [this, i] { slotClicked (i); }; }
    saveBtn.setClickingTogglesState (true); saveBtn.onClick = [this] { saveMode = saveBtn.getToggleState(); };
    undoBtn.onClick = [this] { proc.undo(); }; redoBtn.onClick = [this] { proc.redo(); };
    skinBtn.onClick = [this] { lightSkin() = ! lightSkin(); applySkin(); };
    logoutBtn.onClick = [this] { AccessManager::get().logout(); timerCallback(); };
    advSeg.setSelected (0); pageSeg.setSelected (0);

    for (auto* c : std::initializer_list<juce::Component*> {
             &modeSeg, &pageSeg, &advSeg, &slotBtn[0], &slotBtn[1], &slotBtn[2], &slotBtn[3], &saveBtn, &undoBtn, &redoBtn, &skinBtn, &logoutBtn,
             &gain, &ceiling, &release, &lookahead, &punch, &mix, &knee, &cexp, &bias, &postgain, &bassamt, &rlink, &link, &bassfreq, &clippos, &clipneg,
             &transel, &dualoff, &xo1, &xo2, &mbg1, &mbg2, &mbg3, &tp, &bass, &gainmatch, &ceilock, &dual, &dcblock, &mb, &delta, &ms, &hq,
             &character, &relmode, &spectral, &distguard, &shape, &os, &alias, &hchar, &route, &deltasel, &ratio, &balance,
             &grView, &curve, &harm, &spectrum, &meterView, &doseBar, &assistView, &effView })
    {
        content.addChildComponent (c); everything.push_back (c);
    }
    content.addAndMakeVisible (login);
    balance.s.onDragEnd = [this] { proc.applyBalance(); };

    applySkin();
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1280.0 / 820.0);
    setResizeLimits (800, 513, 2200, 1409);
    setSize (1280, 820);
    startTimerHz (30);
    timerCallback();
}

LoudnexEditor::~LoudnexEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LoudnexEditor::applySkin()
{
    look.refresh();
    sendLookAndFeelChange();
    skinBtn.setButtonText (lxui::lightSkin() ? "Skin: Clara" : "Skin: Escura");
    login.toFront (false);
    content.repaint(); repaint();
    for (auto* c : everything) c->repaint();
}

void LoudnexEditor::slotClicked (int i)
{
    if (saveMode || ! proc.slotUsed (i)) proc.storeSlot (i); else proc.recallSlot (i);
    slotSel = i; saveMode = false; saveBtn.setToggleState (false, juce::dontSendNotification);
}

void LoudnexEditor::resized()
{
    const float s = (float) getWidth() / 1280.f;
    content.setBounds (0, 0, 1280, 820);
    content.setTransform (juce::AffineTransform::scale (s));
    layoutAll();
}

void LoudnexEditor::layoutAll()
{
    cards.clear(); labels.clear();
    for (auto* c : everything) c->setVisible (false);
    login.setBounds (0, 0, 1280, 820);
    const int mode = juce::jlimit (0, 2, shownMode < 0 ? 0 : shownMode);
    constexpr int X0 = 24, CW = 904, RX = 944, RW = 312, Y0 = 112, H = 696;
    auto put = [&] (juce::Component& c, int x, int y, int w, int h) { c.setBounds (x, y, w, h); c.setVisible (true); };
    auto card = [&] (int x, int y, int w, int h, const juce::String& t = {}) { cards.push_back ({ { x, y, w, h }, t }); };
    auto knobs = [&] (std::initializer_list<K*> ks, int x, int y, int w, int kw, int kh)
    {
        const int n = (int) ks.size(), sp = w / n; int i = 0;
        for (auto* k : ks) { put (*k, x + sp * i + (sp - kw) / 2, y, kw, kh); ++i; }
    };

    // ---- cabeçalho
    put (modeSeg, 236, 14, 262, 36);
    put (saveBtn, 538, 15, 70, 34); for (int i = 0; i < 4; ++i) put (slotBtn[i], 616 + i * 38, 15, 34, 34);
    put (undoBtn, 772, 15, 64, 34); put (redoBtn, 844, 15, 64, 34); put (advSeg, 916, 14, 164, 36); put (skinBtn, 1088, 15, 104, 34); put (logoutBtn, 1200, 15, 56, 34);
    put (pageSeg, 24, 68, 560, 34);
    for (int i = 0; i < 4; ++i) { slotBtn[i].getProperties().set ("used", proc.slotUsed (i)); slotBtn[i].setToggleState (i == slotSel, juce::dontSendNotification); slotBtn[i].repaint(); }

    // ---- PÁGINA 0: principal
    if (page == 0)
    {
        card (RX, Y0, RW, H); put (meterView, RX, Y0, RW, H); meterView.mode = mode;
        if (mode == 0)
        {
            const int advH = adv ? 190 : 0, doseH = adv ? 0 : 64;
            const int dispH = H - (adv ? advH + 16 : 0) - 150 - 16 - 44 - 16 - (adv ? 0 : doseH + 16);
            int y = Y0;
            card (X0, y, CW, dispH); grView.acc = 0; put (grView, X0 + 12, y + 10, CW - 24, dispH - 20); y += dispH + 16;
            if (adv)
            {
                card (X0, y, CW, advH, T8 ("Avançado · limiter"));
                put (relmode, X0 + 16, y + 30, 250, 34); put (spectral, X0 + 282, y + 30, 290, 34); put (distguard, X0 + 588, y + 30, 300, 34);
                knobs ({ &bassamt, &rlink, &link, &bassfreq }, X0, y + 72, CW, 112, 112);
                y += advH + 16;
            }
            card (X0, y, CW, 150);
            knobs ({ &gain, &ceiling, &release, &lookahead, &punch, &mix }, X0, y + 10, CW, 118, 132); y += 166;
            put (tp, X0, y + 4, 140, 36); put (bass, X0 + 150, y + 4, 150, 36); put (gainmatch, X0 + 310, y + 4, 150, 36); put (ceilock, X0 + 470, y + 4, 150, 36);
            put (character, X0 + CW - 270, y + 4, 270, 36); y += 60;
            if (! adv) { card (X0, y, CW, 64); put (doseBar, X0, y, CW, 64); }
        }
        else if (mode == 1)
        {
            const int advH = adv ? 138 : 0;
            const int topH = H - (adv ? advH + 16 : 0) - 150 - 16 - 40 - 16 - 40 - 16;
            int y = Y0;
            card (X0, y, 520, topH, T8 ("Curva de transferência")); put (curve, X0 + 10, y + 28, 500, topH - 38);
            card (X0 + 536, y, CW - 536, topH, T8 ("Harmónicos · 1 kHz, -6 dBFS")); put (harm, X0 + 546, y + 26, CW - 556, topH - 36);
            y += topH + 16;
            if (adv) { card (X0, y, CW, advH); knobs ({ &clippos, &clipneg, &transel, &dualoff, &mix }, X0, y + 4, CW, 112, 130); y += advH + 16; }
            card (X0, y, CW, 150); knobs ({ &gain, &ceiling, &knee, &cexp, &bias, &postgain }, X0, y + 10, CW, 118, 132); y += 166;
            put (shape, X0, y, 300, 40); put (os, X0 + 316, y, 220, 40); put (alias, X0 + 552, y, 300, 40); y += 56;
            put (hchar, X0, y, 470, 40); put (tp, X0 + 486, y + 2, 140, 36); put (dual, X0 + 636, y + 2, 150, 36); put (dcblock, X0 + 796, y + 2, 108, 36);
            dcblock.setButtonText ("DC");
        }
        else
        {
            const int advH = adv ? 138 : 0;
            const int dispH = H - 150 - 16 - (adv ? advH + 16 : 0) - 150 - 16 - 40 - 16 - 16;
            int y = Y0;
            card (X0, y, CW, 150, "Cadeia de sinal"); put (route, X0 + 16, y + 40, 250, 40);
            put (ratio, X0 + 292, y + 34, 280, 52); put (balance, X0 + 600, y + 34, 288, 52);
            labels.push_back ({ { X0 + 16, y + 100, CW - 32, 40 }, "@hybrid" }); y += 166;
            card (X0, y, CW, dispH); grView.acc = 1; put (grView, X0 + 12, y + 10, CW - 24, dispH - 20); y += dispH + 16;
            if (adv) { card (X0, y, CW, advH); knobs ({ &punch, &bassamt, &rlink, &cexp, &bias, &postgain }, X0, y + 4, CW, 112, 130); y += advH + 16; }
            card (X0, y, CW, 150); knobs ({ &gain, &ceiling, &release, &lookahead, &knee, &mix }, X0, y + 10, CW, 118, 132); y += 166;
            put (shape, X0, y, 300, 40); put (os, X0 + 316, y, 220, 40); put (tp, X0 + 552, y + 2, 140, 36); put (bass, X0 + 702, y + 2, 150, 36);
        }
    }
    // ---- PÁGINA 1: multibanda e análise
    else if (page == 1)
    {
        card (RX, Y0, RW, H); put (meterView, RX, Y0, RW, H); meterView.mode = 1;
        card (X0, Y0, CW, 320, T8 ("Espectro da saída · 3 bandas")); put (spectrum, X0 + 12, Y0 + 28, CW - 24, 284);
        const int bw = (CW - 32) / 3, y = Y0 + 336;
        K* mk[3] = { &mbg1, &mbg2, &mbg3 };
        
        for (int i = 0; i < 3; ++i) { card (X0 + i * (bw + 16), y, bw, 150); put (*mk[i], X0 + i * (bw + 16) + (bw - 118) / 2, y + 18, 118, 128); }
        const int y2 = y + 166;
        card (X0, y2, CW, H - (y2 - Y0), "Multibanda e oversampling");
        put (mb, X0 + 16, y2 + 34, 160, 36); put (xo1, X0 + 190, y2 + 22, 112, 128); put (xo2, X0 + 310, y2 + 22, 112, 128);
        put (os, X0 + 450, y2 + 34, 220, 36); put (alias, X0 + 690, y2 + 34, 200, 36);
        labels.push_back ({ { X0 + 450, y2 + 78, CW - 466, 60 }, "@band" });
    }
    // ---- PÁGINA 2: assistentes, delta e sistema
    else
    {
        card (RX, Y0, RW, 432); put (effView, RX, Y0, RW, 432);
        card (X0, Y0, CW, 330); put (assistView, X0, Y0, CW, 330);
        card (X0, Y0 + 346, CW, 130, "Balance e Delta");
        put (balance, X0 + 16, Y0 + 346 + 32, 400, 52); put (delta, X0 + 450, Y0 + 346 + 40, 160, 36); put (deltasel, X0 + 626, Y0 + 346 + 40, 262, 36);
        labels.push_back ({ { X0 + 16, Y0 + 346 + 96, CW - 32, 28 }, "@delta" });
        card (X0, Y0 + 492, CW, 130, "Sistema");
        put (ceilock, X0 + 16, Y0 + 492 + 36, 200, 36); put (gainmatch, X0 + 226, Y0 + 492 + 36, 200, 36); put (ms, X0 + 436, Y0 + 492 + 36, 200, 36); put (hq, X0 + 646, Y0 + 492 + 36, 200, 36);
        labels.push_back ({ { X0 + 16, Y0 + 492 + 84, CW - 32, 36 }, "@system" });
        card (RX, Y0 + 448, RW, H - 448, "Slots A / B / C / D");
        labels.push_back ({ { RX + 16, Y0 + 448 + 34, RW - 32, H - 448 - 44 }, "@slots" });
    }
    login.toFront (false);
    content.repaint(); repaint();
}

void LoudnexEditor::timerCallback()
{
    const bool unlocked = AccessManager::get().isUnlocked();
    if (unlocked != wasUnlocked || shownMode < 0) { wasUnlocked = unlocked; login.setVisible (! unlocked); logoutBtn.setVisible (unlocked); content.repaint(); repaint(); }
    const int mode = juce::roundToInt (R (proc, ids::mode));
    if (mode != shownMode) { shownMode = mode; layoutAll(); }
    if (! unlocked) return;
    ++tick;
    if (page == 0)
    {
        if (mode != 1) grView.tick();
        if (mode != 0 && tick % 3 == 0) { curve.refresh(); harm.refresh(); }
        if (mode == 0 && ! adv) doseBar.tick();
    }
    if (page == 1 && tick % 2 == 0) spectrum.refresh();
    if (page == 2) { assistView.tick(); effView.repaint(); }
    if (page != 2) meterView.tick();
    if (tick % 15 == 0) { for (int i = 0; i < 4; ++i) { slotBtn[i].getProperties().set ("used", proc.slotUsed (i)); slotBtn[i].repaint(); } }
    if (page == 2 && tick % 10 == 0) repaint();
}

void LoudnexEditor::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / 1280.f));
    g.fillAll (cBg());
    g.setGradientFill (juce::ColourGradient (cAm(), 24.f, 14.f, cCy(), 210.f, 14.f, false));
    g.setFont (fontOf (30.f, true)); g.drawText ("LABLOUD", 24, 8, 210, 40, juce::Justification::centredLeft);
    g.setColour (cMu()); g.setFont (fontOf (11.f)); g.drawText ("by Piradex", 26, 46, 120, 14, juce::Justification::centredLeft);
    g.setColour (cLine()); g.drawHorizontalLine (63, 0.f, 1280.f);

    for (auto& c : cards)
    {
        drawCard (g, c.r.toFloat());
        if (c.title.isNotEmpty()) caption (g, c.title, { c.r.getX() + 16, c.r.getY() + 8, c.r.getWidth() - 32, 18 });
    }
    // texto do lado direito da linha de páginas
    const double lat = proc.reportedLatency() * 1000.0 / std::max (1.0, proc.currentSampleRate());
    g.setColour (cMu()); g.setFont (fontOf (11.f, true));
    g.drawText (AccessManager::get().roleText() + T8 ("   ·   ") + T8 ("Latência ") + juce::String (lat, 2) + T8 (" ms   ·   v") + LX_VERSION_STRING, 600, 68, 656, 34, juce::Justification::centredRight);

    for (auto& l : labels)
    {
        g.setColour (cMu()); g.setFont (fontOf (12.f));
        juce::String t = l.second; const auto r = l.first;
        if (t == "@hybrid")
        {
            const double rt = R (proc, ids::ratio) * 0.01, ce = R (proc, ids::ceiling);
            if (juce::roundToInt (R (proc, ids::route)) == 0)
                t = "Clip>Limit: o clipper corta a Teto + (1 - r) x 6 dB = " + juce::String (ce + (1.0 - rt) * 6.0, 1) + " dBFS e o limiter fecha em " + juce::String (ce, 1) + " dBFS.";
            else
                t = "Limit>Clip: o limiter fecha a Teto + r x 3 dB = " + juce::String (ce + rt * 3.0, 1) + " dBFS; o clipper termina em " + juce::String (ce, 1) + " dBFS, seguido da barreira de pico.";
            t += T8 ("  Mais Clipper = mais corte antes do limiter.");
        }
        else if (t == "@band")
            t = T8 ("Aliasing medido num clip duro a 3 kHz (teste interno): 1× −44,5 dB · 4× −67,5 dB · 16× −69,7 dB. De 4× para 16× o ganho é só ~2 dB e o CPU quase triplica; reserve 16× e 32× para exportar.");
        else if (t == "@delta")
            t = T8 ("Delta ouve só o que o plugin removeu (saída − entrada alinhada). Balance mexe em Punch, release, ratio e clip assist ao largar o botão.");
        else if (t == "@system")
            t = T8 ("Ceiling Lock: assistentes, slots e anular não mexem no teto. Gain Match: iguala a loudness da saída à da entrada. Mid/Side: barreira final de pico em L/R. HQ offline: 32× na exportação.");
        else if (t == "@slots")
        {
            t = T8 ("Use os botões A B C D no cabeçalho: clique guarda num slot vazio e chama um slot guardado. Ative \"Guardar\" para sobrescrever. Um ponto no botão marca os slots em uso. Anular e Refazer percorrem as últimas alterações.");
        }
        g.drawFittedText (t, r, juce::Justification::topLeft, 4);
    }
}
