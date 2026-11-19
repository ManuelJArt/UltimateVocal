#include "PluginEditor.h"

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

juce::Font hud::mono (float size, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

namespace
{
    constexpr int kW = 1180, kH = 700;

    juce::String noteLabel (int midi)
    {
        const int pc = ((midi % 12) + 12) % 12;
        const int oct = (int) std::floor ((float) midi / 12.0f) - 1;
        return ids::noteNames[pc] + juce::String (oct);
    }

    bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    void toggleNote (UltimateVocalProcessor& proc, int pc)
    {
        if (auto* p = proc.apvts.getParameter ("n" + juce::String (pc)))
        {
            const bool on = p->getValue() > 0.5f;
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }

    bool noteOn (UltimateVocalProcessor& proc, int pc)
    {
        return proc.apvts.getRawParameterValue ("n" + juce::String (pc))->load() > 0.5f;
    }
}

// ============================================================================
//  LookAndFeel
// ============================================================================
HudLookAndFeel::HudLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, hud::line);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, hud::line);
    setColour (juce::ComboBox::textColourId, hud::line);
    setColour (juce::ComboBox::backgroundColourId, Colour (0x30143a5c));
    setColour (juce::ComboBox::outlineColourId, hud::line.withAlpha (0.5f));
    setColour (juce::ComboBox::arrowColourId, hud::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colour (0xff07182b));
    setColour (juce::PopupMenu::textColourId, hud::line);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, hud::accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void HudLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    g.setColour (Colour (0x28163a5e));
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    g.setColour (hud::line.withAlpha (0.55f));
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.0f);

    // marcas
    for (int i = 0; i <= 20; ++i)
    {
        const float a = startAngle + (float) i / 20.0f * (endAngle - startAngle);
        const float r1 = R + 0.5f, r2 = R - (i % 5 == 0 ? 6.0f : 3.5f);
        g.setColour (hud::line.withAlpha (i % 5 == 0 ? 0.7f : 0.35f));
        g.drawLine (c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, c.x + std::sin (a) * r2, c.y - std::cos (a) * r2, 1.0f);
    }

    const float ra = R - 9.0f;
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, ra, ra, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, ra, ra, 0.0f, startAngle, angle, true);
    g.setColour (hud::dim.withAlpha (0.5f));
    g.strokePath (track, juce::PathStrokeType (2.0f));
    g.setColour (hud::accent.withAlpha (0.22f));
    g.strokePath (value, juce::PathStrokeType (7.0f));
    g.setColour (hud::accent);
    g.strokePath (value, juce::PathStrokeType (2.6f));

    // aguja
    g.setColour (juce::Colours::white);
    g.drawLine (c.x + std::sin (angle) * (ra * 0.35f), c.y - std::cos (angle) * (ra * 0.35f),
                c.x + std::sin (angle) * (ra * 0.85f), c.y - std::cos (angle) * (ra * 0.85f), 1.8f);
    g.setColour (hud::line.withAlpha (0.7f));
    g.drawEllipse (c.x - 3, c.y - 3, 6, 6, 1.0f);
    juce::ignoreUnused (s);
}

void HudLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = Rectangle<float> (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (Colour (0x30143a5c));
    g.fillRect (r);
    g.setColour (hud::line.withAlpha (0.5f));
    g.drawRect (r, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (hud::accent);
    g.fillPath (arrow);
}

void HudLookAndFeel::drawToggleButton (Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? hud::warn.withAlpha (0.28f) : Colour (0x30143a5c));
    g.fillRect (r);
    g.setColour (on ? hud::warn : hud::line.withAlpha (highlighted ? 0.9f : 0.5f));
    g.drawRect (r, 1.0f);
    g.setFont (hud::mono (12.0f, true));
    g.setColour (on ? hud::warn : hud::line);
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

// ============================================================================
//  Potenciómetro
// ============================================================================
HudKnob::HudKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                  const juce::String& title, const juce::String& suffix, int decimals)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 16);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);

    slider.textFromValueFunction = [suffix, decimals] (double v) { return juce::String (v, decimals) + suffix; };
    slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    slider.updateText();

    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, hud::dim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void HudKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (15));
    slider.setBounds (r);
}

