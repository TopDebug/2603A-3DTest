#pragma once

#include <QMainWindow>
#include <QString>

#include <Qt>

class QCloseEvent;
class QEvent;
class QResizeEvent;

class QWindow;
class QVulkanInstance;
class QDoubleSpinBox;
class QLabel;
class QToolButton;
class QTimer;
class QAction;
class Application;

class MainWindow final : public QMainWindow {
public:
    MainWindow(Application* app, QVulkanInstance* vulkanInstance, QWidget* parent = nullptr);

    QWindow* vulkanWindow() const { return _vkWindow; }

    void updateReadoutLabel(const QString& text);
    void syncViewPanelFromApp();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenuBar();
    void buildViewOverlayPanel();
    void applyViewPanelToApp();

    void relayoutCentralOverlays();
    void logVulkanSurfaceSize(const char* reason) const;
    void scheduleViewDockAutoHide();
    void cancelViewDockAutoHide();
    void revealViewDockFromAutoHide();
    void syncViewPanelMenuCheck();
    void applyAutoHideFromUi(bool on);

    Application* _app = nullptr;
    QVulkanInstance* _vulkanInstance = nullptr;
    QWindow* _vkWindow = nullptr;
    QLabel* _readoutLabel = nullptr;

    QDoubleSpinBox* _coordX = nullptr;
    QDoubleSpinBox* _coordY = nullptr;
    QDoubleSpinBox* _coordZ = nullptr;
    QDoubleSpinBox* _rotX = nullptr;
    QDoubleSpinBox* _rotY = nullptr;
    QDoubleSpinBox* _orthoL = nullptr;
    QDoubleSpinBox* _orthoR = nullptr;
    QDoubleSpinBox* _orthoB = nullptr;
    QDoubleSpinBox* _orthoT = nullptr;
    QDoubleSpinBox* _viewportW = nullptr;
    QDoubleSpinBox* _viewportH = nullptr;

    QWidget* _viewOverlayPanel = nullptr;
    QWidget* _centralStack = nullptr;
    QWidget* _vkContainer = nullptr;
    QToolButton* _autoHidePeekTab = nullptr;
    QTimer* _viewAutoHideTimer = nullptr;
    bool _viewAutoHideEnabled = false;
    bool _panelOnLeft = true;
    int _viewPanelWidth = 220;
    QAction* _actViewPanelToggle = nullptr;
    QAction* _actAutoHidePanel = nullptr;
    QToolButton* _viewDockPinButton = nullptr;
};
