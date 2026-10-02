#include "ShaderEngine.h"
#include <juce_opengl/juce_opengl.h>
#include <cmath>
#include <map>
#include <regex>
#include <string>

using namespace juce::gl;

namespace
{
    // ---------------------------------------------------------------- シェーダー (Web 版と同じ中身、GLSL 3.30 core)
    const char* vsSource = R"(#version 330 core
void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";

    const char* prelude = R"(#version 330 core
precision highp float;
precision highp int;
uniform float time;
uniform vec2  resolution;
uniform int   frame;
uniform float sampleRate;
uniform float frameRate;
uniform float bpm;
uniform float beat;
uniform vec2  mouse;
uniform float fMin;
uniform float fMax;
uniform int   logScale;
uniform sampler2D backbuffer;
uniform sampler2D _ss_rows;
uniform float _ss_tInt;
uniform float _ss_tFrac;
uniform float _ss_beat64;
uniform float pageLength;
out vec4 fragColor;
float t; float tl; float bt; float hz; float row; vec2 uv; vec2 pos;
const float TAU = 6.283185307179586;
float rowHz(float y) { float n = clamp((y - 0.5) / max(resolution.y - 1.0, 1.0), 0.0, 1.0); return logScale == 1 ? fMin * pow(fMax / fMin, n) : mix(fMin, fMax, n); }
float hzY(float f) { float n = logScale == 1 ? log(max(f, 1e-6) / fMin) / log(fMax / fMin) : (f - fMin) / (fMax - fMin); return n * (resolution.y - 1.0) + 0.5; }
float midiHz(float m) { return 440.0 * exp2((m - 69.0) / 12.0); }
float hzMidi(float f) { return 69.0 + 12.0 * log2(max(f, 1e-6) / 440.0); }
float rowPhase() { return fract(texelFetch(_ss_rows, ivec2(int(row), 0), 0).y + hz * tl); }
uint _ss_pcg(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float hash(float x) { return float(_ss_pcg(uint(int(floor(x))))) / 4294967295.0; }
float hash(vec2 p) { return float(_ss_pcg(_ss_pcg(uint(int(floor(p.x)))) + uint(int(floor(p.y))))) / 4294967295.0; }
vec2 _ss_partial(float f, float amp, float ph) {
  float d = gl_FragCoord.y - hzY(f);
  float g = exp(-d * d / 0.7225) / 1.50662;
  if (!(f > 0.0) || f >= sampleRate * 0.5) g = 0.0;
  float a = TAU * ph;
  return amp * g * vec2(cos(a), sin(a));
}
float _ss_mulFract(float a, float n) {
  if (a == 0.0) return 0.0;
  float e = exp2(floor(log2(abs(a))) - 11.0);
  float hi = floor(a / e) * e;
  return fract(fract(hi * n) + (a - hi) * n);
}
float phase(float f) {
  float y = clamp(floor(hzY(f)), 0.0, resolution.y - 1.0);
  vec4 r = texelFetch(_ss_rows, ivec2(int(y), 0), 0);
  float df = f - r.x;
  return fract(fract(r.y + r.x * tl) + _ss_mulFract(df, _ss_tInt) + fract(df * (_ss_tFrac + tl)));
}
vec2 tone(float f, float amp) { return _ss_partial(f, amp, fract(phase(f) - rowPhase())); }
vec2 sweep(float f, float cycles, float amp) { return _ss_partial(f, amp, fract(cycles - rowPhase())); }
vec4 rg(vec2 c) { return vec4(length(c), atan(c.y, c.x) / TAU, 0.0, 1.0); }
#define main _ss_user_main
)";

    const char* epilogue = R"(
#undef main
void main() {
  tl = floor(gl_FragCoord.x) / sampleRate;
  t = time + tl;
  bt = _ss_beat64 + tl * bpm / 60.0;
  row = floor(gl_FragCoord.y);
  hz = texelFetch(_ss_rows, ivec2(int(row), 0), 0).x;
  uv = gl_FragCoord.xy / resolution;
  pos = vec2(fract(t / pageLength), uv.y);
  fragColor = vec4(0.0);
  _ss_user_main();
}
)";

    const char* synthSource = R"(#version 330 core