// ============================================================================
//  Anillo central
// ============================================================================
void RingComponent::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f - 8.0f;
    const bool voiced = proc.liveVoiced.load();

    // resplandor
    juce::ColourGradient glow (hud::accent.withAlpha (0.42f), c, juce::Colours::transparentBlack, c.translated (R * 1.25f, 0), true);
    g.setGradientFill (glow);
    g.fillEllipse (c.x - R * 1.25f, c.y - R * 1.25f, R * 2.5f, R * 2.5f);

    for (int i = 0; i < 5; ++i)
    {
        g.setColour (hud::accentHi.withAlpha (0.07f * (float) (5 - i)));
        g.drawEllipse (c.x - R - (float) i, c.y - R - (float) i, 2 * (R + (float) i), 2 * (R + (float) i), 1.5f);
    }
    g.setColour (juce::Colours::white);
    g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 2.6f);

    // anillo de marcas
    for (int i = 0; i < 90; ++i)
    {
        const float a = (float) i / 90.0f * juce::MathConstants<float>::twoPi;
        const float r1 = R - 7.0f, r2 = R - (i % 5 == 0 ? 16.0f : 11.0f);
        g.setColour (hud::line.withAlpha (i % 5 == 0 ? 0.75f : 0.35f));
        g.drawLine (c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, c.x + std::sin (a) * r2, c.y - std::cos (a) * r2, 1.0f);
    }
    const float r2 = R * 0.80f;
    g.setColour (hud::line.withAlpha (0.8f));
    g.drawEllipse (c.x - r2, c.y - r2, 2 * r2, 2 * r2, 1.4f);
    const float r3 = R * 0.72f;
    g.setColour (hud::line.withAlpha (0.35f));
    g.drawEllipse (c.x - r3, c.y - r3, 2 * r3, 2 * r3, 1.0f);

    // puntos laterales
    for (float a : { -0.80f, 0.80f })
    {
        const float rr = R * 0.80f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + std::sin (a + 2.35f) * rr - 4.5f, c.y - std::cos (a + 2.35f) * rr - 4.5f, 9.0f, 9.0f);
    }

    // arco de desviación (±50 cents)
    const float midiIn = proc.liveMidiIn.load(), target = proc.liveMidiTarget.load();
    const float cents = voiced ? juce::jlimit (-50.0f, 50.0f, (midiIn - target) * 100.0f) : 0.0f;
    smoothedCents += (cents - smoothedCents) * 0.25f;
    const float arcR = R * 0.91f;
    const float span = juce::degreesToRadians (62.0f);
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, -span, span, true);
    const float va = smoothedCents / 50.0f * span;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (0.0f, va), std::max (0.0f, va), true);
    g.setColour (hud::dim.withAlpha (0.55f));
    g.strokePath (track, juce::PathStrokeType (2.0f));
    if (voiced)
    {
        const bool inTune = std::abs (smoothedCents) < 8.0f;
        g.setColour ((inTune ? hud::accent : hud::warn).withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (9.0f));
        g.setColour (inTune ? hud::accent : hud::warn);
        g.strokePath (val, juce::PathStrokeType (3.2f));
    }
    g.setColour (juce::Colours::white);
    g.drawLine (c.x, c.y - arcR - 7, c.x, c.y - arcR + 7, 1.6f);

    // marco octogonal central
    const float ow = R * 0.50f, oh = R * 0.40f, cut = 14.0f;
    juce::Path oct;
    oct.startNewSubPath (c.x - ow + cut, c.y - oh); oct.lineTo (c.x + ow - cut, c.y - oh);
    oct.lineTo (c.x + ow, c.y - oh + cut);          oct.lineTo (c.x + ow, c.y + oh - cut);
    oct.lineTo (c.x + ow - cut, c.y + oh);          oct.lineTo (c.x - ow + cut, c.y + oh);
    oct.lineTo (c.x - ow, c.y + oh - cut);          oct.lineTo (c.x - ow, c.y - oh + cut);
    oct.closeSubPath();
    g.setColour (Colour (0x400a2038));
    g.fillPath (oct);
    g.setColour (hud::line.withAlpha (0.9f));
    g.strokePath (oct, juce::PathStrokeType (1.6f));

    // texto
    if (voiced)
    {
        g.setColour (juce::Colours::white);
        g.setFont (hud::mono (46.0f, true));
        g.drawText (noteLabel ((int) std::lround (target)), (int) (c.x - ow), (int) (c.y - oh + 6), (int) (2 * ow), 52, juce::Justification::centred);
        g.setFont (hud::mono (12.0f));
        g.setColour (hud::accentHi);
        g.drawText (juce::String ((int) std::lround (smoothedCents)) + " cents", (int) (c.x - ow), (int) (c.y + 14), (int) (2 * ow), 16, juce::Justification::centred);
        g.setColour (hud::dim);
        g.drawText (juce::String (uv::midiToHz (midiIn), 1) + " Hz", (int) (c.x - ow), (int) (c.y + 31), (int) (2 * ow), 16, juce::Justification::centred);
    }
    else
    {
        g.setColour (hud::dim);
        g.setFont (hud::mono (30.0f, true));
        g.drawText ("--", (int) (c.x - ow), (int) (c.y - oh + 6), (int) (2 * ow), 40, juce::Justification::centred);
        g.setFont (hud::mono (11.0f));
        g.drawText ("SIN SENAL VOCAL", (int) (c.x - ow), (int) (c.y + 22), (int) (2 * ow), 16, juce::Justification::centred);
    }
}

