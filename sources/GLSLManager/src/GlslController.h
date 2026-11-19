#pragma once
// Verwaltung der Filter (wie die Custom Filter Codes des Userscripts) und deren Anwendung als Shader-Stufen.
//
// Jede aktive Stufe ist ein eigener "Platz" <pluginId>#sN, damit ein fehlerhafter Shader nur sich selbst abschaltet.
// Die Reihenfolge im ShaderChain hängt am ersten Aufruf je Besitzer: Beim Umsortieren wandern daher die Dateien
// durch die Plätze, nicht die Plätze selbst. Mehrere Änderungen laufen nacheinander mit kleiner Pause, weil der Host
// einen Kompilierfehler der zuletzt geänderten Stufe zuordnet.
#include "PluginApi.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace glslmgr {

class GlslController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool masterEnabled READ masterEnabled WRITE setMasterEnabled NOTIFY entriesChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY entriesChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY entriesChanged)
    Q_PROPERTY(QString captureId READ captureId NOTIFY captureChanged)
    Q_PROPERTY(QString previewStatus READ previewStatus NOTIFY previewChanged)   // "" | checking | ok | error | novideo
    Q_PROPERTY(QString previewError READ previewError NOTIFY previewChanged)
public:
    GlslController(uve::IPlayerHost *host, const QString &pluginId, QObject *parent = nullptr);

    void activate();
    void suspend();
    void onShaderError(const QString &message);
    void onEvent(const QString &name, const QVariantMap &data);

    QVariantList entries() const;
    bool masterEnabled() const { return m_master; }
    void setMasterEnabled(bool on);
    int activeCount() const;
    bool hasVideo() const;
    QString captureId() const { return m_captureId; }
    QString previewStatus() const { return m_previewStatus; }
    QString previewError() const { return m_previewError; }

    // Liste
    Q_INVOKABLE QVariantMap entry(const QString &id) const;
    Q_INVOKABLE QString addEntry(const QVariantMap &fields);
    Q_INVOKABLE bool updateEntry(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void removeEntry(const QString &id);
    Q_INVOKABLE QString duplicateEntry(const QString &id);
    Q_INVOKABLE void setEnabled(const QString &id, bool on);
    Q_INVOKABLE void moveEntry(int from, int to);
    Q_INVOKABLE void retry(const QString &id);

    // Editor
    Q_INVOKABLE QVariantList uniformDefs(const QString &code) const { return parseDefs(code); }
    Q_INVOKABLE QString check(const QString &code) const;
    // Probelauf im GPU-Treiber: leer = in Ordnung oder nicht prüfbar, sonst die Meldung des Treibers
    Q_INVOKABLE QString validate(const QVariantMap &fields) const;
    Q_INVOKABLE QString detect(const QString &code) const;
    Q_INVOKABLE bool isHook(const QString &code) const;
    Q_INVOKABLE void setPreview(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void clearPreview();

    // Austausch
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void copyText(const QString &text) const;
    // policy "" meldet nur doppelte Namen (dupes), "skip" oder "overwrite" führt den Import aus
    Q_INVOKABLE QVariantMap importText(const QString &text, const QString &policy);
    // Datei einlesen: {text, label, isJson} oder {error}
    Q_INVOKABLE QVariantMap readImportFile(const QString &urlOrPath) const;
    Q_INVOKABLE QVariantMap exportFile(const QString &urlOrPath) const;
    Q_INVOKABLE QString suggestedExportName() const;

    // Tasten (wie im Userscript: eine Taste ohne Umschalter schaltet den Filter)
    Q_INVOKABLE void beginCapture(const QString &id);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void clearHotkey(const QString &id);

    QStringList appliedFilesForTest() const { return m_applied; }

signals:
    void entriesChanged();
    void captureChanged();
    void previewChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static QVariantList parseDefs(const QString &code);
    void load();
    void save() const;
    void changed();
    void rebuild();
    void processQueue();
    void cleanShaderDir();
    void updateHotkeyFilter();
    void setPreviewState(const QString &status, const QString &error);
    void reportError(const QString &id, const QString &text);
    bool preflightFailed(const QJsonObject &e, const QString &file);
    QString writeShaderFile(const QJsonObject &e);
    QString owner(int slot) const;
    int indexOf(const QString &id) const;
    QJsonObject sanitized(const QVariantMap &fields, QJsonObject base) const;

    uve::IPlayerHost *m_host;
    QString m_id;
    QList<QJsonObject> m_entries;
    QHash<QString, QString> m_errors;   // Filter-ID → Fehlermeldung dieser Sitzung
    bool m_master = true;
    bool m_active = false;
    bool m_filterInstalled = false;
    QString m_captureId;
    double m_fps = 30.0;

    // Plätze
    QStringList m_desired, m_applied;
    QStringList m_desiredIds, m_appliedIds;
    // Ergebnis des Probelaufs je Shaderdatei; geprüfte Dateien brauchen keine Pause zur Fehlerzuordnung
    QHash<QString, QString> m_preflight;
    QTimer m_queueTimer, m_rebuildTimer, m_previewTimer;

    // Vorschau aus dem Editor (ersetzt den Filter oder hängt sich hinten an)
    bool m_previewOn = false;
    QJsonObject m_preview;
    QString m_previewStatus, m_previewError;
};

} // namespace glslmgr
