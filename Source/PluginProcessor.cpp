#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Examples.h"

const juce::Identifier SpecShaderVSTAudioProcessor::idCode  { "code" };
const juce::Identifier SpecShaderVSTAudioProcessor::idRows  { "rows" };
const juce::Identifier SpecShaderVSTAudioProcessor::idFps   { "fps" };
const juce::Identifier SpecShaderVSTAudioProcessor::idScale { "logScale" };
const juce::Identifier SpecShaderVSTAudioProcessor::idFMin  { "fMin" };
const juce::Identifier SpecShaderVSTAudioProcessor::idFMax  { "fMax" };

namespace
{
    juce::String dbText (float v, int) { return juce::String::formatted ("%+.1f dB", (double) v); }
    const int barsChoices[] = { 1, 2, 4, 8 };
    double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
}

SpecShaderVSTAudioProcessor::SpecShaderVSTAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpecShader", createLayout())
{
    engine = std::make_unique<ShaderEngine> ([this] (std::unique_ptr<PageAudio> p) { publishPage (std::move (p)); });
    pGain   = apvts.getRawParameterValue ("gain");
    pClip   = apvts.getRawParameterValue ("softclip");
    pBars   = apvts.getRawParameterValue ("bars");
    pPlay   = apvts.getRawParameterValue ("play");
    pBypass = apvts.getRawParameterValue ("bypass");

    auto& st = apvts.state;
    st.setProperty (idCode, juce::String::fromUTF8 (shaderExamples[0].code), nullptr);
    st.setProperty (idRows, 433, nullptr);
    st.setProperty (idFps, 60.0, nullptr);
    st.setProperty (idScale, true, nullptr);
    st.setProperty (idFMin, std::round (midiHz (24) * 1000.0) / 1000.0, nullptr);
    st.setProperty (idFMax, std::round (midiHz (132) * 100.0) / 100.0, nullptr);
    st.addListener (this);
    startTimerHz (20);
}

SpecShaderVSTAudioProcessor::~SpecShaderVSTAudioProcessor()
{
    stopTimer();
    apvts.state.removeListener (this);
    // エンジンのスレッドを止めてから受け渡し中のページを消す
    engine.reset();
    delete pending.exchange (nullptr);
    drainRetired();
    delete current; current = nullptr;
    delete fading;  fading = nullptr;
}

juce::AudioProcessorValueTreeState::ParameterLayout SpecShaderVSTAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                                                       NormalisableRange<float> (-48.0f, 12.0f, 0.1f), -6.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "softclip", 1 }, "Soft Clip", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "bars", 1 }, "Page", StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "play", 1 }, "Play", StringArray { "Host", "Always" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

bool SpecShaderVSTAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

//==============================================================================
juce::String SpecShaderVSTAudioProcessor::getCode() const { return apvts.state.getProperty (idCode).toString(); }
void SpecShaderVSTAudioProcessor::setCode (const juce::String& code) { apvts.state.setProperty (idCode, code, nullptr); }
void SpecShaderVSTAudioProcessor::setSetting (const juce::Identifier& id, const juce::var& v) { apvts.state.setProperty (id, v, nullptr); }
juce::var SpecShaderVSTAudioProcessor::getSetting (const juce::Identifier& id) const { return apvts.state.getProperty (id); }

void SpecShaderVSTAudioProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    settingsDirty = true;
}

PageSettings SpecShaderVSTAudioProcessor::currentSettings (double bpm) const
{
    PageSettings s;
    const auto& st = apvts.state;
    s.code = st.getProperty (idCode).toString();
    s.rows = juce::jlimit (16, 2048, (int) st.getProperty (idRows, 433));
    s.fps = juce::jlimit (10.0, 480.0, (double) st.getProperty (idFps, 60.0));
    s.logScale = (bool) st.getProperty (idScale, true);
    s.fMin = (double) st.getProperty (idFMin, 32.703);
    s.fMax = (double) st.getProperty (idFMax, 16744.04);
    s.bars = barsChoices[juce::jlimit (0, 3, (int) std::lround (pBars->load()))];
    s.bpm = std::round (juce::jlimit (20.0, 999.0, bpm) * 1000.0) / 1000.0;
    s.sampleRate = sr;
    return s;
}

// 設定・コード・テンポが変わったら描き直しを頼む。テンポは 0.15 秒落ち着いてから (オートメーション中に連打しない)
void SpecShaderVSTAudioProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double bpm = hostBpm.load();
    if (bpm != bpmSeen) { bpmSeen = bpm; bpmSeenSince = now; }
    const bool bpmSettled = now - bpmSeenSince > 0.15 || ! requestedOnce;

    auto s = currentSettings (bpmSettled ? bpm : lastRequested.bpm);
    if (! requestedOnce || s != lastRequested)
    {
        lastRequested = s;
        requestedOnce = true;
        engine->request (s);
    }
    settingsDirty = false;
}