// ============================================================================
//  Editor de tono
// ============================================================================
PitchGraph::PitchGraph (UltimateVocalProcessor& p) : proc (p) { startTimerHz (30); }

float PitchGraph::rowTop (float midi, Rectangle<float> r, float lo) const
{
    return r.getBottom() - (midi - lo) / kRangeSt * r.getHeight();
}

void PitchGraph::mouseDown (const juce::MouseEvent& e)
{
    auto r = getLocalBounds().toFloat();
    const float lo = centre - kRangeSt * 0.5f;
    if (e.position.x > r.getX() + 40.0f)
        return;                                    // la edición de notas se hace en la columna de notas
    const float midi = lo + (r.getBottom() - e.position.y) / r.getHeight() * kRangeSt;
    toggleNote (proc, ((int) std::lround (midi) % 12 + 12) % 12);
}

void PitchGraph::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto plot = r.withTrimmedLeft (40.0f);
    g.saveState();
    g.reduceClipRegion (getLocalBounds());

    const uint64_t w = proc.traceWrite.load (std::memory_order_acquire);
    const int count = (int) std::min<uint64_t> (w, (uint64_t) kVisible);
    const int size = UltimateVocalProcessor::kTraceSize;

    // centrado suave sobre el tono reciente
    float sum = 0.0f; int num = 0;
    for (int i = 0; i < std::min (count, 120); ++i)
    {
        const float v = proc.traceOut[(size_t) ((w - 1 - (uint64_t) i) % (uint64_t) size)].load (std::memory_order_relaxed);
        if (! std::isnan (v)) { sum += v; ++num; }
    }
    if (num > 0) centre += (sum / (float) num - centre) * 0.06f;
    const float lo = centre - kRangeSt * 0.5f;

    // filas de notas
    for (int n = (int) std::floor (lo) - 1; n <= (int) std::ceil (lo + kRangeSt) + 1; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const float yT = rowTop ((float) n + 0.5f, r, lo), yB = rowTop ((float) n - 0.5f, r, lo);
        const bool on = noteOn (proc, pc);
        if (isBlack (pc)) { g.setColour (Colour (0x20000000)); g.fillRect (r.getX(), yT, r.getWidth(), yB - yT); }
        if (on) { g.setColour (hud::accent.withAlpha (0.10f)); g.fillRect (r.getX(), yT, r.getWidth(), yB - yT); }
        g.setColour (hud::line.withAlpha (pc == 0 ? 0.28f : 0.08f));
        g.drawHorizontalLine ((int) yB, r.getX(), r.getRight());

        // columna de notas (clic = activar/desactivar)
        g.setColour (on ? hud::accent.withAlpha (0.35f) : Colour (0x18ffffff));
        g.fillRect (r.getX() + 1.0f, yT + 1.0f, 36.0f, std::max (1.0f, yB - yT - 2.0f));
        g.setColour (on ? juce::Colours::white : hud::dim);
        g.setFont (hud::mono (10.0f, pc == 0));
        g.drawText (noteLabel (n), juce::Rectangle<float> (r.getX() + 2.0f, yT, 34.0f, yB - yT), juce::Justification::centred);
    }

    // cuadrícula temporal
    g.setColour (hud::line.withAlpha (0.06f));
    for (int i = 1; i < 10; ++i) g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 10.0f), r.getY(), r.getBottom());

    const float dx = plot.getWidth() / (float) kVisible;
    auto xAt = [&] (int j) { return plot.getRight() - (float) (count - 1 - j) * dx; };
    auto entry = [&] (std::array<std::atomic<float>, UltimateVocalProcessor::kTraceSize>& a, int j)
    {
        return a[(size_t) ((w - (uint64_t) count + (uint64_t) j) % (uint64_t) size)].load (std::memory_order_relaxed);
    };

    // bloques de nota (estilo editor de notas)
    {
        int startJ = -1, curNote = 0;
        auto closeBlob = [&] (int endJ)
        {
            if (startJ < 0) return;
            const float x1 = xAt (startJ), x2 = xAt (endJ);
            if (x2 - x1 > 3.0f)
            {
                const Rectangle<float> blob (x1, rowTop ((float) curNote + 0.42f, r, lo), x2 - x1,
                                             rowTop ((float) curNote - 0.42f, r, lo) - rowTop ((float) curNote + 0.42f, r, lo));
                g.setColour (hud::accent.withAlpha (0.16f));
                g.fillRoundedRectangle (blob, 3.0f);
                g.setColour (hud::accent.withAlpha (0.75f));
                g.drawRoundedRectangle (blob, 3.0f, 1.0f);
            }
            startJ = -1;
        };
        for (int j = 0; j < count; ++j)
        {
            const float t = entry (proc.traceTarget, j);
            if (std::isnan (t)) { closeBlob (j - 1); continue; }
            const int note = (int) std::lround (t);
            if (startJ >= 0 && note != curNote) closeBlob (j - 1);
            if (startJ < 0) { startJ = j; curNote = note; }
        }
        closeBlob (count - 1);
    }

    // trazas
    auto drawTrace = [&] (std::array<std::atomic<float>, UltimateVocalProcessor::kTraceSize>& a, Colour col, float thick, float glowAlpha)
    {
        juce::Path path; bool pen = false;
        for (int j = 0; j < count; ++j)
        {
            const float v = entry (a, j);
            if (std::isnan (v)) { pen = false; continue; }
            const float x = xAt (j), y = rowTop (v, r, lo);
            if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
        }
        if (glowAlpha > 0.0f) { g.setColour (col.withAlpha (glowAlpha)); g.strokePath (path, juce::PathStrokeType (thick + 4.0f)); }
        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    drawTrace (proc.traceIn, juce::Colours::white.withAlpha (0.75f), 1.1f, 0.0f);
    drawTrace (proc.traceOut, hud::accentHi, 2.0f, 0.18f);

    // leyenda
    g.setFont (hud::mono (10.0f));
    const float lx = plot.getRight() - 230.0f, ly = r.getY() + 8.0f;
    g.setColour (juce::Colours::white.withAlpha (0.75f)); g.drawLine (lx, ly + 6, lx + 16, ly + 6, 1.2f);
    g.setColour (hud::dim); g.drawText ("ENTRADA", (int) lx + 20, (int) ly, 60, 12, juce::Justification::left);
    g.setColour (hud::accentHi); g.drawLine (lx + 90, ly + 6, lx + 106, ly + 6, 2.2f);
    g.setColour (hud::dim); g.drawText ("CORREGIDO", (int) lx + 110, (int) ly, 80, 12, juce::Justification::left);

    g.restoreState();
    g.setColour (hud::line.withAlpha (0.25f));
    g.drawRect (r, 1.0f);
}

