# Web 版 (../SpecShader/src.html) のサンプルを Source/Examples.h に写す。サンプルを直したらこれを流す
import pathlib, re
root = pathlib.Path(__file__).resolve().parent.parent
src = (root.parent / 'SpecShader' / 'src.html').read_text()
block = src[src.index('const EXAMPLES = ['):src.index('];', src.index('const EXAMPLES = ['))]
TR = {'// このシェーダーは毎フレーム「横 = 時間 / 縦 = 周波数」の画像を描きます。': '// Every frame this shader draws an image: x = time, y = frequency.', '// 1ピクセル = 1サンプル × 1本のサイン波。描いた画像がそのまま音になります。': '// One pixel = one sample x one sine wave. The picture is the sound.', '//   fragColor.r = 振幅': '//   fragColor.r = amplitude', '//   fragColor.g = 位相（1.0 で1周）': '//   fragColor.g = phase (1.0 = one cycle)', '// 使える変数と関数は右下のリファレンスにあります。': '// Press Help for the list of variables and functions.', '// いま何拍目か（64拍で一周）': '// which beat we are on (wraps at 64)', '// コード進行 Am - F - C - G（ルートの MIDI ノート番号）': '// chords Am - F - C - G (root as MIDI note number)', '// 振幅と位相は複素数で足していく': '// amplitude + phase are summed as complex numbers', '// パッド：ルートの倍音を少しずつ弱く': '// pad: harmonics of the root, each a little quieter', '// アルペジオ：16分音符': '// arpeggio: 16th notes', '// キック：周波数が落ちていく線。動く音は位相（周波数の積分）を自分で渡す': '// kick: a falling line. For moving pitches, pass the phase (integral of the frequency)', '// 拍頭からの秒': '// seconds since the beat', '// ハイハット：裏拍で高域にノイズを撒く（R と G を直接いじる例）': '// hi-hat: noise up high on the off-beat (writing R and G directly)', '// 位相は気にせず、R に絵を描くだけ。': '// Ignore the phase and just draw into R.', '// 時間 × 周波数 の平面に図形を置くと、その形のまま音になります。': '// Put a shape on the time x frequency plane and it sounds like that shape.', '// pos は「このピクセルが画面のどこに描かれるか」（0〜1）。横を2倍して縦横比を合わせる': '// pos = where this pixel lands on the page (0..1). x is doubled to keep the aspect', '// 顔': '// face', '// 目': '// eyes', '// 口': '// mouth', '// 終わらない上昇音。各オクターブに1本ずつ線を引き、1ページで1オクターブ上へずらす。': '// Endless rise. One line per octave, moved up one octave per page.', '// G は 0 のまま。線が行から行へ移っていくぶん、少しざらついた質感になります。': '// G stays 0. The line hops from row to row, so it sounds a bit grainy.', '// この行の高さ（オクターブ単位）': '// height of this row in octaves', '// ページの頭から終わりまでで1オクターブ上昇': '// rises one octave over the page', '// 一番近い線までの距離（半音）': '// distance to the nearest line (semitones)', '// 中域を強く、両端を弱く': '// loud in the middle, quiet at both ends', '// G（位相）を時間で揺らすと位相変調、いわゆる FM っぽい音になります。': '// Wobbling G (phase) over time gives phase modulation, the FM kind of sound.', '// ビューの上でマウスを動かしてください。横 = 深さ、縦 = 変調の速さ。': '// In the plugin mouse is fixed at (0.5, 0.5): edit depth and rate directly.', '// 位相の振れ幅（周）': '// phase swing (cycles)', '// キャリアの整数倍': '// integer multiple of the carrier', '// ここが位相変調': '// this is the phase modulation', '// backbuffer には1フレーム前の画像が入っています。': '// backbuffer holds the previous frame.', '// 前のフレームの右端（最後のサンプル）を読んで減衰させながら引き継ぐと、': '// Reading its right edge (last sample) and carrying it on while decaying', '// 鳴った音がしばらく残る「スペクトルの残響」になります。': '// makes every hit ring on: a spectral reverb.', '// 雨粒：ランダムな行に、ランダムな時刻で短い粒を落とす': '// raindrops: short grains on random rows at random times', '// 流れる線の束（2・4・8枚目の系統）': '// Flowing bundles of lines', '// 等間隔の横縞を、なめらかなノイズで歪ませた座標の上に引く。': '// Evenly spaced stripes drawn on coordinates bent by smooth noise.', '// なめらかな乱数（値ノイズ）': '// smooth random (value noise)', '// 1. 座標そのものをノイズで歪ませる（ドメインワープ）。': '// 1. bend the coordinates themselves with noise (domain warp).', '//    縦だけでなく横（時間）にもずらすと、線が渦を巻いて折り返す': '//    shifting x (time) as well as y makes the lines curl back', '// 2. ずらした座標に等間隔の横縞を引く（pow で細い線にする）': '// 2. stripes on the bent coordinates (pow makes them thin)', '// 3. 束の上下の範囲': '// 3. vertical extent of the bundle', '// 4. 拍ごとに強く出て、ゆっくり引く': '// 4. swell on the beat, then fade', '// 5. 2拍ごとに全帯域の縦線（クリック）': '// 5. a full-range vertical line (click) every 2 beats', '// V の字・山形の並び（1・3枚目の系統）': '// Rows of V shapes', '// ジグザグの線を少しずつずらして何本も重ね、段ごとに半拍単位で出し入れする。': '// Zigzags stacked with small offsets, each tier gated per half beat.', '// 0〜1 の三角波': '// triangle wave 0..1', '// 段ごとの高さ': '// height of each tier', '// ジグザグ': '// zigzag', '// 同じ形を5本ずらして束にする': '// 5 offset copies make a bundle', '// この段をこの半拍で鳴らすか': '// does this tier play in this half beat?', '// 網目（5枚目の系統）': '// Lattice', '// ノイズで塗った面から、右上がりと右下がりの斜線を暗く抜いて網にする。上の縁は山なり、ところどころ丸い穴。': '// Fill with noise, cut rising and falling diagonals out of it. Arched top edge, round holes.', '// 塗りつぶし用のノイズ（サンプルごとに変わる乱数＝ザーッという音）': '// fill noise (new random value per sample = hiss)', '// 2方向の斜線': '// diagonals both ways', '// 上の縁：1ページに3つの山': '// top edge: three arches per page', '// 丸い穴': '// round holes', '// 記号の列（6枚目の系統）': '// Rows of glyphs', '// マス目ごとに乱数で ─ │ ╱ ╲ のどれかを描き、それを山なりの道筋に沿って並べる。': '// Each grid cell draws a random stroke (horizontal, vertical or diagonal), laid along an arched path.', '// 中央で一番高い山なり': '// arch, highest in the middle', '// ─': '// horizontal', '// │': '// vertical', '// ╱': '// rising', '// ╲': '// falling', '// 空白のマスも混ぜる': '// leave some cells empty'}