precision highp float; precision highp int;
uniform sampler2D spec; uniform sampler2D rows; uniform int H; uniform float sr;
out vec4 o;
void main() {
  int x = int(gl_FragCoord.x);
  float xs = float(x) / sr;
  float s = 0.0;
  for (int y = 0; y < H; y++) {
    vec4 v = texelFetch(spec, ivec2(x, y), 0);
    float a = v.r;
    if (a == 0.0 || !(abs(a) < 1e4)) continue;
    vec4 r = texelFetch(rows, ivec2(y, 0), 0);
    float g = abs(v.g) < 1e6 ? v.g : 0.0;
    s += a * sin(6.283185307179586 * fract(fract(r.y + r.x * xs) + g));
  }
  o = vec4(s, 0.0, 0.0, 1.0);
}
)";

    const char* histSource = R"(#version 330 core
precision highp float; precision highp int;
uniform sampler2D cur; uniform sampler2D prev;
uniform float rel0; uniform float spc; uniform int W; uniform int D; uniform int head0;
out vec4 o;
void main() {
  int c = int(gl_FragCoord.x);
  int y = int(gl_FragCoord.y);
  int j = (c - head0 + D) % D;
  float s0 = rel0 + float(j) * spc;
  int i0 = max(int(floor(s0)), -W);
  int i1 = min(int(ceil(s0 + spc)), W);
  int st = max(1, (i1 - i0) / 96);
  float best = -1.0; float ph = 0.0;
  for (int i = i0; i < i1; i += st) {
    vec2 v = i < 0 ? texelFetch(prev, ivec2(i + W, y), 0).rg : texelFetch(cur, ivec2(i, y), 0).rg;
    float a = abs(v.r);
    if (!(a < 1e6)) a = 0.0;
    if (a > best) { best = a; ph = v.g; }
  }
  o = vec4(max(best, 0.0), ph, 0.0, 1.0);
}
)";

    // KodeLife 風に書かれた宣言は、こちらの前置きと重なるので消す (行は残して行番号を保つ)
    juce::String wrapUser (const juce::String& code)
    {
        static const std::regex reserved (R"(^\s*uniform\s+\w+\s+(time|resolution|frame|sampleRate|frameRate|bpm|beat|mouse|fMin|fMax|logScale|backbuffer|pageLength)\s*;.*$)");
        static const std::regex outColor (R"(^\s*out\s+(highp\s+|mediump\s+|lowp\s+)?vec4\s+fragColor\s*;.*$)");
        static const std::regex version (R"(^\s*#version\b.*$)");
        juce::StringArray lines;
        lines.addLines (code);
        for (auto& l : lines)
        {
            const auto s = l.toStdString();
            if (std::regex_match (s, reserved) || std::regex_match (s, outColor) || std::regex_match (s, version))
                l = {};
        }
        return juce::String (prelude) + "#line 1\n" + lines.joinIntoString ("\n") + epilogue;
    }

    // ドライバごとのエラー書式から行番号と本文を取り出す
    //   Apple / AMD / Intel: "ERROR: 0:12: ..."   NVIDIA: "0(12) : error C1008: ..."
    bool parseErrorLine (const juce::String& line, int& lineNo, juce::String& msg)
    {
        static const std::regex a (R"((?:ERROR|WARNING):\s*\d+:(-?\d+):\s*(.*))");
        static const std::regex b (R"(^\s*\d+\((-?\d+)\)\s*:\s*(.*))");
        std::smatch m;
        const auto s = line.trim().toStdString();
        if (std::regex_search (s, m, a) || std::regex_search (s, m, b))
        {
            lineNo = std::stoi (m[1].str());
            msg = juce::String (m[2].str());
            return true;
        }
        return false;
    }

    juce::Colour colourFor (float amp)
    {
        const float db = 20.0f * std::log10 (std::max (std::abs (amp), 1.0e-7f));
        const float k = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 60.0f) * 4.0f;
        static const float c[5][3] = { { 0.122f, 0.122f, 0.122f }, { 0.20f, 0.11f, 0.33f }, { 0.74f, 0.22f, 0.36f },
                                       { 0.95f, 0.64f, 0.23f }, { 1.0f, 0.96f, 0.86f } };
        const int i = std::min (3, (int) k);
        const float f = k - (float) i;
        return juce::Colour::fromFloatRGBA (c[i][0] + (c[i + 1][0] - c[i][0]) * f,
                                            c[i][1] + (c[i + 1][1] - c[i][1]) * f,
                                            c[i][2] + (c[i + 1][2] - c[i][2]) * f, 1.0f);
    }
}

