#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "ShaderEngine.h"

// GLSL で描いたスペクトログラムを鳴らす音源。ページ (1〜8 小節) を先に全部描いて 1 周分の音を作り、
// ホストの再生位置に合わせてループする。描き直しは裏のスレッド (ShaderEngine) がやる。
class SpecShaderVSTAudioProcessor  : public juce::AudioProcessor,
                                     private juce::Timer,
                                     private juce::ValueTree::Listener
{
public:
    SpecShaderVSTAudioProcessor();
    ~SpecShaderVSTAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SpecShader"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // コードと縦軸の設定 (オートメーションしないので state のプロパティに持つ。メッセージスレッドから触る)
    juce::String getCode() const;
    void setCode (const juce::String& code);
    void setSetting (const juce::Identifier& id, const juce::var& v);
    juce::var getSetting (const juce::Identifier& id) const;

    // UI 向け
    ShaderEngine& getEngine() { return *engine; }
    std::atomic<float> playPhase { -1.0f };   // いま鳴っているページ上の位置 0〜1 (止まっているときは -1)
    std::atomic<float> meterOut { 0.0f };
    std::atomic<double> hostBpm { 120.0 };

    // 検証用: true にすると、設定が変わったときに processBlock / prepareToPlay の中で描き終わるまで待つ
    static inline bool syncRenderForTests = false;

    static const juce::Identifier idCode, idRows, idFps, idScale, idFMin, idFMax;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    PageSettings currentSettings (double bpm) const;
    void publishPage (std::unique_ptr<PageAudio> page);
    void drainRetired();
    float sampleAt (const PageAudio& p, double ppq) const;

    std::unique_ptr<ShaderEngine> engine;

    // エンジン → オーディオの受け渡し (ロックなし)。古いページはオーディオ側から retire に積み、エンジン側で消す
    std::atomic<PageAudio*> pending { nullptr };
    juce::AbstractFifo retireFifo { 32 };
    PageAudio* retireSlots[32] {};
    PageAudio* current = nullptr;
    PageAudio* fading = nullptr;
    int fadePos = 0, fadeLen = 960;
    void retire (PageAudio* p);

    double sr = 48000.0;
    double freePpq = 0.0;
    double lastBpm = 120.0;
    juce::SmoothedValue<float> gain, runEnv, clipMix;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pBars = nullptr;
    std::atomic<float>* pPlay = nullptr;
    std::atomic<float>* pBypass = nullptr;

    // 描き直しの判断 (メッセージスレッドのタイマー)
    double bpmSeenSince = 0.0, bpmSeen = 0.0;
    PageSettings lastRequested;
    bool requestedOnce = false;
    std::atomic<bool> settingsDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpecShaderVSTAudioProcessor)
};
