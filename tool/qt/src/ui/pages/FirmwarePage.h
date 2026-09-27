// FirmwarePage — what image is running and what it has been told about itself: TI_FW_ID against this build's
// protocol exports, the FW-20 calibration record and its byte layout, the FW-24 EOL/HIL validation record, the arming
// evidence, the FW-16 self-test record, the provisioning; and the FW-38 field update (FirmwareUpdater's logic): a
// signed .tifw image selected, its header shown and compared with the running image before anything is sent, a typed
// confirmation, then the sequence with its progress and every refusal's meaning.
#pragma once

#include "FirmwareUpdater.h"
#include "Page.h"

class CheckList;
class KvGrid;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

class FirmwarePage : public Page
{
    Q_OBJECT
public:
    explicit FirmwarePage(Session &s, QWidget *parent = nullptr);
    bool loadImage(const QString &path); // the header shown; the running image read and compared (false: unusable file)
    FirmwareUpdater &updater() { return m_up; }

protected:
    void showEvent(QShowEvent *e) override;

private:
    void onInfo(const QJsonObject &info);
    void onFrame(const TelemetryFrame &f);
    void selectImage();
    void confirmUpdate();
    void renderUpdate();

    QLabel *m_fwPill, *m_canNote;
    KvGrid *m_ident, *m_calib, *m_valid, *m_self;
    QTableWidget *m_layout;
    CheckList *m_evidence;
    FirmwareUpdater m_up;
    FirmwareUpdater::Image m_img;
    KvGrid *m_imgGrid, *m_runGrid, *m_progress;
    QLabel *m_upPill, *m_warn, *m_outcome;
    QPushButton *m_update;
    QProgressBar *m_bar;
};
