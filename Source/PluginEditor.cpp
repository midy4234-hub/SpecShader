#include "PluginEditor.h"
#include "Examples.h"

namespace
{
    const char* helpText =
        "Each frame the shader draws an image: x = time (one pixel = one sample), y = frequency (one row = one sine).\n"
        "fragColor.r = amplitude, fragColor.g = phase added to that row (1.0 = one cycle).\n"
        "The whole page (Page bars) is drawn first, then played in a loop in sync with the host.\n"
        "\n"
        "per pixel : t (s, 0 at page start)  tl (s from frame start)  bt (beats, wraps at 64)\n"
        "            hz (row frequency)  row  uv  pos (where on the page, 0..1)\n"
        "uniforms  : time resolution frame sampleRate frameRate bpm beat pageLength fMin fMax logScale backbuffer\n"
        "functions : tone(f, amp)  sweep(f, cycles, amp)  rg(sum)  phase(f)  rowPhase()\n"
        "            midiHz(n) hzMidi(f) rowHz(y) hzY(f) hash(x) hash(vec2) TAU\n"
        "\n"
        "Sum tone()/sweep() into a vec2 and finish with fragColor = rg(sum).\n"
        "Use phase(f) or bt for anything that oscillates; sin(TAU * f * t) gets noisy as t grows.\n"
        "Cmd/Ctrl + Enter compiles now.";

    const juce::Colour codeBg { 0xff1f1f1f };

    juce::CodeEditorComponent::ColourScheme makeScheme()
    {
        juce::CodeEditorComponent::ColourScheme cs;
        cs.set ("Error",        juce::Colour (0xffff6b6b));
        cs.set ("Comment",      juce::Colour (0xff6f7787));
        cs.set ("Keyword",      juce::Colour (0xffff8fa3));
        cs.set ("Operator",     juce::Colour (0xffd2d2d2));
        cs.set ("Identifier",   juce::Colour (0xffd2d2d2));
        cs.set ("Integer",      juce::Colour (0xffffa400));
        cs.set ("Float",        juce::Colour (0xffffa400));
        cs.set ("String",       juce::Colour (0xff9be59b));
        cs.set ("Bracket",      juce::Colour (0xffd2d2d2));
        cs.set ("Punctuation",  juce::Colour (0xffd2d2d2));
        cs.set ("Preprocessor Text", juce::Colour (0xffc3a6ff));
        return cs;
    }

    juce::String noteName (int midi) { return "C" + juce::String (midi / 12 - 1); }
}

//==============================================================================
void PageView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (lab::col::lcd);
    g.fillRect (r);

    auto& eng = proc.getEngine();
    const auto st = eng.getStatus();
    const auto pic = eng.getPicture();
    const auto rf = r.toFloat();

    if (pic.image.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (pic.image, rf);

        // 小節と拍の線
        const int beats = pic.bars * 4;
        for (int b = 1; b < beats; ++b)
        {
            const float x = rf.getX() + rf.getWidth() * (float) b / (float) beats;
            g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.16f : 0.05f));
            g.drawVerticalLine ((int) x, rf.getY(), rf.getBottom());
        }

        // 縦軸の目盛り (対数なら各オクターブの C、線形なら Hz)
        g.setFont (juce::FontOptions (10.0f));
        auto yOf = [&] (double f)
        {
            const double n = pic.logScale ? std::log (f / pic.fMin) / std::log (pic.fMax / pic.fMin)
                                          : (f - pic.fMin) / (pic.fMax - pic.fMin);
            return rf.getBottom() - (float) n * rf.getHeight();
        };
        auto mark = [&] (double f, const juce::String& label)
        {
            if (f < pic.fMin || f > pic.fMax) return;
            const float y = yOf (f);
            if (y < rf.getY() + 8 || y > rf.getBottom() - 8) return;
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine ((int) y, rf.getX(), rf.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawText (label, (int) rf.getX() + 5, (int) y - 12, 60, 12, juce::Justification::bottomLeft);
        };
        if (pic.logScale)
            for (int m = 12; m <= 132; m += 12) mark (440.0 * std::pow (2.0, (m - 69) / 12.0), noteName (m));
        else
        {
            const double span = pic.fMax - pic.fMin;
            double step = 5000;
            for (double s : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 }) if (span / s <= 8) { step = s; break; }
            for (double f = std::ceil (pic.fMin / step) * step; f <= pic.fMax; f += step)
                mark (f, f >= 1000 ? juce::String (f / 1000.0) + "k" : juce::String ((int) f));
        }
    }

    // 今鳴っている位置
    const float ph = proc.playPhase.load();
    if (ph >= 0.0f)
    {
        const float x = rf.getX() + rf.getWidth() * ph;
        g.setColour (lab::col::accent);
        g.fillRect (x - 1.0f, rf.getY(), 2.0f, rf.getHeight());
    }

    // 状態
    juce::String msg;
    if (! st.glReady && st.glError.isNotEmpty()) msg = "OpenGL: " + st.glError;
    else if (st.busy) msg = "Rendering " + juce::String (juce::roundToInt (st.progress * 100.0f)) + " %";
    if (msg.isNotEmpty())
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const auto box = juce::Rectangle<int> (r.getRight() - 230, r.getY() + 6, 224, 20);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (st.glReady ? lab::col::accent : lab::col::over);
        g.drawText (msg, box.reduced (6, 0), juce::Justification::centredRight);
    }
}