//==============================================================================
struct ShaderEngine::GL
{
    struct Prog
    {
        GLuint id = 0;
        std::map<std::string, GLint> cache;
        GLint loc (const char* name)
        {
            auto it = cache.find (name);
            if (it != cache.end()) return it->second;
            const auto l = glGetUniformLocation (id, name);
            cache[name] = l;
            return l;
        }
        void release() { if (id != 0) glDeleteProgram (id); id = 0; cache.clear(); }
    };

    GLuint vao = 0, vs = 0;
    Prog synth, hist, user;
    juce::String userCode;
    bool hasUser = false;
    int lineOffset = 0;

    GLuint spec[2] {}, fspec[2] {}, rowsTex = 0, audioTex = 0, faudio = 0, histTex = 0, fhist = 0;
    int W = 0, H = 0;

    PageSettings lastRendered;
    bool renderedOnce = false;

    static GLuint compile (GLenum type, const juce::String& src, juce::String& log)
    {
        const GLuint s = glCreateShader (type);
        const auto utf8 = src.toStdString();
        const char* p = utf8.c_str();
        glShaderSource (s, 1, &p, nullptr);
        glCompileShader (s);
        GLint ok = 0;
        glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
        if (! ok)
        {
            GLint len = 0;
            glGetShaderiv (s, GL_INFO_LOG_LENGTH, &len);
            std::string buf ((size_t) juce::jmax (1, len), '\0');
            glGetShaderInfoLog (s, len, nullptr, buf.data());
            log = juce::String (buf.c_str());
            glDeleteShader (s);
            return 0;
        }
        return s;
    }

    GLuint link (const juce::String& fsSrc, juce::String& log)
    {
        const GLuint fs = compile (GL_FRAGMENT_SHADER, fsSrc, log);
        if (fs == 0) return 0;
        const GLuint p = glCreateProgram();
        glAttachShader (p, vs);
        glAttachShader (p, fs);
        glLinkProgram (p);
        glDetachShader (p, vs);
        glDeleteShader (fs);
        GLint ok = 0;
        glGetProgramiv (p, GL_LINK_STATUS, &ok);
        if (! ok)
        {
            GLint len = 0;
            glGetProgramiv (p, GL_INFO_LOG_LENGTH, &len);
            std::string buf ((size_t) juce::jmax (1, len), '\0');
            glGetProgramInfoLog (p, len, nullptr, buf.data());
            log = juce::String (buf.c_str());
            glDeleteProgram (p);
            return 0;
        }
        return p;
    }

    static GLuint makeTex (int w, int h)
    {
        GLuint t = 0;
        glGenTextures (1, &t);
        glBindTexture (GL_TEXTURE_2D, t);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    static GLuint makeFbo (GLuint tex)
    {
        GLuint f = 0;
        glGenFramebuffers (1, &f);
        glBindFramebuffer (GL_FRAMEBUFFER, f);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        return f;
    }

    static void clearFbo (GLuint f)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, f);
        glClearColor (0, 0, 0, 0);
        glClear (GL_COLOR_BUFFER_BIT);
    }

    void releaseTargets()
    {
        for (auto* t : { &spec[0], &spec[1], &rowsTex, &audioTex, &histTex })
            if (*t != 0) { glDeleteTextures (1, t); *t = 0; }
        for (auto* f : { &fspec[0], &fspec[1], &faudio, &fhist })
            if (*f != 0) { glDeleteFramebuffers (1, f); *f = 0; }
        W = H = 0;
    }

    bool makeTargets (int w, int h, juce::String& err)
    {
        releaseTargets();
        W = w; H = h;
        for (int i = 0; i < 2; ++i) { spec[i] = makeTex (W, H); fspec[i] = makeFbo (spec[i]); }
        rowsTex = makeTex (H, 1);
        audioTex = makeTex (W, 1);  faudio = makeFbo (audioTex);
        histTex = makeTex (historyColumns, H);  fhist = makeFbo (histTex);
        for (auto f : { fspec[0], fspec[1], faudio, fhist })
        {
            glBindFramebuffer (GL_FRAMEBUFFER, f);
            if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                err = "This GPU cannot render to float textures";
                return false;
            }
        }
        glBindFramebuffer (GL_FRAMEBUFFER, 0);
        return true;
    }

    void release()
    {
        releaseTargets();
        synth.release(); hist.release(); user.release();
        if (vs != 0) glDeleteShader (vs);
        if (vao != 0) glDeleteVertexArrays (1, &vao);
        vs = vao = 0;
    }
};

