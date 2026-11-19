#version 440
// Glaseffekt für Seitenleiste und Plugin-Fenster: weichgezeichneter Hintergrund (source), am Rand wie durch
// eine Glaskante gebrochen (Bild wird zur Mitte hin gezogen, leichte Farbsäume), getönt, mit Glanzkante
// (Licht von oben links) und abgerundeten, geglätteten Ecken.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;          // Größe in Pixeln
    float radius;       // Eckenradius in Pixeln
    float bezel;        // Breite der brechenden Kante in Pixeln
    float refraction;   // maximale Verschiebung an der Kante in Pixeln
    float tintAlpha;    // Anteil der Tönung (0 = klares Glas)
    float highlight;    // Stärke der Glanzkante
    vec4 tint;          // Tönungsfarbe
};
layout(binding = 1) uniform sampler2D source;

float roundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 half_ = size * 0.5;
    vec2 p = qt_TexCoord0 * size - half_;
    float r = min(radius, min(half_.x, half_.y));
    float d = roundBox(p, half_, r);              // < 0 innen, 0 am Rand
    float coverage = clamp(0.5 - d, 0.0, 1.0);    // geglätteter Rand
    if (coverage <= 0.0) { fragColor = vec4(0.0); return; }

    // Randnormale (zeigt nach außen) aus dem Gradienten der Distanz
    vec2 e = vec2(0.5, 0.0);
    vec2 g = vec2(roundBox(p + e.xy, half_, r) - roundBox(p - e.xy, half_, r),
                  roundBox(p + e.yx, half_, r) - roundBox(p - e.yx, half_, r));
    vec2 n = length(g) > 1e-4 ? normalize(g) : vec2(0.0);

    float inside = max(-d, 0.0);
    float t = clamp(1.0 - inside / max(bezel, 1.0), 0.0, 1.0);
    float bend = t * t;                           // stärker, je näher am Rand
    vec2 shift = -n * bend * refraction;          // zur Mitte hin abtasten → Bild wölbt sich zur Kante
    vec2 uv = clamp((p + half_ + shift) / size, 0.0, 1.0);
    vec2 ca = n * bend * 1.5 / size;              // leichte Farbsäume an der Kante

    vec3 col;
    col.r = texture(source, clamp(uv - ca, 0.0, 1.0)).r;
    col.g = texture(source, uv).g;
    col.b = texture(source, clamp(uv + ca, 0.0, 1.0)).b;

    col = mix(col, tint.rgb, clamp(tintAlpha, 0.0, 1.0));

    // Glanzkante: hell oben links, schwächer unten rechts; dazu ein weicher Schimmer entlang der Kante
    float rim = clamp(1.0 - inside / 2.0, 0.0, 1.0);
    float lightA = clamp(dot(n, normalize(vec2(-0.55, -0.85))), 0.0, 1.0);
    float lightB = clamp(dot(n, normalize(vec2(0.55, 0.85))), 0.0, 1.0);
    col += highlight * (rim * (0.8 * lightA + 0.3 * lightB) + bend * 0.08 * (0.6 + 0.4 * lightA));

    fragColor = vec4(min(col, vec3(1.0)), 1.0) * coverage * qt_Opacity;
}