//==============================================================================
SpecShaderVSTAudioProcessorEditor::SpecShaderVSTAudioProcessorEditor (SpecShaderVSTAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    look.setColour (juce::ScrollBar::thumbColourId, lab::col::track);
    setLookAndFeel (&look);

    for (int i = 0; i < (int) std::size (shaderExamples); ++i)
        examples.addItem (shaderExamples[i].name, i + 1);
    examples.setTextWhenNothingSelected ("Examples");
    examples.onChange = [this]
    {
        const int i = examples.getSelectedId() - 1;
        if (i >= 0)
        {
            doc.replaceAllContent (juce::String::fromUTF8 (shaderExamples[i].code));
            commitCode();
        }
    };
    addAndMakeVisible (examples);

    compileBtn.onClick = [this] { commitCode(); };
    addAndMakeVisible (compileBtn);
    autoBtn.setClickingTogglesState (true);
    autoBtn.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (autoBtn);
    helpBtn.setClickingTogglesState (true);
    helpBtn.onClick = [this] { showingHelp = helpBtn.getToggleState(); shownLog = {}; timerCallback(); };
    addAndMakeVisible (helpBtn);

    gain.setUp (*this, proc.apvts, "gain");
    softclip.setUp (*this, proc.apvts, "softclip", "Soft clip");
    page.setUp (*this, proc.apvts, "bars", { { "1", 0 }, { "2", 1 }, { "4", 2 }, { "8", 3 } });
    play.setUp (*this, proc.apvts, "play", { { "Host", 0 }, { "Always", 1 } });

    doc.replaceAllContent (proc.getCode());
    doc.clearUndoHistory();
    doc.addListener (this);
    code = std::make_unique<ShaderCodeEditor> (doc, &tokeniser);
    code->setColourScheme (makeScheme());
    code->setColour (juce::CodeEditorComponent::backgroundColourId, codeBg);
    code->setColour (juce::CodeEditorComponent::defaultTextColourId, lab::col::text);
    code->setColour (juce::CodeEditorComponent::lineNumberBackgroundId, codeBg);
    code->setColour (juce::CodeEditorComponent::lineNumberTextId, juce::Colour (0xff5a5a5a));
    code->setColour (juce::CodeEditorComponent::highlightColourId, lab::col::accent.withAlpha (0.25f));
    code->setColour (juce::CaretComponent::caretColourId, lab::col::accent);
    code->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    code->setTabSize (4, true);
    code->onCommit = [this] { commitCode(); };
    addAndMakeVisible (*code);

    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setScrollbarsShown (true);
    log.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
    log.setColour (juce::TextEditor::backgroundColourId, codeBg);
    log.setColour (juce::TextEditor::outlineColourId, lab::col::panelStroke);
    addAndMakeVisible (log);

    addAndMakeVisible (view);

    setupNumber (rowsL, SpecShaderVSTAudioProcessor::idRows);
    setupNumber (fpsL,  SpecShaderVSTAudioProcessor::idFps);
    setupNumber (fminL, SpecShaderVSTAudioProcessor::idFMin);
    setupNumber (fmaxL, SpecShaderVSTAudioProcessor::idFMax);
    for (auto* b : { &logBtn, &linBtn })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    logBtn.onClick = [this] { proc.setSetting (SpecShaderVSTAudioProcessor::idScale, true);  refreshSettings(); };
    linBtn.onClick = [this] { proc.setSetting (SpecShaderVSTAudioProcessor::idScale, false); refreshSettings(); };
    gridBtn.onClick = [this]
    {
        proc.setSetting (SpecShaderVSTAudioProcessor::idRows, 433);
        proc.setSetting (SpecShaderVSTAudioProcessor::idScale, true);
        proc.setSetting (SpecShaderVSTAudioProcessor::idFMin, 32.703);
        proc.setSetting (SpecShaderVSTAudioProcessor::idFMax, 16744.04);
        refreshSettings();
    };
    addAndMakeVisible (gridBtn);
    refreshSettings();

    setResizable (true, true);
    setResizeLimits (900, 560, 2600, 1600);
    setSize (1180, 700);
    startTimerHz (30);
}

SpecShaderVSTAudioProcessorEditor::~SpecShaderVSTAudioProcessorEditor()
{
    doc.removeListener (this);
    setLookAndFeel (nullptr);
}