// ============================================================================
//  Teclado de escala
// ============================================================================
ScaleKeyboard::ScaleKeyboard (UltimateVocalProcessor& p) : proc (p) { startTimerHz (20); }

Rectangle<float> ScaleKeyboard::keyRect (int pc) const
{
    const auto b = getLocalBounds().toFloat();
    const float ww = b.getWidth() / 7.0f;
    static const int whiteIndex[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    static const int blackAfter[12] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };
    if (! isBlack (pc))
        return { b.getX() + (float) whiteIndex[pc] * ww, b.getY(), ww, b.getHeight() };
    const float bw = ww * 0.58f;
    return { b.getX() + ((float) blackAfter[pc] + 1.0f) * ww - bw * 0.5f, b.getY(), bw, b.getHeight() * 0.58f };
}

int ScaleKeyboard::keyAt (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc) && keyRect (pc).contains (p)) return pc;
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc) && keyRect (pc).contains (p)) return pc;
    return -1;
}

void ScaleKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc >= 0) toggleNote (proc, pc);
}

void ScaleKeyboard::paint (Graphics& g)
{
    const bool voiced = proc.liveVoiced.load();
    const int livePc = voiced ? (((int) std::lround (proc.liveMidiTarget.load()) % 12) + 12) % 12 : -1;

    auto drawKey = [&] (int pc)
    {
        const auto r = keyRect (pc).reduced (1.5f);
        const bool on = noteOn (proc, pc), live = pc == livePc;
        g.setColour (on ? hud::accent.withAlpha (isBlack (pc) ? 0.55f : 0.30f) : Colour (isBlack (pc) ? 0xff06121f : 0x18ffffff));
        g.fillRect (r);
        if (live) { g.setColour (hud::accentHi.withAlpha (0.55f)); g.fillRect (r); }
        g.setColour (on ? hud::accent : hud::line.withAlpha (0.4f));
        g.drawRect (r, live ? 2.0f : 1.0f);
        g.setFont (hud::mono (11.0f, on));
        g.setColour (on ? juce::Colours::white : hud::dim);
        g.drawText (ids::noteNames[pc], r.withTrimmedBottom (4.0f), juce::Justification::centredBottom);
    };
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) drawKey (pc);
}

