#pragma once
#include "engine/DenoiseShader.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>

namespace uve {
class IPlayerHost;
}

namespace uve::fx {

// Live-Denoising im Effekte-Menü: Modus und Regler, eigene Shader-Stufe bzw. Videofilter, Fehler-Rückfall.
// Eigener Einstellungsblock "denoising" – Presets, UVE-Profile und Undo der Effekte bleiben davon unberührt.
class DenoiseController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QVariantList modes READ modes NOTIFY languageChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(int strength READ strength NOTIFY changed)
    Q_PROPERTY(int detail READ detail NOTIFY changed)
    Q_PROPERTY(int debanding READ debanding NOTIFY changed)
    Q_PROPERTY(bool modeActive READ modeActive NOTIFY changed)
    Q_PROPERTY(bool debandingEnabled READ debandingEnabled NOTIFY changed)
    Q_PROPERTY(bool temporal READ temporal NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsProblem READ statusIsProblem NOTIFY statusChanged)

public:
    DenoiseController(IPlayerHost *host, const QString &pluginId, QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVariantList modes() const;
    QString mode() const { return denoiseModeKey(m_params.mode); }
    int strength() const { return m_params.strength; }
    int detail() const { return m_params.detail; }
    int debanding() const { return m_params.debanding; }
    bool modeActive() const { return m_params.mode != DenoiseMode::Off; }
    bool debandingEnabled() const { return m_params.mode == DenoiseMode::AdaptiveDeband; }
    bool temporal() const { return m_params.mode == DenoiseMode::Temporal; }
    QString status() const;
    bool statusIsProblem() const { return !m_problem.isEmpty(); }
    QString pipelineLabel() const;   // leer, wenn nichts läuft

    void load(const QJsonObject &o);
    QJsonObject toJson() const;

    void registerStage();                 // legt die Reihenfolge fest: Denoising vor der UVE-Stufe
    void setRunning(bool running);        // Plugin aktiv und Effekte an
    void apply(bool compare);             // Shader-Stufe und Filter auf den aktuellen Stand bringen
    bool stageSavesOriginal() const { return !m_stageFile.isEmpty() && m_stageCompare; }
    bool usesShader() const { return m_available && m_running && denoiseUsesShader(m_params); }
    void onShaderError(const QString &message);
    void retranslate();

    Q_INVOKABLE void setMode(const QString &key);
    Q_INVOKABLE void setStrength(int v);
    Q_INVOKABLE void setDetail(int v);
    Q_INVOKABLE void setDebanding(int v);
    Q_INVOKABLE void applyPreset(const QString &resolution);
    Q_INVOKABLE void reset();

signals:
    void changed();
    void statusChanged();
    void languageChanged();
    void persistRequested();
    void stageRequested();                // UveController entscheidet die Reihenfolge der Stufen

private:
    void setParams(const DenoiseParams &next, bool immediately);
    void fallBack(DenoiseMode mode, const QString &problem);
    void pollFilter();
    bool writeStage(const QString &text);
    void removeOldStageFiles();

    IPlayerHost *m_host;
    QString m_id;
    DenoiseParams m_params;
    bool m_available = true;
    bool m_disabledBySetting = false;
    bool m_running = false;
    QTimer m_commitTimer, m_pollTimer;
    QString m_problem, m_filterInfo, m_stageText, m_stageFile, m_graph, m_session;
    bool m_stageCompare = false;
    int m_seq = 0, m_overloadSeconds = 0, m_graceSeconds = 0;
    qint64 m_lastDropped = -1;
};

} // namespace uve::fx
