#pragma once

// GLSL でページ (1 周分) を描いて、加算合成で 1 周分の音を作る裏方。
// 自前のスレッドが画面に出ない OpenGL コンテキストを持つので、エディタが閉じていても描き直せる。
// 仕組みは Web 版 (../SpecShader) と同じ: 1 フレーム = sr/fps サンプル × 行数、各行は固定周波数のサイン波。

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <memory>
#include <vector>
#include "OffscreenGL.h"

struct PageSettings
{
    juce::String code;
    int rows = 433;
    double fps = 60.0;
    bool logScale = true;
    double fMin = 32.703, fMax = 16744.04;
    int bars = 4;
    double bpm = 120.0;
    double sampleRate = 48000.0;

    bool sameExceptCode (const PageSettings& o) const
    {
        return rows == o.rows && fps == o.fps && logScale == o.logScale && fMin == o.fMin && fMax == o.fMax
            && bars == o.bars && bpm == o.bpm && sampleRate == o.sampleRate;
    }
    bool operator== (const PageSettings& o) const { return code == o.code && sameExceptCode (o); }
    bool operator!= (const PageSettings& o) const { return ! (*this == o); }
};

// 1 周分の音。bars 小節 (4 分音符 bars*4 個) ぶんを bpm・sampleRate で描いたもの
struct PageAudio
{
    std::vector<float> data;
    double bpm = 120.0, sampleRate = 48000.0;
    int bars = 4;
};

class ShaderEngine  : private juce::Thread
{
public:
    using Publish = std::function<void (std::unique_ptr<PageAudio>)>;
    explicit ShaderEngine (Publish publishFn);
    ~ShaderEngine() override;

    // 描き直しを頼む (オーディオスレッド以外から)。同じ内容なら何もしない
    void request (const PageSettings& s);
    // 頼んで、描き終わるまで待つ (検証用)。成功なら true
    bool renderBlocking (const PageSettings& s, int timeoutMs = 20000);

    struct Status
    {
        bool glReady = false;
        juce::String glError;
        bool compileOk = true;
        juce::String log;          // コンパイルの結果 (行番号はユーザーのコードの行)
        juce::Array<int> errorLines;
        bool busy = false;
        float progress = 0.0f;
        double lastRenderSeconds = 0.0;
        int renders = 0;
    };
    Status getStatus() const;

    // 表示用の画像 (横 = ページ、縦 = 行。上が高域) と、その画像を描いたときの縦軸
    struct Picture
    {
        juce::Image image;
        int rows = 0, bars = 4;
        bool logScale = true;
        double fMin = 0, fMax = 0;
    };
    Picture getPicture() const;

    static constexpr int historyColumns = 1024;

private:
    void run() override;
    bool renderPage (const PageSettings& s, juce::uint64 gen);
    void setStatus (std::function<void (Status&)> fn);

    Publish publish;
    OffscreenGL gl;

    mutable juce::CriticalSection lock;
    PageSettings pending;
    juce::uint64 pendingGen = 0, doneGen = 0;
    juce::WaitableEvent wake, finished;
    Status status;
    Picture picture;

    struct GL;
    std::unique_ptr<GL> g;   // GL のオブジェクト (エンジンのスレッドだけが触る)
};
