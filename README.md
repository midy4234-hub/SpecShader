# SpecShader

GLSL で描いたスペクトログラム (R = 振幅、G = 位相) を加算合成で鳴らす音源プラグイン (VST3 / AU)。
1 周 (1〜8 小節) を GPU で先に描いて、ホストの再生位置とテンポに合わせてループする。

- Source/ShaderEngine.* … 画面に出ない OpenGL 3.3 Core でページを描いて 1 周分の音を作る裏方
- Source/OffscreenGL.* … そのコンテキスト (Mac は CGL、Windows は隠しウィンドウ + WGL)
- Source/Examples.h … `tools/gen_examples.py` が Web 版 (../SpecShader/src.html) から生成
- tests/TestMain.cpp … DAW 無しの検証 (`SpecShaderVSTTest`、`SpecShaderVSTTest robust`)

Windows 版は GitHub Actions (`.github/workflows/windows.yml`) でビルドする。ランナーに GPU が無いので、動作は実機で確かめる。