//==============================================================================
void SpecShaderVSTAudioProcessor::publishPage (std::unique_ptr<PageAudio> page)
{
    drainRetired();
    delete pending.exchange (page.release());
}

void SpecShaderVSTAudioProcessor::drainRetired()
{
    const auto scope = retireFifo.read (retireFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) delete retireSlots[scope.startIndex1 + i];
    for (int i = 0; i < scope.blockSize2; ++i) delete retireSlots[scope.startIndex2 + i];
}

void SpecShaderVSTAudioProcessor::retire (PageAudio* p)
{
    if (p == nullptr) return;
    const auto scope = retireFifo.write (1);
    if (scope.blockSize1 > 0) retireSlots[scope.startIndex1] = p;
    else if (scope.blockSize2 > 0) retireSlots[scope.startIndex2] = p;
    // 満杯は起きない設計 (エンジンが毎回空にする)。万一のときは捨てずに漏らす方を選ぶ (オーディオで delete しない)
}

void SpecShaderVSTAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    gain.reset (sampleRate, 0.03);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    runEnv.reset (sampleRate, 0.005);
    runEnv.setCurrentAndTargetValue (0.0f);
    clipMix.reset (sampleRate, 0.01);
    clipMix.setCurrentAndTargetValue (pClip->load() > 0.5f ? 1.0f : 0.0f);
    fadeLen = juce::jmax (1, (int) std::lround (0.02 * sampleRate));
    freePpq = 0.0;

    if (syncRenderForTests)
    {
        engine->renderBlocking (currentSettings (lastBpm));
        if (auto* p = pending.exchange (nullptr)) { retire (current); current = p; }
        retire (fading); fading = nullptr;
        drainRetired();
    }
}

float SpecShaderVSTAudioProcessor::sampleAt (const PageAudio& p, double ppq) const
{
    const int n = (int) p.data.size();
    if (n == 0) return 0.0f;
    const double beats = p.bars * 4.0;
    double ph = ppq / beats;
    ph -= std::floor (ph);
    const double x = ph * n;
    int i0 = (int) x;
    if (i0 >= n) i0 = 0;
    const int i1 = i0 + 1 < n ? i0 + 1 : 0;
    const float f = (float) (x - i0);
    return p.data[(size_t) i0] + (p.data[(size_t) i1] - p.data[(size_t) i0]) * f;
}

void SpecShaderVSTAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();

    // 再生位置とテンポ
    bool hostPlaying = false;
    double bpm = lastBpm, ppq = freePpq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (hostPlaying)
                if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    lastBpm = bpm;
    hostBpm.store (bpm);

    if (syncRenderForTests)
    {
        engine->renderBlocking (currentSettings (bpm));
    }

    // 新しいページが届いていたら、20 ms で前のページからつなぐ
    if (auto* p = pending.exchange (nullptr))
    {
        if (current == nullptr) current = p;
        else
        {
            retire (fading);
            fading = current;
            current = p;
            fadePos = 0;
        }
    }

    const bool always = pPlay->load() > 0.5f;
    const bool running = (hostPlaying || always) && pBypass->load() < 0.5f;
    runEnv.setTargetValue (running ? 1.0f : 0.0f);
    gain.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    clipMix.setTargetValue (pClip->load() > 0.5f ? 1.0f : 0.0f);

    const double ppqPerSample = bpm / 60.0 / sr;
    float pk = 0.0f;
    auto* out0 = buffer.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        const double q = ppq + i * ppqPerSample;
        float v = current != nullptr ? sampleAt (*current, q) : 0.0f;
        if (fading != nullptr)
        {
            const float w = (float) fadePos / (float) fadeLen;
            v = sampleAt (*fading, q) * (1.0f - w) + v * w;
            if (++fadePos >= fadeLen) { retire (fading); fading = nullptr; }
        }
        v *= runEnv.getNextValue() * gain.getNextValue();
        const float c = clipMix.getNextValue();
        v = v + (std::tanh (v) - v) * c;
        if (! std::isfinite (v)) v = 0.0f;
        out0[i] = v;
        pk = juce::jmax (pk, std::abs (v));
    }
    for (int ch = 1; ch < nch; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    // 止まっているとき (Always) は自前の位置で進む。再生中はホストの位置を覚えておき、止めたらそこから続ける
    freePpq = ppq + n * ppqPerSample;

    if (current != nullptr && running)
    {
        double ph = ppq / (current->bars * 4.0);
        playPhase.store ((float) (ph - std::floor (ph)));
    }
    else
        playPhase.store (-1.0f);
    meterOut.store (juce::jmax (meterOut.load(), pk));
}

//==============================================================================
juce::AudioProcessorEditor* SpecShaderVSTAudioProcessor::createEditor()
{
    return new SpecShaderVSTAudioProcessorEditor (*this);
}

void SpecShaderVSTAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpecShaderVSTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.state.removeListener (this);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            apvts.state.addListener (this);
            settingsDirty = true;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpecShaderVSTAudioProcessor();
}