//==============================================================================
ShaderEngine::ShaderEngine (Publish publishFn)
    : juce::Thread ("SpecShader GL"), publish (std::move (publishFn))
{
    startThread (juce::Thread::Priority::normal);
}

ShaderEngine::~ShaderEngine()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void ShaderEngine::request (const PageSettings& s)
{
    {
        const juce::ScopedLock sl (lock);
        if (pendingGen != 0 && pending == s)
            return;
        pending = s;
        ++pendingGen;
    }
    wake.signal();
}

bool ShaderEngine::renderBlocking (const PageSettings& s, int timeoutMs)
{
    juce::uint64 gen;
    {
        const juce::ScopedLock sl (lock);
        if (! (pendingGen != 0 && pending == s))
        {
            pending = s;
            ++pendingGen;
        }
        gen = pendingGen;
    }
    wake.signal();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        {
            const juce::ScopedLock sl (lock);
            if (doneGen >= gen) return true;
            if (! status.glReady && status.glError.isNotEmpty()) return false;
        }
        if (juce::Time::getMillisecondCounter() > end) return false;
        finished.wait (20);
    }
}

ShaderEngine::Status ShaderEngine::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

ShaderEngine::Picture ShaderEngine::getPicture() const
{
    const juce::ScopedLock sl (lock);
    return picture;
}

void ShaderEngine::setStatus (std::function<void (Status&)> fn)
{
    const juce::ScopedLock sl (lock);
    fn (status);
}

void ShaderEngine::run()
{
    juce::String err;
    g = std::make_unique<GL>();
    bool ok = gl.create (err);
    if (ok)
    {
        glGenVertexArrays (1, &g->vao);
        glBindVertexArray (g->vao);
        g->vs = GL::compile (GL_VERTEX_SHADER, vsSource, err);
        ok = g->vs != 0
          && (g->synth.id = g->link (synthSource, err)) != 0
          && (g->hist.id = g->link (histSource, err)) != 0;
        if (ok)
        {
            // #line の数え方がドライバで違うので、1 行目にわざとエラーを置いて差を測る
            juce::String log;
            if (auto p = g->link (wrapUser ("@"), log)) glDeleteProgram (p);
            juce::StringArray lines;
            lines.addLines (log);
            for (auto& l : lines)
            {
                int n = 0; juce::String m;
                if (parseErrorLine (l, n, m)) { g->lineOffset = n - 1; break; }
            }
        }
    }

    if (! ok)
    {
        setStatus ([&] (Status& s) { s.glReady = false; s.glError = err.isEmpty() ? juce::String ("OpenGL init failed") : err; });
        if (g) g->release();
        gl.destroy();
        while (! threadShouldExit())
        {
            finished.signal();
            wake.wait (200);
        }
        return;
    }
    setStatus ([] (Status& s) { s.glReady = true; s.glError = {}; });

    while (! threadShouldExit())
    {
        gl.pump();
        PageSettings s;
        juce::uint64 gen = 0;
        {
            const juce::ScopedLock sl (lock);
            if (pendingGen != doneGen) { s = pending; gen = pendingGen; }
        }
        if (gen == 0)
        {
            wake.wait (30);
            continue;
        }

        setStatus ([] (Status& st) { st.busy = true; st.progress = 0.0f; });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (renderPage (s, gen))
        {
            const double sec = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const juce::ScopedLock sl (lock);
            doneGen = gen;
            status.busy = pendingGen != doneGen;
            status.progress = 1.0f;
            status.lastRenderSeconds = sec;
        }
        finished.signal();
    }

    g->release();
    g.reset();
    gl.destroy();
}