// ============================================================================
//  Medidores
// ============================================================================
void MetersComponent::drawBar (Graphics& g, Rectangle<float> r, float norm, const juce::String& name, bool fromTop)
{
    auto bar = r.withTrimmedBottom (18.0f);
    const int segs = 22;
    const float sh = bar.getHeight() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float f = (float) i / (float) segs;                 // 0 = extremo de referencia
        const float yy = fromTop ? bar.getY() + (float) i * sh : bar.getBottom() - (float) (i + 1) * sh;
        const bool lit = f < norm;
        Colour col = f > 0.9f ? Colour (0xffff4d5e) : (f > 0.72f ? hud::warn : hud::accent);
        if (fromTop) col = hud::warn;
        g.setColour (lit ? col : Colour (0x16ffffff));
        g.fillRect (bar.getX(), yy + 1.0f, bar.getWidth(), sh - 2.0f);
    }
    g.setFont (hud::mono (11.0f));
    g.setColour (hud::dim);
    g.drawText (name, r.withTop (r.getBottom() - 16.0f), juce::Justification::centred);
}

void MetersComponent::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float bw = (r.getWidth() - 20.0f) / 3.0f;
    auto toNorm = [] (float lin) { return juce::jlimit (0.0f, 1.0f, juce::jmap (juce::Decibels::gainToDecibels (lin + 1.0e-6f), -60.0f, 0.0f, 0.0f, 1.0f)); };
    drawBar (g, { r.getX(), r.getY(), bw, r.getHeight() }, toNorm (proc.meterIn.load()), "IN", false);
    drawBar (g, { r.getX() + bw + 10.0f, r.getY(), bw, r.getHeight() }, toNorm (proc.meterOut.load()), "OUT", false);
    drawBar (g, { r.getX() + 2.0f * (bw + 10.0f), r.getY(), bw, r.getHeight() },
             juce::jlimit (0.0f, 1.0f, proc.meterGR.load() / 12.0f), "GR", true);
}

