# SpecShader — GLSL で描いたスペクトログラムがそのまま鳴る音源 (VST3 / AU)

コードは Claude Code (Anthropic の AI) が書き、MIDy が仕様を決めて Ableton Live で確認しました。
無保証です。不具合の報告は Issue で受け付けますが、返事や修正は約束しません。Pull Request は受け付けません。
ライセンスは AGPLv3 (LICENSE)。JUCE (AGPLv3)、VST3 SDK (MIT)、Apple AudioUnitSDK (Apache 2.0)、CodeMirror 5 (MIT、Web 版のみ) を使っています。Copyright (C) 2026 MIDy

Made with Claude Code. Provided as-is. Bug reports via Issues are welcome, but replies and fixes are not promised. Pull requests are not accepted. Licensed under AGPLv3.

**ダウンロード**: Releases に Mac 版 (VST3 + AU、Intel / Apple Silicon 両対応) と Windows 版 (VST3、x64) の zip があります。

- Mac: `SpecShader.vst3` を `~/Library/Audio/Plug-Ins/VST3/` に、`SpecShader.component` を `~/Library/Audio/Plug-Ins/Components/` に入れる。署名していないので、入れたあとターミナルで

  ```
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/SpecShader.vst3 ~/Library/Audio/Plug-Ins/Components/SpecShader.component
  ```

- Windows: `SpecShader.vst3` フォルダごと `C:\Program Files\Common Files\VST3\` に入れる

GPU (OpenGL 3.3 以上) を使います。ブラウザで動く版 (`web/SpecShader.html`) もあります。

## しくみ

フラグメントシェーダーが毎フレーム 1 枚の画像を描きます。

- 横 = 時間。1 ピクセル = 1 サンプル (横幅 = サンプルレート ÷ フレームレート)
- 縦 = 周波数。1 行 = 1 本のサイン波 (行数・最低 / 最高周波数・対数 / 線形は画面下で設定)
- `fragColor.r` = その行の振幅、`fragColor.g` = その行に足す位相 (1.0 で 1 周)

```
out[n] = Σ R(n, 行) · sin(2π · (hz(行) · n / sampleRate + G(n, 行)))
```

1 周ぶん (Page bars = 1〜8 小節) を先に全部描いて音にし、DAW の再生位置とテンポに合わせてループします。
コードやテンポを変えると裏で描き直し (1 周 0.2〜1 秒ほど)、描き終わったら 20 ms でつないで差し替えます。
エディタを閉じていても描き直せます (GPU のコンテキストはプラグイン本体のスレッドが持つ)。

## 書き方

KodeLife や Shadertoy と同じ感覚で `void main()` を書きます。使えるもの (Help ボタンでも出ます):

| | |
|---|---|
| ピクセルごとの入力 | `t` (秒、ページの頭が 0)、`tl` (フレーム頭からの秒)、`bt` (拍、64 拍で 0 に戻る)、`hz` (この行の周波数)、`row`、`uv`、`pos` (ページ上の位置 0〜1) |
| uniform | `time` `resolution` `frame` `sampleRate` `frameRate` `bpm` `beat` `pageLength` `fMin` `fMax` `logScale` `backbuffer` (1 フレーム前の画像) |
| 関数 | `tone(f, amp)` 周波数 f の音を置く (行の間でも音程は正確) / `sweep(f, cycles, amp)` 動く音 (cycles = 位相の積分) / `rg(sum)` 足した vec2 を R・G に / `phase(f)` 精度の落ちない f·t / `midiHz` `hzMidi` `rowHz` `hzY` `hash` `TAU` |

```glsl
void main() {
    float e = exp(-fract(bt) * 6.0);              // 拍ごとに鳴って減衰
    vec2 sum = vec2(0.0);
    sum += tone(midiHz(60.0), 0.2 * e);           // C4
    sum += tone(midiHz(64.0), 0.2 * e);           // E4
    sum += tone(midiHz(67.0), 0.2 * e);           // G4
    fragColor = rg(sum);
}
```

- GPU の float は 32 bit なので、`sin(TAU * f * t)` のように t から位相を作ると時間とともにノイズが混ざります。`phase(f)` か `bt` を使ってください
- コード欄は日本語の表示が崩れます (JUCE のコードエディタが全角を並べられない)。コメントは英語で
- Examples に 9 つの見本があります (コード進行、お絵描き、シェパード・トーン、位相変調、backbuffer の残響、模様 4 種)

## ビルド

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE は `../_deps/JUCE` があればそれを、無ければ取得します。Windows 版は GitHub Actions (`.github/workflows/windows.yml`) でビルドしています。

- Source/ShaderEngine.* … 画面に出ない OpenGL 3.3 Core でページを描いて 1 周分の音を作る
- Source/OffscreenGL.* … そのコンテキスト (Mac は CGL、Windows は隠しウィンドウ + WGL)
- Source/Examples.h … `tools/gen_examples.py` が Web 版から生成 (コメントは英語に置き換え)
- tests/TestMain.cpp … DAW 無しの検証 (`-DLAB_BUILD_TESTS=ON` で `SpecShaderVSTTest`、`SpecShaderVSTTest robust`)
