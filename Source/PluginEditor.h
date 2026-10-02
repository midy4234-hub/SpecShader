#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "LabLook.h"

// ページ全体の絵 + 今鳴っている位置のバー
class PageView  : public juce::Component
{
public:
    explicit PageView (SpecShaderVSTAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    SpecShaderVSTAudioProcessor& proc;
};

// ⌘/Ctrl+Enter で即コンパイルできるコードエディタ
class ShaderCodeEditor  : public juce::CodeEditorComponent
{
public:
    ShaderCodeEditor (juce::CodeDocument& d, juce::CodeTokeniser* t) : CodeEditorComponent (d, t) {}
    std::function<void()> onCommit;
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::returnKey && k.getModifiers().isCommandDown())
        {
            if (onCommit) onCommit();
            return true;
        }
        return CodeEditorComponent::keyPressed (k);
    }
};

class SpecShaderVSTAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                           private juce::Timer,
                                           private juce::CodeDocument::Listener
{
public:
    explicit SpecShaderVSTAudioProcessorEditor (SpecShaderVSTAudioProcessor&);
    ~SpecShaderVSTAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void codeDocumentTextInserted (const juce::String&, int) override { codeChanged(); }
    void codeDocumentTextDeleted (int, int) override { codeChanged(); }
    void codeChanged();
    void commitCode();
    void setupNumber (juce::Label& l, const juce::Identifier& id);
    void refreshSettings();

    SpecShaderVSTAudioProcessor& proc;
    lab::Look look;

    juce::ComboBox examples;
    juce::TextButton compileBtn { "Compile" }, autoBtn { "Auto" }, helpBtn { "Help" };
    lab::NumberDrag gain;
    lab::Toggle softclip;
    lab::Switch page, play;

    juce::CodeDocument doc;
    juce::CPlusPlusCodeTokeniser tokeniser;
    std::unique_ptr<ShaderCodeEditor> code;
    juce::TextEditor log;
    PageView view;

    juce::Label rowsL, fpsL, fminL, fmaxL;
    juce::TextButton logBtn { "Log" }, linBtn { "Lin" }, gridBtn { "C1-C10 grid" };

    juce::String shownLog;
    bool showingHelp = false, settingDoc = false;
    int pendingCommitTicks = -1;
    int lastRenders = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpecShaderVSTAudioProcessorEditor)
};
