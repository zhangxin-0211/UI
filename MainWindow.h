#pragma once

#include <QList>
#include <QMainWindow>

#include "Actions.h"
#include "TelemetryTypes.h"

class QButtonGroup;
class QStackedWidget;
class QLabel;
class NavButton;
class DemoDataModel;
class QHideEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class PageId {
        RoutePlanning = 0,
        HealthMonitoring,
        VideoMonitoring,
        DataManagement
    };

    explicit MainWindow(QWidget *parent = nullptr);
    void navigateTo(PageId page);
    PageId currentPage() const;

signals:
    void currentPageChanged(PageId page);
    void actionRequested(ActionId action);
    void monitoringSettingsChanged(const MonitoringSettings &settings);
    void deviceControlRequested(const DeviceControlParameters &parameters);
    void telemetryExported(const QString &filePath, int rowCount);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void toggleFullScreen();
    QWidget *createHeader();
    QWidget *createNavigation();
    void showActionHint(ActionId action);

    QStackedWidget *m_pages = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QList<NavButton *> m_navButtons;
    DemoDataModel *m_model = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_actionHint = nullptr;
    bool m_firstShow = true;
    bool m_transitioning = false;
};
