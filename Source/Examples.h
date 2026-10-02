#pragma once
// tools/gen_examples.py が Web 版から生成する (コメントは英語に置き換え。コードエディタが全角を並べられないため)。手で直さない

struct ShaderExample { const char* id; const char* name; const char* code; };

static const ShaderExample shaderExamples[] = {
    { "welcome", "Welcome (chords)", R"GLSL(// SpecShader
// Every frame this shader draws an image: x = time, y = frequency.
// One pixel = one sample x one sine wave. The picture is the sound.
//
//   fragColor.r = amplitude
//   fragColor.g = phase (1.0 = one cycle)
//
// Press Help for the list of variables and functions.

float env(float x, float k) { return x < 0.0 ? 0.0 : exp(-x * k); }

void main() {
    float b   = bt;                        // which beat we are on (wraps at 64)
    int   c   = int(mod(floor(b / 4.0), 4.0));

    // chords Am - F - C - G (root as MIDI note number)
    float root  = c == 0 ? 57.0 : c == 1 ? 53.0 : c == 2 ? 60.0 : 55.0;
    float third = c == 0 ? 3.0 : 4.0;

    vec2 sum = vec2(0.0);                  // amplitude + phase are summed as complex numbers

    // pad: harmonics of the root, each a little quieter
    for (int k = 1; k <= 8; k++) {
        sum += tone(midiHz(root - 12.0) * float(k), 0.12 / float(k));
    }

    // arpeggio: 16th notes
    int   i  = int(mod(floor(b * 4.0), 4.0));
    float n  = root + 12.0 + (i == 0 ? 0.0 : i == 1 ? third : i == 2 ? 7.0 : 12.0);
    float ea = env(fract(b * 4.0), 5.0);
    for (int k = 1; k <= 5; k++) {
        sum += tone(midiHz(n) * float(k), 0.25 * ea / float(k * k));
    }

    // kick: a falling line. For moving pitches, pass the phase (integral of the frequency)
    float kt = fract(b) * 60.0 / bpm;      // seconds since the beat
    float kf = 45.0 + 110.0 * exp(-kt * 28.0);
    float kc = 45.0 * kt + 110.0 * (1.0 - exp(-kt * 28.0)) / 28.0;
    sum += sweep(kf, kc, 0.9 * env(kt, 7.0));

    fragColor = rg(sum);

    // hi-hat: noise up high on the off-beat (writing R and G directly)
    float ht   = fract(b + 0.5) * 60.0 / bpm;
    float band = smoothstep(6000.0, 9000.0, hz);
    float nz   = hash(vec2(row, floor(t * sampleRate / 8.0)));
    fragColor.r += band * nz * 0.04 * env(ht, 40.0);
    fragColor.g += band > 0.0 ? hash(row) : 0.0;
}
)GLSL" },
    { "draw", "Draw (R only)", R"GLSL(// Ignore the phase and just draw into R.
// Put a shape on the time x frequency plane and it sounds like that shape.

float ring(vec2 p, vec2 c, float r, float w) {
    return smoothstep(w, 0.0, abs(length(p - c) - r));
}

void main() {
    // pos = where this pixel lands on the page (0..1). x is doubled to keep the aspect
    vec2 p = pos * vec2(2.0, 1.0);
    float w = 0.012;

    float a = 0.0;
    a += ring(p, vec2(1.0, 0.5), 0.32, w);                   // face
    a += ring(p, vec2(0.87, 0.62), 0.045, w);                // eyes
    a += ring(p, vec2(1.13, 0.62), 0.045, w);
    a += ring(p, vec2(1.0, 0.5), 0.18, w) * step(p.y, 0.45); // mouth

    fragColor = vec4(a * 0.06, 0.0, 0.0, 1.0);
}
)GLSL" },
    { "shepard", "Shepard tone", R"GLSL(// Endless rise. One line per octave, moved up one octave per page.
// G stays 0. The line hops from row to row, so it sounds a bit grainy.

void main() {
    float oct  = hzMidi(hz) / 12.0;              // height of this row in octaves
    float ph   = fract(oct - pos.x);             // rises one octave over the page
    float d    = min(ph, 1.0 - ph) * 12.0;       // distance to the nearest line (semitones)
    float line = exp(-d * d / 0.09);
    float bell = exp(-pow((oct - 5.5) / 1.6, 2.0));   // loud in the middle, quiet at both ends
    fragColor = vec4(line * bell * 0.35, 0.0, 0.0, 1.0);
}
)GLSL" },
    { "pm", "Phase play (PM)", R"GLSL(// Wobbling G (phase) over time gives phase modulation, the FM kind of sound.
// In the plugin mouse is fixed at (0.5, 0.5): edit depth and rate directly.

void main() {
    float f     = midiHz(45.0);                     // A1
    float depth = mouse.x * 3.0;                    // phase swing (cycles)
    float rate  = f * floor(1.0 + mouse.y * 4.0);   // integer multiple of the carrier

    vec2 sum = vec2(0.0);
    for (int k = 1; k <= 6; k++) {
        sum += tone(f * float(k), 0.3 / float(k));
    }
    fragColor = rg(sum);
    fragColor.g += depth * sin(TAU * phase(rate));  // this is the phase modulation
}
)GLSL" },
    { "rain", "Rain + backbuffer", R"GLSL(// backbuffer holds the previous frame.
// Reading its right edge (last sample) and carrying it on while decaying
// makes every hit ring on: a spectral reverb.

void main() {
    float last = texelFetch(backbuffer, ivec2(int(resolution.x) - 1, int(row)), 0).r;
    float tail = last * exp(-tl * 2.5);

    // raindrops: short grains on random rows at random times
    float slot = floor(t * 24.0);
    float hit  = step(0.996, hash(vec2(row, slot)));
    float drop = hit * exp(-fract(t * 24.0) * 6.0) * 0.3;

    float band = smoothstep(200.0, 600.0, hz) * (1.0 - smoothstep(3000.0, 9000.0, hz));
    fragColor  = vec4(max(tail, drop * band), 0.0, 0.0, 1.0);
}
)GLSL" },
    { "flow", "Image: flow lines", R"GLSL(// Flowing bundles of lines
// Evenly spaced stripes drawn on coordinates bent by smooth noise.

float noise(vec2 p) {                        // smooth random (value noise)
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i), b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 p = pos;

    // 1. bend the coordinates themselves with noise (domain warp).
    //    shifting x (time) as well as y makes the lines curl back
    vec2 q = p;
    q += 0.36 * (vec2(noise(p * vec2(4.0, 3.0) + 1.7), noise(p * vec2(4.0, 3.0) + 8.3)) - 0.5);
    q += 0.16 * (vec2(noise(q * vec2(9.0, 7.0) + 4.1), noise(q * vec2(9.0, 7.0) + 2.9)) - 0.5);
    float y = q.y;

    // 2. stripes on the bent coordinates (pow makes them thin)
    float line = pow(0.5 + 0.5 * cos(TAU * y * 45.0), 10.0);

    // 3. vertical extent of the bundle
    float band = smoothstep(0.15, 0.3, y) * (1.0 - smoothstep(0.55, 0.85, y));

    // 4. swell on the beat, then fade
    float e = 0.35 + 0.65 * exp(-fract(bt / 2.0) * 3.0);

    fragColor = vec4(line * band * e * 0.1, 0.0, 0.0, 1.0);

    // 5. a full-range vertical line (click) every 2 beats
    float tc = fract(bt / 2.0) * 120.0 / bpm;
    fragColor.r += exp(-tc * 600.0) * 0.05;
}
)GLSL" },
    { "chevron", "Image: chevrons", R"GLSL(// Rows of V shapes
// Zigzags stacked with small offsets, each tier gated per half beat.

float tri(float x) { return abs(fract(x) - 0.5) * 2.0; }   // triangle wave 0..1

void main() {
    float a = 0.0;
    for (int k = 0; k < 6; k++) {
        float fk   = float(k);
        float base = 0.30 + fk * 0.1;                        // height of each tier
        float zig  = 0.035 * tri(pos.x * (10.0 + fk * 4.0)); // zigzag
        float ak = 0.0;
        for (int j = 0; j < 5; j++) {                        // 5 offset copies make a bundle
            float d = abs(pos.y - (base + zig + float(j) * 0.007));
            ak += smoothstep(0.003, 0.0, d) * (1.0 - float(j) * 0.15);
        }
        float gate = step(0.4, hash(vec2(fk, floor(bt * 2.0))));  // does this tier play in this half beat?
        a += ak * gate;
    }
    fragColor = vec4(a * 0.08, 0.0, 0.0, 1.0);
}
)GLSL" },
    { "lattice", "Image: lattice", R"GLSL(// Lattice
// Fill with noise, cut rising and falling diagonals out of it. Arched top edge, round holes.

void main() {
    // fill noise (new random value per sample = hiss)
    float fill = hash(vec2(row, floor(t * sampleRate / 2.0)));

    // diagonals both ways
    float s1 = pow(0.5 + 0.5 * cos(TAU * (pos.y * 18.0 + pos.x * 14.0)), 12.0);
    float s2 = pow(0.5 + 0.5 * cos(TAU * (pos.y * 18.0 - pos.x * 14.0)), 12.0);
    float net = max(s1, s2);

    // top edge: three arches per page
    float top = 0.35 + 0.55 * pow(sin(3.14159 * fract(pos.x * 3.0)), 2.0);
    float mask = 1.0 - smoothstep(top - 0.05, top, pos.y);

    // round holes
    vec2 c = vec2(fract(pos.x * 3.0) - 0.75, (pos.y - 0.4) / 3.0 * 2.0);
    float hole = smoothstep(0.06, 0.08, length(c));

    float a = fill * (1.0 - 0.95 * net) * mask * hole;
    fragColor = vec4(a * 0.03, 0.0, 0.0, 1.0);
}
)GLSL" },
    { "glyph", "Image: glyphs", R"GLSL(// Rows of glyphs
// Each grid cell draws a random stroke (horizontal, vertical or diagonal), laid along an arched path.

void main() {
    float path = 0.25 + 0.45 * (1.0 - abs(pos.x - 0.5) * 2.0);   // arch, highest in the middle
    vec2 g  = vec2(pos.x * 90.0, (pos.y - path) * 60.0);
    vec2 id = floor(g), f = fract(g) - 0.5;

    float r = hash(id + vec2(7.0, 3.0));
    float d = r < 0.25 ? abs(f.y)                            // horizontal
            : r < 0.5  ? abs(f.x)                            // vertical
            : r < 0.75 ? abs(f.x - f.y) * 0.7071             // rising
            :            abs(f.x + f.y) * 0.7071;            // falling
    float stroke = smoothstep(0.1, 0.0, d);
    float on = step(0.3, hash(id + vec2(31.0, 17.0)));      // leave some cells empty
    float inBand = 1.0 - smoothstep(0.06, 0.12, abs(pos.y - path));

    fragColor = vec4(stroke * on * inBand * 0.1, 0.0, 0.0, 1.0);
}
)GLSL" },
};