void SpecShaderVSTAudioProcessorEditor::setupNumber (juce::Label& l, const juce::Identifier& id)
{
    l.setEditable (true);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::backgroundColourId, lab::col::switchOff);
    l.setColour (juce::Label::outlineColourId, lab::col::panelStroke);
    l.setColour (juce::Label::textColourId, lab::col::text);
    l.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    l.onTextChange = [this, &l, id]
    {
        const double v = l.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v > 0.0)
        {
            if (id == SpecShaderVSTAudioProcessor::idRows) proc.setSetting (id, juce::jlimit (16, 2048, (int) std::lround (v)));
            else if (id == SpecShaderVSTAudioProcessor::idFps) proc.setSetting (id, juce::jlimit (10.0, 480.0, v));
            else proc.setSetting (id, juce::jlimit (1.0, 24000.0, v));
        }
        refreshSettings();
    };
    addAndMakeVisible (l);
}

void SpecShaderVSTAudioProcessorEditor::refreshSettings()
{
    using P = SpecShaderVSTAudioProcessor;
    rowsL.setText (juce::String ((int) proc.getSetting (P::idRows)), juce::dontSendNotification);
    fpsL.setText (juce::String ((double) proc.getSetting (P::idFps), 1), juce::dontSendNotification);
    fminL.setText (juce::String ((double) proc.getSetting (P::idFMin), 1), juce::dontSendNotification);
    fmaxL.setText (juce::String ((double) proc.getSetting (P::idFMax), 0), juce::dontSendNotification);
    const bool isLog = (bool) proc.getSetting (P::idScale);
    logBtn.setToggleState (isLog, juce::dontSendNotification);
    linBtn.setToggleState (! isLog, juce::dontSendNotification);
}

void SpecShaderVSTAudioProcessorEditor::codeChanged()
{
    if (autoBtn.getToggleState())
        pendingCommitTicks = 12;   // 0.4 秒 (30 Hz) 打つ手が止まったら反映
}

void SpecShaderVSTAudioProcessorEditor::commitCode()
{
    pendingCommitTicks = -1;
    proc.setCode (doc.getAllContent());
}

void SpecShaderVSTAudioProcessorEditor::timerCallback()
{
    if (pendingCommitTicks > 0 && --pendingCommitTicks == 0)
        commitCode();

    const auto st = proc.getEngine().getStatus();
    juce::String text = showingHelp ? juce::String (helpText) : st.log;
    if (! showingHelp && st.renders > 0 && st.compileOk)
        text = "OK  -  page rendered in " + juce::String (st.lastRenderSeconds, 2) + " s";
    if (! showingHelp && ! st.glReady && st.glError.isNotEmpty())
        text = "OpenGL: " + st.glError;
    if (text != shownLog)
    {
        shownLog = text;
        log.setColour (juce::TextEditor::textColourId,
                       showingHelp ? lab::col::text : st.compileOk && st.glReady ? lab::col::meter : lab::col::over);
        log.setText (text, false);
    }
    view.repaint();
}

void SpecShaderVSTAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (lab::col::bg);
    lab::drawTitle (g, "SpecShader");

    g.setColour (lab::col::dim);
    g.setFont (juce::FontOptions (10.5f));
    auto label = [&] (juce::Component& c, const juce::String& t, int gap = 64)
    {
        g.drawText (t, c.getX() - gap, c.getY(), gap - 5, c.getHeight(), juce::Justification::centredRight);
    };
    label (gain.slider, "Gain");
    label (*page.buttons[0], "Page bars");
    label (*play.buttons[0], "Play");
    label (rowsL, "Rows", 38);
    label (fpsL, "FPS", 34);
    label (fminL, "Min Hz", 48);
    label (fmaxL, "Max Hz", 50);
    label (logBtn, "Scale", 40);
}

void SpecShaderVSTAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    auto top = r.removeFromTop (40).withTrimmedTop (8).withTrimmedBottom (6);
    top.removeFromLeft (100);   // タイトル
    examples.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (8);
    compileBtn.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (4);
    autoBtn.setBounds (top.removeFromLeft (48));
    top.removeFromLeft (4);
    helpBtn.setBounds (top.removeFromLeft (48));

    softclip.button.setBounds (top.removeFromRight (74));
    top.removeFromRight (78);
    gain.slider.setBounds (top.removeFromRight (70));
    top.removeFromRight (78);
    play.setBounds (top.removeFromRight (110));
    top.removeFromRight (78);
    page.setBounds (top.removeFromRight (110));

    r.removeFromBottom (12);
    auto left = r.removeFromLeft (r.getWidth() * 46 / 100);
    r.removeFromLeft (10);
    log.setBounds (left.removeFromBottom (96));
    left.removeFromBottom (6);
    code->setBounds (left);

    auto settings = r.removeFromBottom (26);
    r.removeFromBottom (8);
    view.setBounds (r);

    auto place = [&] (juce::Component& c, int gap, int w) { settings.removeFromLeft (gap); c.setBounds (settings.removeFromLeft (w)); settings.removeFromLeft (6); };
    place (rowsL, 38, 48);
    place (fpsL, 34, 44);
    place (fminL, 48, 56);
    place (fmaxL, 50, 60);
    settings.removeFromLeft (40);
    logBtn.setBounds (settings.removeFromLeft (38));
    linBtn.setBounds (settings.removeFromLeft (38));
    gridBtn.setBounds (settings.removeFromRight (96));
}
