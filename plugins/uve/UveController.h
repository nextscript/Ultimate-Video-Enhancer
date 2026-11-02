#pragma once
#include "engine/CubeLut.h"
#include "engine/UveParams.h"
#include "LutLibrary.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

namespace uve {
class IPlayerHost;
}

namespace uve::fx {

// Brücke zwischen Effekt-Parametern, Shader-Erzeugung und der QML-Oberfläche des Plugins
class UveController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList controls READ controls NOTIFY languageChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY valuesChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY valuesChanged)
    Q_PROPERTY(bool compare READ compare NOTIFY valuesChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString lutInfo READ lutInfo NOTIFY valuesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString pipelineInfo READ pipelineInfo NOTIFY valuesChanged)
    Q_PROPERTY(bool mediaPresetActive READ mediaPresetActive NOTIFY mediaPresetChanged)
    Q_PROPERTY(QString defaultPreset READ defaultPreset NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList lutProfiles READ lutProfiles NOTIFY libraryChanged)
    Q_PROPERTY(QString lutProfileInfo READ lutProfileInfo NOTIFY valuesChanged)
    Q_PROPERTY(QString librarySource READ librarySource NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryBusy READ libraryBusy NOTIFY libraryStatusChanged)
    Q_PROPERTY(QString libraryStatus READ libraryStatus NOTIFY libraryStatusChanged)
    Q_PROPERTY(QVariantList galleryItems READ galleryItems NOTIFY galleryChanged)
    Q_PROPERTY(QString galleryOriginal READ galleryOriginal NOTIFY galleryChanged)
    Q_PROPERTY(QString galleryInfo READ galleryInfo NOTIFY galleryChanged)

public:
    UveController(IPlayerHost *host, const QString &pluginId, QObject *parent = nullptr);

    QVariantMap values() const;
    QVariantList controls() const;
    QVariantList presets() const;
    QString activePreset() const { return m_activePreset; }
    bool enabled() const { return m_params.enabled; }
    bool compare() const { return m_params.compare; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString lutInfo() const;
    QString lastError() const { return m_lastError; }
    QString pipelineInfo() const;
    bool mediaPresetActive() const;
    QString defaultPreset() const { return m_defaultPreset; }
    QVariantList lutProfiles() const { return m_library->list(); }
    QString lutProfileInfo() const;
    QString librarySource() const { return m_library->source(); }
    bool libraryBusy() const { return m_library->busy(); }
    QString libraryStatus() const { return m_library->status(); }
    QVariantList galleryItems() const { return m_galleryItems; }
    QString galleryOriginal() const { return m_galleryOriginal; }
    QString galleryInfo() const { return m_galleryInfo; }

    const Params &params() const { return m_params; }
    bool eventFilter(QObject *o, QEvent *e) override;
    void releaseNetwork() { m_library->releaseNetwork(); }
    void setActive(bool active);        // Plugin aktiv/aus (activate/deactivate)
    void onMediaChanged(const QString &path);
    void onShaderError(const QString &message);

    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void beginGesture();    // Undo-Schritt für zusammenhängende Regler-Bewegung (Regler gedrückt)
    Q_INVOKABLE void endGesture();      // Regler losgelassen
    Q_INVOKABLE void resetValue(const QString &key);
    Q_INVOKABLE void resetSection(const QString &section);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void setEnabled(bool on);
    Q_INVOKABLE void toggleEnabled();
    Q_INVOKABLE void setCompare(bool on);
    Q_INVOKABLE void toggleCompare();
    Q_INVOKABLE void setCompareSplit(double split);
    Q_INVOKABLE bool applyPreset(const QString &name);
    Q_INVOKABLE bool savePreset(const QString &name);
    Q_INVOKABLE bool deletePreset(const QString &name);
    Q_INVOKABLE void setDefaultPreset(const QString &name);
    Q_INVOKABLE void setMediaPreset(bool on);           // aktuelles Preset für diese Datei merken
    Q_INVOKABLE bool loadLut(const QString &fileOrUrl);
    Q_INVOKABLE void clearLut();
    Q_INVOKABLE bool importProfile(const QString &fileOrUrl);
    Q_INVOKABLE bool exportProfile(const QString &fileOrUrl);
    Q_INVOKABLE void reenableShader();
    // UVE-LUT-Profile (4x5-Matrix) und GitHub-Bibliothek - Netzwerk nur auf Knopfdruck
    Q_INVOKABLE void setLutProfile(const QString &key);            // "" = keins
    Q_INVOKABLE void downloadLutPackage();                         // neuestes LUTsProfiles_v*.zip
    Q_INVOKABLE bool importLutProfiles(const QString &fileOrUrl);  // lokales .json/.zip
    Q_INVOKABLE void requestScroll(const QString &where) { emit scrollRequested(where); } // "top" | "bottom"
    Q_INVOKABLE void requestGallery() { emit galleryRequested(); } // Panel öffnet das Galerie-Fenster
    Q_INVOKABLE bool buildGallery();                              // LUT-Galerie aus dem aktuellen Videoframe
    Q_INVOKABLE void loadExampleProfile();                         // My_Profile.json von GitHub (+ benoetigtes LUT-Paket)

signals:
    void valuesChanged();
    void presetsChanged();
    void historyChanged();
    void errorChanged();
    void mediaPresetChanged();
    void libraryChanged();
    void libraryStatusChanged();
    void galleryChanged();
    void galleryRequested();
    void languageChanged();
    void scrollRequested(const QString &where);

private:
    void changed(bool recordUndo);
    void scheduleShader();
    void writeShader();
    void persist();
    void loadState();
    void loadPresets();
    bool reloadLut();
    void setError(const QString &e);
    Params presetParams(const QString &name, bool *found) const;
    static QString toLocal(const QString &fileOrUrl);
    bool applyProfileJson(const QByteArray &json, const QString &label, QString *error);

    IPlayerHost *m_host;
    LutLibrary *m_library = nullptr;
    QString m_id;
    Params m_params;
    QString m_activePreset;
    QString m_defaultPreset;
    QString m_mediaPath;
    QJsonObject m_passthrough;
    QJsonObject m_mediaPresets;           // Pfad → Preset-Name
    QList<QPair<QString, QJsonObject>> m_builtinPresets;
    QList<QPair<QString, QJsonObject>> m_userPresets;
    QList<Params> m_undo, m_redo;
    bool m_gestureOpen = false;
    std::unique_ptr<CubeLut> m_lut;
    QString m_lutLoadedFrom;
    QString m_lastError;
    QTimer m_shaderTimer;
    bool m_active = false;
    QString m_shaderSession;
    QVariantList m_galleryItems;
    QString m_galleryOriginal, m_galleryInfo;
    int m_galleryGen = 0;
    int m_shaderSeq = 0;
};

} // namespace uve::fx
