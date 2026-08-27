#pragma once

#include <QList>
#include <QMainWindow>

#include "Actions.h"
#include "LocationTypes.h"
#include "RouteTypes.h"
#include "TelemetryTypes.h"

class QButtonGroup;
class QStackedWidget;
class QLabel;
class NavButton;
class DemoDataModel;
class RoutePage;
class QHideEvent;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class PageId {
        HealthMonitoring = 0,
        RoutePlanning,
        VideoMonitoring,
        SonarVisualization,
        DataManagement
    };

    explicit MainWindow(QWidget *parent = nullptr);
    void navigateTo(PageId page);
    PageId currentPage() const;
    void setVehiclePosition(const RoutePoint &position);
    void setPlannedPath(const RoutePath &path);
    void updateGpsLocation(const LocationFix &fix);
    void setGpsAvailable(bool available);

signals:
    void currentPageChanged(PageId page);
    void actionRequested(ActionId action);
    void monitoringSettingsChanged(const MonitoringSettings &settings);
    void deviceControlRequested(const DeviceControlParameters &parameters);
    void telemetryExported(const QString &filePath, int rowCount);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void toggleFullScreen();
    void applyWindowChrome();
    QWidget *createHeader();
    QWidget *createNavigation();
    void showActionHint(ActionId action);
    void syncMapOverlayGeometry();

    QStackedWidget *m_pages = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QList<NavButton *> m_navButtons;
    DemoDataModel *m_model = nullptr;
    RoutePage *m_routePage = nullptr;
    QWidget *m_mapHost = nullptr;
    QWidget *m_mapPrewarmCover = nullptr;
    class LocationController *m_locationController = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_actionHint = nullptr;
    bool m_firstShow = true;
    bool m_transitioning = false;
};
