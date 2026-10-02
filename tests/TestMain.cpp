//  SpecShaderVSTTest — DAW を開かずに挙動を確かめるオフラインドライバ
//
//   SpecShaderVSTTest                 … 下の固有テストを走らせ、UI を renders/ui.png に撮る
//   SpecShaderVSTTest robust          … 標準の耐久テスト (Play = Always、描き直しは同期)
//   SpecShaderVSTTest snapshot out.png
//   SpecShaderVSTTest page <name> out.wav   … サンプル <name> の 1 周を 2 回ループして書く

#include "LabTest.h"
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Examples.h"

using Proc = SpecShaderVSTAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    struct Rig
    {
        Proc p;
        lab::SimPlayHead head;
        explicit Rig (double bpm = 120.0, bool always = true)
        {
            head.bpm = bpm;
            head.sampleRate = sr;
            p.setPlayHead (&head);
            lab::prepare (p, sr);
            lab::setParam (p, "play", always ? 1.0f : 0.0f);
            lab::setParam (p, "gain", 0.0f);
            lab::setParam (p, "softclip", 0.0f);
        }
        juce::AudioBuffer<float> run (double seconds, bool playing = true, double ppqStart = 0.0)
        {
            head.playing = playing;
            head.ppqAtZero = ppqStart;
            lab::RunOptions o;
            o.playhead = &head;
            juce::AudioBuffer<float> silence (2, (int) (seconds * sr));
            silence.clear();
            return lab::runWith (p, silence, o);
        }
        void code (const juce::String& c) { p.setCode (c); }
    };

    double zeroCrossFreq (const juce::AudioBuffer<float>& b, int start, int len)
    {
        const float* x = b.getReadPointer (0);
        double first = -1, last = -1; int n = 0;
        for (int i = start + 1; i < start + len; ++i)
            if (x[i - 1] < 0 && x[i] >= 0)
            {
                const double f = i - 1 + (-x[i - 1]) / (x[i] - x[i - 1]);
                if (first < 0) first = f;
                last = f; ++n;
            }
        return n > 1 ? (n - 1) / ((last - first) / sr) : 0.0;
    }

    int fails = 0;
    void check (bool ok, const char* what, const juce::String& detail)
    {
        std::printf ("  %s  %s  %s\n", ok ? "PASS" : "FAIL", what, detail.toRawUTF8());
        if (! ok) ++fails;
    }

    void testGlAndPage()
    {
        std::printf ("[1] OpenGL and the first page\n");
        Rig r (120.0);
        r.run (0.01);
        const auto st = r.p.getEngine().getStatus();
        check (st.glReady, "GL context", st.glReady ? "ready" : st.glError);
        check (st.compileOk, "default example compiles", st.log);
        auto out = r.run (8.0);
        check (lab::allFinite (out), "finite", {});
        check (lab::rms (out) > 0.01, "makes sound", juce::String::formatted ("rms %.1f dBFS, peak %.1f dBFS", lab::db (lab::rms (out)), lab::db (lab::peak (out))));
        // 4 小節 @120 = 8 秒。2 周目の頭 (8 s) は 1 周目の頭と同じ音のはず → 0..1 s と 8..9 s ではなく、4 小節ぶん周期的か
        auto out2 = r.run (16.0);
        const int L = (int) (8.0 * sr);
        double e = 0, p = 0;
        for (int i = 0; i < L; ++i) { const double a = out2.getSample (0, i), b = out2.getSample (0, i + L); e += (a - b) * (a - b); p += a * a; }
        check (10 * std::log10 (e / std::max (p, 1e-20)) < -60, "loops every 4 bars", juce::String::formatted ("diff %.1f dB", 10 * std::log10 (e / std::max (p, 1e-20))));
    }

    void testPitch()
    {
        std::printf ("[2] pitch between rows (tone 441.7 Hz)\n");
        Rig r (120.0);
        r.code ("void main(){ fragColor = rg(tone(441.7, 0.5)); }");
        lab::prepare (r.p, sr);
        auto out = r.run (3.0);
        const double f = zeroCrossFreq (out, (int) (0.5 * sr), (int) (2.0 * sr));
        check (std::abs (f - 441.7) < 0.01, "frequency", juce::String::formatted ("%.4f Hz", f));
        check (std::abs (lab::peak (out) - 0.5) < 0.01, "amplitude 0.5", juce::String::formatted ("peak %.4f", lab::peak (out)));

        // ループの継ぎ目: 1 周 (2 小節 @120 = 4 s) の境目で段差が通常の 1 サンプルの変化より大きくならないか
        lab::setParam (r.p, "bars", 1.0f);
        lab::prepare (r.p, sr);
        auto o2 = r.run (9.0);
        const float* x = o2.getReadPointer (0);
        double maxStep = 0, seam = 0;
        for (int i = 1; i < o2.getNumSamples(); ++i)
        {
            const double d = std::abs (x[i] - x[i - 1]);
            maxStep = std::max (maxStep, d);
            if (i == (int) (4.0 * sr) || i == (int) (8.0 * sr)) seam = std::max (seam, d);
        }
        const double expect = 0.5 * 2 * juce::MathConstants<double>::pi * 441.7 / sr;
        check (maxStep < expect * 1.05, "no clicks anywhere (loop seam included)", juce::String::formatted ("max step %.4f, seam %.4f, sine step %.4f", maxStep, seam, expect));
    }

    void testSync()
    {
        std::printf ("[3] follows the host position\n");
        Rig r (120.0, false);
        lab::prepare (r.p, sr);
        auto a = r.run (9.0, true, 0.0);           // 頭から
        auto b = r.run (2.0, true, 3.0);           // 3 拍目から (= 1.5 s の位置)
        double e = 0, p = 0;
        const int off = (int) (1.5 * sr);
        for (int i = 2000; i < (int) (1.5 * sr); ++i) { const double d = a.getSample (0, i + off) - b.getSample (0, i); e += d * d; p += a.getSample (0, i + off) * a.getSample (0, i + off); }
        check (10 * std::log10 (e / std::max (p, 1e-20)) < -60, "starting at beat 3 plays the same audio as 1.5 s into the page",
               juce::String::formatted ("diff %.1f dB", 10 * std::log10 (e / std::max (p, 1e-20))));
        auto s = r.run (1.0, false);
        check (lab::peak (s, (int) (0.02 * sr)) < 1e-6, "silent when the host is stopped (Play = Host)", juce::String::formatted ("peak %.1f dBFS", lab::db (lab::peak (s, (int) (0.02 * sr)))));
    }

    void testTempo()
    {
        std::printf ("[4] tempo change redraws the page\n");
        Rig r (120.0);
        lab::prepare (r.p, sr);
        r.run (0.5);
        r.head.bpm = 150.0;
        r.run (0.5);
        auto out = r.run (12.0);
        const int L = (int) std::lround (16 * 60.0 / 150.0 * sr);   // 4 小節 @150 = 6.4 s
        double e = 0, p = 0;
        for (int i = 0; i < out.getNumSamples() - L; ++i) { const double d = out.getSample (0, i) - out.getSample (0, i + L); e += d * d; p += out.getSample (0, i) * out.getSample (0, i); }
        check (10 * std::log10 (e / std::max (p, 1e-20)) < -60, "period is 4 bars at 150 BPM", juce::String::formatted ("diff %.1f dB", 10 * std::log10 (e / std::max (p, 1e-20))));
    }

    void testCompileError()
    {
        std::printf ("[5] compile errors\n");
        Rig r (120.0);
        r.code ("void main() {\n  float a = 1.0;\n  foo = 2.0;\n}\n");
        r.run (0.1);
        const auto st = r.p.getEngine().getStatus();
        check (! st.compileOk && st.errorLines.contains (3), "error reported on line 3", st.log.upToFirstOccurrenceOf ("\n", false, false));
        auto out = r.run (1.0);
        check (lab::rms (out) > 0.01, "keeps playing the previous shader", juce::String::formatted ("rms %.1f dBFS", lab::db (lab::rms (out))));
    }

    void testExamples()
    {
        std::printf ("[6] every example compiles and makes sound\n");
        for (auto& ex : shaderExamples)
        {
            Rig r (140.0);
            r.code (juce::String::fromUTF8 (ex.code));
            auto out = r.run (7.0);
            const auto st = r.p.getEngine().getStatus();
            check (st.compileOk && lab::allFinite (out) && lab::rms (out) > 0.003, ex.id,
                   juce::String::formatted ("rms %.1f dBFS peak %.1f dBFS, render %.2f s", lab::db (lab::rms (out)), lab::db (lab::peak (out)), st.lastRenderSeconds));
        }
    }

    int writePage (const juce::String& id, const juce::File& out)
    {
        for (auto& ex : shaderExamples)
            if (id == ex.id)
            {
                Rig r (140.0);
                r.code (juce::String::fromUTF8 (ex.code));
                lab::setParam (r.p, "gain", -6.0f);
                lab::setParam (r.p, "softclip", 1.0f);
                lab::prepare (r.p, sr);
                auto o = r.run (2 * 16 * 60.0 / 140.0);
                lab::writeWav (out, o, sr);
                std::printf ("wrote %s (peak %.1f dBFS)\n", out.getFullPathName().toRawUTF8(), lab::db (lab::peak (o)));
                return 0;
            }
        std::printf ("no example %s\n", id.toRawUTF8());
        return 1;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    Proc::syncRenderForTests = true;

    if (argc >= 4 && juce::String (argv[1]) == "page")
        return writePage (argv[2], juce::File (juce::String::fromUTF8 (argv[3])));

    auto robust = []
    {
        lab::RobustConfig c;
        c.setup = [] (juce::AudioProcessor& p) { lab::setParam (p, "play", 1.0f); };
        return c;
    };
    if (int r = lab::cliMain<Proc> (argc, argv, sr, robust); r >= 0)
        return r;

    testGlAndPage();
    testPitch();
    testSync();
    testTempo();
    testCompileError();
    testExamples();

    {
        Rig r (140.0);
        r.run (0.2);
        std::printf ("[7] CPU %.2f %% of realtime (playback only)\n", lab::cpuPercent (r.p, sr));
        r.run (1.0);
        auto png = juce::File::getCurrentWorkingDirectory().getChildFile ("renders/ui.png");
        lab::snapshot (r.p, png);
        std::printf ("[8] UI -> %s\n", png.getFullPathName().toRawUTF8());
    }
    std::printf ("%s (%d failed)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails);
    return fails == 0 ? 0 : 1;
}