def english(code):
    out = []
    for l in code.split('\n'):
        if '//' in l:
            i = l.index('//')
            c = l[i:]
            if c in TR:
                l = l[:i] + TR[c]
        assert all(ord(ch) < 128 for ch in l), l
        out.append(l)
    return '\n'.join(out)

items = re.findall(r"\{ id: '([^']+)', name: '([^']+)', code:\n`(.*?)` \}", block, re.S)
names = {  # UI は ASCII だけ (JUCE 既定書体に日本語が無い)
    'welcome': 'Welcome (chords)', 'draw': 'Draw (R only)', 'shepard': 'Shepard tone', 'pm': 'Phase play (PM)',
    'rain': 'Rain + backbuffer', 'flow': 'Image: flow lines', 'chevron': 'Image: chevrons',
    'lattice': 'Image: lattice', 'glyph': 'Image: glyphs',
}
out = ['#pragma once', '// tools/gen_examples.py が Web 版から生成する (コメントは英語に置き換え。コードエディタが全角を並べられないため)。手で直さない', '',
       'struct ShaderExample { const char* id; const char* name; const char* code; };', '',
       'static const ShaderExample shaderExamples[] = {']
for i, (eid, _name, code) in enumerate(items):
    assert ')GLSL"' not in code
    out.append('    { "%s", "%s", R"GLSL(%s)GLSL" },' % (eid, names[eid], english(code)))
out.append('};')
(root / 'Source' / 'Examples.h').write_text('\n'.join(out) + '\n')
print(len(items), 'examples')
