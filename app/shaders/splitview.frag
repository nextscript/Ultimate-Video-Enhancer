#version 440
// Vorher/Nachher-Zusammensetzung (Strg+Alt+U): mpv liefert ein doppelt breites Bild
// (links Original, rechts mit Effekten). Hier wird je Bildpunkt die passende Hälfte gewählt –
// die Schieberposition ist nur ein Uniform, mpv wird beim Ziehen nicht angefasst.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float split;      // 0 … 1 innerhalb der Videofläche
    float flipY;      // 1 = Quelltextur steht auf dem Kopf
    vec4 normRect;    // Videofläche in der Anzeige (x, y, w, h; normiert)
    vec4 combRect;    // doppelt breites Bild in der mpv-Ausgabe (x, y, w, h; normiert)
    vec4 eq;          // mpv-Equalizer (brightness, contrast, saturation, hue; je /100) – wird links herausgerechnet
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 u = (qt_TexCoord0 - normRect.xy) / normRect.zw;
    if (u.x < 0.0 || u.y < 0.0 || u.x > 1.0 || u.y > 1.0) {   // schwarze Ränder
        fragColor = vec4(0.0, 0.0, 0.0, 1.0) * qt_Opacity;
        return;
    }
    float hx = u.x < split ? u.x * 0.5 : 0.5 + u.x * 0.5;
    vec2 t = combRect.xy + vec2(hx, u.y) * combRect.zw;
    if (flipY > 0.5) t.y = 1.0 - t.y;
    vec3 c = texture(source, t).rgb;
    if (u.x < split) {
        // VORHER = echtes Original: den mpv-Equalizer (Auto-Scene-Match) umkehren. mpv (video/csputils.c):
        // [U,V] um −θ drehen und mit s skalieren, dann out = k·rgb + b  →  hier rückwärts in BT.709-YCbCr
        float k = 1.0 + eq.y, b = eq.x, s = max(0.01, 1.0 + eq.z), th = eq.w * 3.14159265;
        c = (c - vec3(b)) / max(k, 0.01);
        const vec3 L = vec3(0.2126, 0.7152, 0.0722);
        float Y = dot(c, L);
        float U = (c.b - Y) / 1.8556, V = (c.r - Y) / 1.5748;
        float U0 = (cos(th) * U - sin(th) * V) / s, V0 = (sin(th) * U + cos(th) * V) / s;
        float R = Y + 1.5748 * V0, B = Y + 1.8556 * U0;
        c = clamp(vec3(R, (Y - L.r * R - L.b * B) / L.g, B), 0.0, 1.0);
    }
    fragColor = vec4(c, 1.0) * qt_Opacity;
}