// ============================================================================
//  Editor principal
// ============================================================================
UltimateVocalEditor::UltimateVocalEditor (UltimateVocalProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ring (p), graph (p), keyboard (p), meters (p),
      retune (p.apvts, "retune", "RETUNE", " ms", 0), amount (p.apvts, "amount", "CANTIDAD", " %", 0),
      transpose (p.apvts, "transpose", "TRANSPOSE", " st", 1), fine (p.apvts, "fine", "AJUSTE FINO", " ct", 0),
      formant (p.apvts, "formant", "FORMANTE", " st", 1), mix (p.apvts, "mix", "MEZCLA", " %", 0),
      inGain (p.apvts, "inGain", "ENTRADA", " dB", 1), hpf (p.apvts, "hpf", "LOW CUT", " Hz", 0),
      gate (p.apvts, "gate", "GATE", " dB", 0), compThr (p.apvts, "compThr", "COMP THR", " dB", 0),
      compRatio (p.apvts, "compRatio", "RATIO", ":1", 1), outGain (p.apvts, "outGain", "SALIDA", " dB", 1)
{
    setLookAndFeel (&laf);

    pRing = { 24, 24, 300, 300 };       pGraph = { 340, 24, 816, 300 };
    pTuning = { 24, 340, 470, 150 };    pKeys = { 510, 340, 646, 150 };
    pInput = { 24, 506, 300, 170 };     pVoice = { 340, 506, 300, 170 };
    pDynamics = { 656, 506, 330, 170 }; pMeters = { 1002, 506, 154, 170 };

    keyBox.addItemList (ids::noteNames, 1);
    scaleBox.addItemList (ids::scaleNames, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);
    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bypass", bypassButton);

    for (auto* c : std::initializer_list<juce::Component*> { &ring, &graph, &keyboard, &meters,
                                                            &retune, &amount, &transpose, &fine, &formant, &mix,
                                                            &inGain, &hpf, &gate, &compThr, &compRatio, &outGain })
        addAndMakeVisible (c);

    setSize (kW, kH);   // dispara resized() con los rectángulos ya definidos
}

UltimateVocalEditor::~UltimateVocalEditor() { setLookAndFeel (nullptr); }

void UltimateVocalEditor::resized()
{
    ring.setBounds (pRing.withTrimmedTop (34).reduced (8));
    graph.setBounds (pGraph.withTrimmedTop (40).reduced (14, 0).withTrimmedBottom (14));

    keyBox.setBounds (pTuning.getX() + 16, pTuning.getY() + 40, 70, 24);
    scaleBox.setBounds (pTuning.getX() + 94, pTuning.getY() + 40, 170, 24);
    bypassButton.setBounds (pTuning.getRight() - 106, pTuning.getY() + 40, 90, 24);

    auto knobRow = [] (Rectangle<int> area, std::initializer_list<HudKnob*> knobs)
    {
        const int n = (int) knobs.size();
        const int cw = area.getWidth() / n;
        int i = 0;
        for (auto* k : knobs) k->setBounds (area.getX() + i++ * cw + 4, area.getY(), cw - 8, area.getHeight());
    };
    knobRow ({ pTuning.getX() + 10, pTuning.getY() + 70, pTuning.getWidth() - 20, 76 }, { &retune, &amount, &transpose, &fine });
    keyboard.setBounds (pKeys.withTrimmedTop (42).reduced (16, 0).withTrimmedBottom (14));
    knobRow ({ pInput.getX() + 10, pInput.getY() + 44, pInput.getWidth() - 20, 118 }, { &inGain, &hpf, &gate });
    knobRow ({ pVoice.getX() + 10, pVoice.getY() + 44, pVoice.getWidth() - 20, 118 }, { &formant, &mix });
    knobRow ({ pDynamics.getX() + 10, pDynamics.getY() + 44, pDynamics.getWidth() - 20, 118 }, { &compThr, &compRatio, &outGain });
    meters.setBounds (pMeters.withTrimmedTop (42).reduced (18, 0).withTrimmedBottom (12));
}

void UltimateVocalEditor::drawPanel (Graphics& g, Rectangle<int> b, const juce::String& id, const juce::String& title)
{
    const auto r = b.toFloat();
    g.setColour (Colour (0x180b2238));
    g.fillRect (r);
    g.setColour (hud::line.withAlpha (0.25f));
    g.drawRect (r, 1.0f);

    auto bracket = [&] (juce::Point<float> corner, float sx, float sy)
    {
        juce::Path p;
        p.startNewSubPath (corner.x + sx * 16.0f, corner.y);
        p.lineTo (corner);
        p.lineTo (corner.x, corner.y + sy * 16.0f);
        g.setColour (hud::line.withAlpha (0.95f));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    };
    bracket (r.getTopLeft(), 1, 1);       bracket (r.getTopRight(), -1, 1);
    bracket (r.getBottomLeft(), 1, -1);   bracket (r.getBottomRight(), -1, -1);

    g.setFont (hud::mono (18.0f));
    g.setColour (hud::line);
    g.drawText ("MODULE  " + id, (int) r.getX() + 16, (int) r.getY() + 8, 220, 22, juce::Justification::left);
    g.setFont (hud::mono (11.0f));
    g.setColour (hud::accent);
    g.drawText (title, (int) r.getX() + 190, (int) r.getY() + 12, (int) r.getWidth() - 206, 16, juce::Justification::right);
    g.setColour (hud::line.withAlpha (0.35f));
    g.drawHorizontalLine ((int) r.getY() + 34, r.getX() + 16, r.getRight() - 16);
}

void UltimateVocalEditor::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (hud::bgTop, 0, 0, hud::bgBottom, 0, b.getHeight(), false));
    g.fillAll();

    // resplandor tras el anillo
    g.setGradientFill (juce::ColourGradient (hud::accent.withAlpha (0.20f), (float) pRing.getCentreX(), (float) pRing.getCentreY(),
                                             juce::Colours::transparentBlack, (float) pRing.getCentreX() + 340.0f, (float) pRing.getCentreY(), true));
    g.fillRect (b);

    // rejilla fina
    g.setColour (hud::line.withAlpha (0.035f));
    for (int x = 0; x < kW; x += 20) g.drawVerticalLine (x, 0.0f, (float) kH);
    for (int y = 0; y < kH; y += 20) g.drawHorizontalLine (y, 0.0f, (float) kW);

    // partículas tenues
    juce::Random rnd (7);
    for (int i = 0; i < 90; ++i)
    {
        g.setColour (hud::accentHi.withAlpha (0.05f + 0.12f * rnd.nextFloat()));
        g.fillRect (rnd.nextFloat() * (float) kW, rnd.nextFloat() * (float) kH, 1.5f, 1.5f);
    }

    // marco exterior
    auto frame = b.reduced (7.0f);
    g.setColour (hud::line.withAlpha (0.6f));
    struct Corner { juce::Point<float> p; float sx, sy; };
    const Corner corners[] = { { frame.getTopLeft(), 1.0f, 1.0f }, { frame.getTopRight(), -1.0f, 1.0f },
                               { frame.getBottomLeft(), 1.0f, -1.0f }, { frame.getBottomRight(), -1.0f, -1.0f } };
    for (const auto& c : corners)
    {
        juce::Path p;
        p.startNewSubPath (c.p.x + c.sx * 46.0f, c.p.y);
        p.lineTo (c.p);
        p.lineTo (c.p.x, c.p.y + c.sy * 46.0f);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

    drawPanel (g, pRing,     "100", "NOTA ACTUAL");
    drawPanel (g, pGraph,    "200", "EDITOR DE TONO  /  CLIC EN LA COLUMNA DE NOTAS = ACTIVAR");
    drawPanel (g, pTuning,   "300", "AFINACION");
    drawPanel (g, pKeys,     "400", "ESCALA  /  CLIC PARA ACTIVAR NOTAS");
    drawPanel (g, pInput,    "500", "ENTRADA");
    drawPanel (g, pVoice,    "600", "VOZ");
    drawPanel (g, pDynamics, "700", "DINAMICA / SALIDA");
    drawPanel (g, pMeters,   "800", "");

    g.setFont (hud::mono (11.0f));
    g.setColour (hud::dim);
    g.drawText ("ULTIMATE VOCAL  v1.0   |   LATENCIA " + juce::String (proc.getLatencySamples()) + " MUESTRAS",
                (int) b.getX() + 24, kH - 22, 520, 14, juce::Justification::left);
}