bool ShaderEngine::renderPage (const PageSettings& s, juce::uint64 gen)
{
    auto& G = *g;

    // 1. ユーザーのシェーダー
    if (! G.hasUser || s.code != G.userCode)
    {
        juce::String log;
        const GLuint p = G.link (wrapUser (s.code), log);
        if (p != 0)
        {
            G.user.release();
            G.user.id = p;
            G.userCode = s.code;
            G.hasUser = true;
            setStatus ([] (Status& st) { st.compileOk = true; st.log = "OK"; st.errorLines.clear(); });
        }
        else
        {
            juce::StringArray out, lines;
            juce::Array<int> errLines;
            lines.addLines (log);
            for (auto& l : lines)
            {
                if (l.trim().isEmpty()) continue;
                int n = 0; juce::String m;
                if (parseErrorLine (l, n, m))
                {
                    n -= G.lineOffset;
                    if (n >= 1) errLines.addIfNotAlreadyThere (n);
                    out.add ((n >= 1 ? "line " + juce::String (n) : juce::String ("(internal)")) + ": " + m);
                }
                else
                    out.add (l.trim());
            }
            const auto text = out.joinIntoString ("\n") + (G.hasUser ? "\n(still playing the last shader that compiled)" : "");
            setStatus ([&] (Status& st) { st.compileOk = false; st.log = text; st.errorLines = errLines; });
            // コードだけが変わって失敗したなら、描き直さない (前の音のまま)
            if (G.hasUser && G.renderedOnce && G.lastRendered.sameExceptCode (s))
                return true;
        }
    }

    // 2. 描き先
    const double sr = s.sampleRate;
    const double fps = juce::jlimit (10.0, 480.0, s.fps);
    const int W = juce::jlimit (16, 16384, (int) std::lround (sr / fps));
    const int H = juce::jlimit (16, 2048, s.rows);
    if (W != G.W || H != G.H)
    {
        juce::String err;
        if (! G.makeTargets (W, H, err))
        {
            setStatus ([&] (Status& st) { st.glReady = false; st.glError = err; });
            return true;
        }
    }
    const double nyq = sr * 0.5;
    const double fMax = juce::jlimit (20.0, nyq * 0.995, s.fMax);
    const double fMin = juce::jlimit (1.0, fMax * 0.99, s.fMin);
    std::vector<double> freqs ((size_t) H);
    for (int y = 0; y < H; ++y)
    {
        const double n = H > 1 ? (double) y / (H - 1) : 0.0;
        const double f = s.logScale ? fMin * std::pow (fMax / fMin, n) : fMin + (fMax - fMin) * n;
        freqs[(size_t) y] = (double) (float) f;
    }

    // 3. ページの長さ
    const double pageSeconds = s.bars * 4 * 60.0 / juce::jlimit (20.0, 999.0, s.bpm);
    const int pageLen = std::max (W, (int) std::lround (pageSeconds * sr));
    const int xf = std::min ((int) std::lround (0.02 * sr), pageLen / 4);
    const int total = pageLen + xf;
    const int frames = (total + W - 1) / W;
    std::vector<float> buf ((size_t) frames * (size_t) W);

    constexpr int D = historyColumns;
    const double spc = juce::jlimit (1.0, (double) W, (double) pageLen / D);
    int colsWritten = 0;

    std::vector<float> rowsData ((size_t) H * 4), readBuf ((size_t) W * 4);
    glBindVertexArray (G.vao);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    GL::clearFbo (G.fspec[0]);
    GL::clearFbo (G.fspec[1]);
    GL::clearFbo (G.fhist);

    int cur = 0;
    for (int k = 0; k < frames; ++k)
    {
        if ((k & 3) == 0)
        {
            gl.pump();
            const juce::ScopedLock sl (lock);
            if (pendingGen != gen || threadShouldExit()) return false;   // 新しい頼みが来た: やり直す
            status.progress = (float) k / (float) frames;
        }
        const juce::int64 n0 = (juce::int64) k * W;

        // 各行の、このフレーム頭での位相 (倍精度で計算して渡す)
        for (int y = 0; y < H; ++y)
        {
            const double f = freqs[(size_t) y];
            rowsData[(size_t) y * 4] = (float) f;
            rowsData[(size_t) y * 4 + 1] = (float) std::fmod (f * (double) n0 / sr, 1.0);
        }
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, G.rowsTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, H, 1, GL_RGBA, GL_FLOAT, rowsData.data());

        const GLuint prevTex = G.spec[cur ^ 1], curTex = G.spec[cur];

        // a. ユーザーのシェーダー → スペクトル画像
        glBindFramebuffer (GL_FRAMEBUFFER, G.fspec[cur]);
        glViewport (0, 0, W, H);
        if (G.hasUser)
        {
            auto& u = G.user;
            glUseProgram (u.id);
            glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, prevTex);
            glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, G.rowsTex);
            const double time = (double) n0 / sr;
            const double tInt = std::floor (time);
            glUniform1i (u.loc ("backbuffer"), 0);
            glUniform1i (u.loc ("_ss_rows"), 1);
            glUniform1f (u.loc ("time"), (float) time);
            glUniform2f (u.loc ("resolution"), (float) W, (float) H);
            glUniform1i (u.loc ("frame"), k);
            glUniform1f (u.loc ("sampleRate"), (float) sr);
            glUniform1f (u.loc ("frameRate"), (float) (sr / W));
            glUniform1f (u.loc ("bpm"), (float) s.bpm);
            glUniform1f (u.loc ("beat"), (float) (time * s.bpm / 60.0));
            glUniform2f (u.loc ("mouse"), 0.5f, 0.5f);
            glUniform1f (u.loc ("fMin"), (float) fMin);
            glUniform1f (u.loc ("fMax"), (float) fMax);
            glUniform1i (u.loc ("logScale"), s.logScale ? 1 : 0);
            glUniform1f (u.loc ("_ss_tInt"), (float) tInt);
            glUniform1f (u.loc ("_ss_tFrac"), (float) (time - tInt));
            glUniform1f (u.loc ("_ss_beat64"), (float) std::fmod (time * s.bpm / 60.0, 64.0));
            glUniform1f (u.loc ("pageLength"), (float) (pageLen / sr));
            glDrawArrays (GL_TRIANGLES, 0, 3);
        }
        else
        {
            glClearColor (0, 0, 0, 0);
            glClear (GL_COLOR_BUFFER_BIT);
        }

        // b. 加算合成 → 1 行の音
        glBindFramebuffer (GL_FRAMEBUFFER, G.faudio);
        glViewport (0, 0, W, 1);
        glUseProgram (G.synth.id);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, curTex);
        glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, G.rowsTex);
        glUniform1i (G.synth.loc ("spec"), 0);
        glUniform1i (G.synth.loc ("rows"), 1);
        glUniform1i (G.synth.loc ("H"), H);
        glUniform1f (G.synth.loc ("sr"), (float) sr);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glReadPixels (0, 0, W, 1, GL_RGBA, GL_FLOAT, readBuf.data());
        for (int i = 0; i < W; ++i)
            buf[(size_t) n0 + (size_t) i] = readBuf[(size_t) i * 4];

        // c. 表示用の列 (ページの頭から D 列まで)
        const juce::int64 n1 = n0 + W;
        const int A0 = colsWritten;
        const int A1 = std::min (D, (int) std::floor ((double) n1 / spc));
        if (A1 > A0)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, G.fhist);
            glViewport (0, 0, D, H);
            glUseProgram (G.hist.id);
            glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, curTex);
            glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, prevTex);
            glUniform1i (G.hist.loc ("cur"), 0);
            glUniform1i (G.hist.loc ("prev"), 1);
            glUniform1f (G.hist.loc ("rel0"), (float) (A0 * spc - (double) n0));
            glUniform1f (G.hist.loc ("spc"), (float) spc);
            glUniform1i (G.hist.loc ("W"), W);
            glUniform1i (G.hist.loc ("D"), D);
            glUniform1i (G.hist.loc ("head0"), A0);
            glEnable (GL_SCISSOR_TEST);
            glScissor (A0, 0, A1 - A0, H);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            glDisable (GL_SCISSOR_TEST);
            colsWritten = A1;
        }
        cur ^= 1;
    }

    // 4. ページの終わりの続き (pageLen 以降) を頭に重ねて、ループの継ぎ目をつなぐ
    auto page = std::make_unique<PageAudio>();
    page->data.assign (buf.begin(), buf.begin() + pageLen);
    for (int i = 0; i < xf; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) xf);
        page->data[(size_t) i] = page->data[(size_t) i] * w + buf[(size_t) (pageLen + i)] * (1.0f - w);
    }
    for (auto& v : page->data)
        if (! std::isfinite (v)) v = 0.0f;
    page->bpm = s.bpm;
    page->sampleRate = sr;
    page->bars = s.bars;

    // 5. 表示用の画像
    std::vector<float> hb ((size_t) D * (size_t) H * 4);
    glBindFramebuffer (GL_FRAMEBUFFER, G.fhist);
    glReadPixels (0, 0, D, H, GL_RGBA, GL_FLOAT, hb.data());
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    juce::Image img (juce::Image::RGB, D, H, false);
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < D; ++x)
                bd.setPixelColour (x, H - 1 - y, colourFor (hb[((size_t) y * D + (size_t) x) * 4]));
    }

    {
        const juce::ScopedLock sl (lock);
        if (pendingGen != gen) return false;
        picture.image = img;
        picture.rows = H;
        picture.bars = s.bars;
        picture.logScale = s.logScale;
        picture.fMin = fMin;
        picture.fMax = fMax;
        ++status.renders;
    }
    G.lastRendered = s;
    G.renderedOnce = true;
    publish (std::move (page));
    return true;
}
