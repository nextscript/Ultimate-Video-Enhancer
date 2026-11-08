#pragma once
// Live-Denoising: Einstellungen, GPU-Shader (Bilateral, Adaptive + Debanding) und der Filtergraph für Temporal.
// Bewusst getrennt von Params/ShaderBuilder: die Bedeutung der UVE-Profilwerte (auch dn/gpuBase) bleibt unverändert.
//
//   Bilateral, Adaptive, NLMeans  →  eigene Shader-Stufe vor der UVE-Stufe (//!HOOK MAIN, zustandslos)
//   Temporal                      →  mpv-Videofilter vor dem Renderer: Benutzer-Shader haben unter vo=libmpv keine
//                                    Texturen, die einen Frame überleben. atadenoise hält ein festes Fenster aus
//                                    s Frames; mpv baut es bei Seek und Formatwechsel neu auf.

#include <QJsonObject>
#include <QString>

namespace uve::fx {

enum class DenoiseMode { Off, Bilateral, AdaptiveDeband, Temporal, NLMeans };

struct DenoiseParams
{
    DenoiseMode mode = DenoiseMode::Off;
    int strength = 35;    // 0 … 100
    int detail = 60;      // 0 … 100: Kanten und Feinstruktur erhalten
    int debanding = 35;   // 0 … 100, nur AdaptiveDeband
    bool operator==(const DenoiseParams &) const = default;
};

inline constexpr int kDenoiseSchemaVersion = 1;
// Eigene Stufe des Plugins in der ShaderChain; dieser Name steht auch in den //!DESC-Zeilen (Fehlerzuordnung)
inline constexpr const char *kDenoiseStageOwner = "org.nextscript.ultimate-video-enhancer#denoise";

QString denoiseModeKey(DenoiseMode mode);                 // off | bilateral | adaptive | temporal | nlmeans
DenoiseMode denoiseModeFromKey(const QString &key);       // Unbekanntes → Off
DenoiseParams denoiseFromJson(const QJsonObject &o);      // fehlend/kaputt → Standard (Off), Werte begrenzt
QJsonObject denoiseToJson(const DenoiseParams &p);

// Mäßige Voreinstellungen je Auflösung ("480p", "720p", "1080p", "4k"); der Modus bleibt, Off bleibt unverändert
DenoiseParams denoisePreset(const DenoiseParams &current, const QString &resolution);

// true, wenn der Modus mit diesen Werten eine Shader-Stufe braucht (Stärke 0 = keine Pässe)
bool denoiseUsesShader(const DenoiseParams &p);

// Shader-Text der Stufe; leer, wenn denoiseUsesShader() false ist.
// saveOriginal: sichert vorab UVEORIG, damit "Vorher/Nachher" links das unbehandelte Bild zeigt.
QString buildDenoiseShader(const DenoiseParams &p, bool saveOriginal);

// lavfi-Graph für Temporal; leer in anderen Modi und bei Stärke 0
QString buildTemporalGraph(const DenoiseParams &p);

} // namespace uve::fx
