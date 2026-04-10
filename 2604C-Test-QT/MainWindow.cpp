#include "MainWindow.h"
#include "Application.h"

#include <QApplication>
#include <QCloseEvent>
#include <QAction>
#include <QDialog>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVulkanInstance>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

static double roundDepthWindow1(double v) {
    return std::round(v * 10.0) / 10.0;
}

static std::pair<double, double> computeDepthDataRange(const std::vector<float>& d) {
    if (d.empty()) {
        return { 0.0, 1.0 };
    }
    float mn = d[0];
    float mx = d[0];
    for (float v : d) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (!(static_cast<double>(mx) > static_cast<double>(mn) + 1e-20)) {
        mx = mn + 1e-3f;
    }
    return { static_cast<double>(mn), static_cast<double>(mx) };
}

/** Values below min map to black, above max to white; linear between. */
static void depthFloatToRgbaWindow(
    const std::vector<float>& depth,
    int iw,
    int ih,
    double wmin,
    double wmax,
    QImage& out) {
    double span = wmax - wmin;
    if (span <= 1e-20) {
        span = 1e-6;
    }
    out = QImage(iw, ih, QImage::Format_RGBA8888);
    for (int y = 0; y < ih; ++y) {
        uchar* row = out.scanLine(y);
        for (int x = 0; x < iw; ++x) {
            const float d = depth[static_cast<size_t>(y) * static_cast<size_t>(iw) + static_cast<size_t>(x)];
            double t = (static_cast<double>(d) - wmin) / span;
            t = std::clamp(t, 0.0, 1.0);
            const int g = static_cast<int>(std::lround(t * 255.0));
            const uchar u = static_cast<uchar>(std::clamp(g, 0, 255));
            row[x * 4 + 0] = u;
            row[x * 4 + 1] = u;
            row[x * 4 + 2] = u;
            row[x * 4 + 3] = 255;
        }
    }
}

/**
 * Depth contrast window on one slider:
 *   MIN — MAX   slide endpoints (full range, from data min/max ± pad)
 *   WL, WR      window positions on that slide (left/right contrast limits)
 * Slide spans data min/max ± pad, expanded to include WL (left) and WR (right) when outside that band.
 */
class DepthWindowStripWidget final : public QWidget {
public:
    /** Third arg false while dragging (fast); true on mouse release (apply full image). */
    using WindowCallback = std::function<void(double wmin, double wmax, bool committed)>;

    DepthWindowStripWidget(QWidget* parent, double dataMin, double dataMax, WindowCallback cb)
        : QWidget(parent)
        , _dataMin(dataMin)
        , _dataMax(dataMax)
        , _onWindowChanged(std::move(cb)) {
        setMinimumWidth(320);
        setFixedHeight(40);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAutoFillBackground(false);
    }

    void setWindowValues(double wmin, double wmax) {
        _WL = wmin;
        _WR = wmax;
        if (_WL > _WR) {
            std::swap(_WL, _WR);
        }
        update();
    }

    void setDataRange(double dmin, double dmax) {
        _dataMin = dmin;
        _dataMax = dmax;
        update();
    }

    bool isDragging() const {
        return _dragging;
    }

    /** Called at drag start so strip WL/WR match spin text (including uncommitted edits). */
    void setBeforeDragSync(std::function<void()> fn) {
        _beforeDragSync = std::move(fn);
    }

    QRect barRect() const {
        const int w = std::max(2, width() - 8);
        return QRect(4, 8, w, height() - 16);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Mid/dark gray slider (avoid near-white chrome).
        const QColor kFrame(0xA8, 0xA8, 0xA8);
        const QColor kTrack(0x8A, 0x8A, 0x8A);
        const QColor kTrackBorder(0x6A, 0x6A, 0x6A);
        const QColor kMark(0x2C, 0x2C, 0x2C);

        p.fillRect(rect(), kFrame);
        const QRect bar = barRect();
        p.setPen(QPen(kTrackBorder, 1));
        p.setBrush(kTrack);
        p.drawRect(bar);

        const double MINv = slideMin();
        const double MAXv = slideMax();
        if (!(MAXv > MINv)) {
            return;
        }

        auto xOf = [&](double v) -> int {
            double t = (v - MINv) / (MAXv - MINv);
            t = std::clamp(t, 0.0, 1.0);
            return bar.left() + static_cast<int>(std::lround(t * static_cast<double>(bar.width() - 1)));
        };

        double wl = _WL;
        double wr = _WR;
        if (wl > wr) {
            std::swap(wl, wr);
        }
        const int xWl = xOf(wl);
        const int xWr = xOf(wr);
        const int xl = std::min(xWl, xWr);
        const int xr = std::max(xWl, xWr);

        p.setPen(QPen(kMark, 2));
        p.drawLine(xl, bar.top(), xl, bar.bottom());
        p.drawLine(xr, bar.top(), xr, bar.bottom());
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (!barRect().contains(e->pos())) {
            return;
        }
        if (_beforeDragSync) {
            _beforeDragSync();
        }
        const QRect bar = barRect();
        const double MINv = slideMin();
        const double MAXv = slideMax();
        if (!(MAXv > MINv) || bar.width() < 2) {
            return;
        }
        auto xOf = [&](double v) -> int {
            double t = (v - MINv) / (MAXv - MINv);
            t = std::clamp(t, 0.0, 1.0);
            return bar.left() + static_cast<int>(std::lround(t * static_cast<double>(bar.width() - 1)));
        };
        double wl = _WL;
        double wr = _WR;
        if (wl > wr) {
            std::swap(wl, wr);
        }
        const int xWl = xOf(wl);
        const int xWr = xOf(wr);
        const int xl = std::min(xWl, xWr);
        const int xr = std::max(xWl, xWr);
        const int tol = 10;
        const int px = e->pos().x();
        if (px > xl + tol && px < xr - tol) {
            _dragMode = DragMode::Pan;
        }
        else {
            _dragMode = (std::abs(px - xWl) <= std::abs(px - xWr))
                ? DragMode::Left
                : DragMode::Right;
        }
        _dragging = true;
        _lastX = e->pos().x();
        grabMouse();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QRect bar = barRect();
        const double MINv = slideMin();
        const double MAXv = slideMax();

        if (!_dragging) {
            if (!bar.contains(e->pos()) || !(MAXv > MINv) || bar.width() < 2) {
                setCursor(Qt::ArrowCursor);
                QWidget::mouseMoveEvent(e);
                return;
            }
            auto xOf = [&](double v) -> int {
                double t = (v - MINv) / (MAXv - MINv);
                t = std::clamp(t, 0.0, 1.0);
                return bar.left() + static_cast<int>(std::lround(t * static_cast<double>(bar.width() - 1)));
            };
            double wl = _WL;
            double wr = _WR;
            if (wl > wr) {
                std::swap(wl, wr);
            }
            const int xWl = xOf(wl);
            const int xWr = xOf(wr);
            const int xl = std::min(xWl, xWr);
            const int xr = std::max(xWl, xWr);
            const int tol = 10;
            const int px = e->pos().x();
            const bool nearHandle = (std::abs(px - xWl) <= tol) || (std::abs(px - xWr) <= tol);
            const bool inCenter = (px > xl + tol && px < xr - tol);
            setCursor((nearHandle || inCenter) ? Qt::SizeHorCursor : Qt::ArrowCursor);
            QWidget::mouseMoveEvent(e);
            return;
        }
        if (!(MAXv > MINv) || bar.width() < 2) {
            QWidget::mouseMoveEvent(e);
            return;
        }

        auto valAt = [&](int x) -> double {
            const int cx = std::clamp(x, bar.left(), bar.right());
            double t = static_cast<double>(cx - bar.left()) / static_cast<double>(std::max(1, bar.width() - 1));
            t = std::clamp(t, 0.0, 1.0);
            return MINv + t * (MAXv - MINv);
        };

        const double v = valAt(e->pos().x());
        const double v0 = valAt(_lastX);
        const double dv = v - v0;
        _lastX = e->pos().x();

        double wl = _WL;
        double wr = _WR;
        if (wl > wr) {
            std::swap(wl, wr);
        }
        if (_dragMode == DragMode::Pan) {
            const double span = MAXv - MINv;
            if (span <= 1e-18) {
                QWidget::mouseMoveEvent(e);
                return;
            }
            double w = std::max(wr - wl, 1e-9);
            if (w >= span - 1e-12) {
                wl = MINv;
                wr = MAXv;
            }
            else {
                w = std::min(w, span);
                wl = std::clamp(wl + dv, MINv, MAXv - w);
                wr = wl + w;
            }
        }
        else if (_dragMode == DragMode::Left) {
            wl = std::clamp(v, MINv, wr - 1e-9);
        }
        else {
            wr = std::clamp(v, wl + 1e-9, MAXv);
        }

        _WL = wl;
        _WR = wr;
        if (_onWindowChanged) {
            _onWindowChanged(wl, wr, false);
        }
        update();
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            if (mouseGrabber() == this) {
                releaseMouse();
            }
            if (_dragging && _onWindowChanged) {
                double wl = _WL;
                double wr = _WR;
                if (wl > wr) {
                    std::swap(wl, wr);
                }
                _onWindowChanged(wl, wr, true);
            }
            _dragging = false;
            _dragMode = DragMode::None;
            setCursor(Qt::ArrowCursor);
        }
        QWidget::mouseReleaseEvent(e);
    }

private:
    enum class DragMode { None, Left, Right, Pan };

    static double axisPad(double dataMin, double dataMax) {
        return 0.05 * (dataMax - dataMin + 1e-9);
    }

    /**
     * Slide value range: default data min/max ± pad, expanded so WL is never left of the bar
     * and WR is never right of the bar (ordered WL ≤ WR).
     */
    double slideMin() const {
        const double pad = axisPad(_dataMin, _dataMax);
        const double baseMin = _dataMin - pad;
        double wl = _WL;
        double wr = _WR;
        if (wl > wr) {
            std::swap(wl, wr);
        }
        return std::min(baseMin, wl);
    }

    double slideMax() const {
        const double pad = axisPad(_dataMin, _dataMax);
        const double baseMax = _dataMax + pad;
        double wl = _WL;
        double wr = _WR;
        if (wl > wr) {
            std::swap(wl, wr);
        }
        return std::max(baseMax, wr);
    }

    double _dataMin = 0.0;
    double _dataMax = 1.0;
    double _WL = 0.0;
    double _WR = 1.0;
    bool _dragging = false;
    DragMode _dragMode = DragMode::None;
    int _lastX = 0;
    WindowCallback _onWindowChanged;
    std::function<void()> _beforeDragSync;
};

/** Depth preview: WL/WR spins, slider strip, hover readout (no caption), Save. */
class DepthBufferViewDialog final : public QDialog {
public:
    DepthBufferViewDialog(QWidget* parent, std::vector<float> depth, int iw, int ih, QString suggestedFileName)
        : QDialog(parent)
        , _depth(std::move(depth))
        , _iw(iw)
        , _ih(ih)
        , _suggestedFileName(std::move(suggestedFileName)) {
        setModal(false);
        setMinimumSize(400, 320);

        const auto rng = computeDepthDataRange(_depth);
        _dataMin = rng.first;
        _dataMax = rng.second;

        _windowLSpin = new QDoubleSpinBox(this);
        _windowRSpin = new QDoubleSpinBox(this);
        const QString spinGrayStyle = QStringLiteral(
            "QDoubleSpinBox { background-color: #E8E8E8; color: #1a1a1a; border: 1px solid #9E9E9E; "
            "border-radius: 2px; padding: 2px 6px; }");
        for (auto* s : { _windowLSpin, _windowRSpin }) {
            s->setRange(-1e9, 1e9);
            s->setDecimals(1);
            s->setSingleStep(0.1);
            s->setButtonSymbols(QAbstractSpinBox::NoButtons);
            s->setFixedWidth(100);
            s->setStyleSheet(spinGrayStyle);
        }
        _windowLSpin->setValue(roundDepthWindow1(static_cast<double>(rng.first)));
        _windowRSpin->setValue(roundDepthWindow1(static_cast<double>(rng.second)));

        auto* saveBtn = new QPushButton(QStringLiteral("Save…"), this);
        saveBtn->setAutoDefault(false);
        saveBtn->setDefault(false);
        saveBtn->setFocusPolicy(Qt::NoFocus);
        QObject::connect(saveBtn, &QPushButton::clicked, this, &DepthBufferViewDialog::savePng);

        _stripImageThrottleTimer = new QTimer(this);
        _stripImageThrottleTimer->setSingleShot(true);
        _stripImageThrottleTimer->setInterval(16);
        QObject::connect(_stripImageThrottleTimer, &QTimer::timeout, this, [this]() {
            if (!_stripImageDirtyFromDrag) {
                return;
            }
            _stripImageDirtyFromDrag = false;
            rebuildImage();
        });

        _strip = new DepthWindowStripWidget(this, _dataMin, _dataMax, [this](double wmin, double wmax, bool committed) {
            const QSignalBlocker b0(_windowLSpin);
            const QSignalBlocker b1(_windowRSpin);
            _windowLSpin->setValue(roundDepthWindow1(wmin));
            _windowRSpin->setValue(roundDepthWindow1(wmax));
            if (committed) {
                if (_stripImageThrottleTimer) {
                    _stripImageThrottleTimer->stop();
                }
                _stripImageDirtyFromDrag = false;
                rebuildImage();
                return;
            }
            _stripImageDirtyFromDrag = true;
            if (_stripImageThrottleTimer && !_stripImageThrottleTimer->isActive()) {
                _stripImageThrottleTimer->start();
            }
        });
        _strip->setBeforeDragSync([this]() {
            bool okL = true;
            bool okR = true;
            double wl = _windowLSpin->text().toDouble(&okL);
            double wr = _windowRSpin->text().toDouble(&okR);
            if (!okL) {
                wl = _windowLSpin->value();
            }
            if (!okR) {
                wr = _windowRSpin->value();
            }
            wl = roundDepthWindow1(wl);
            wr = roundDepthWindow1(wr);
            if (wl > wr) {
                std::swap(wl, wr);
            }
            {
                const QSignalBlocker b0(_windowLSpin);
                const QSignalBlocker b1(_windowRSpin);
                _windowLSpin->setValue(wl);
                _windowRSpin->setValue(wr);
            }
            _strip->setWindowValues(wl, wr);
        });
        _strip->setWindowValues(_windowLSpin->value(), _windowRSpin->value());

        _readoutLabel = new QLabel(QStringLiteral("—"), this);
        _readoutLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        _readoutLabel->setMinimumWidth(100);
        _readoutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        _scroll = new QScrollArea(this);
        _scroll->setWidgetResizable(false);
        _scroll->setAlignment(Qt::AlignCenter);
        _scroll->setBackgroundRole(QPalette::Dark);
        _scroll->viewport()->setStyleSheet(QStringLiteral("background-color: #505050;"));

        _imageLabel = new QLabel;
        _imageLabel->setMouseTracking(true);
        _imageLabel->installEventFilter(this);
        _scroll->setWidget(_imageLabel);

        auto* controls = new QHBoxLayout;
        controls->setSpacing(8);
        controls->addWidget(new QLabel(QStringLiteral("WL"), this));
        controls->addWidget(_windowLSpin);
        controls->addSpacing(10);
        controls->addWidget(new QLabel(QStringLiteral("WR"), this));
        controls->addWidget(_windowRSpin);
        controls->addSpacing(14);
        controls->addWidget(_strip, 1);
        controls->addSpacing(16);
        controls->addWidget(_readoutLabel);
        controls->addWidget(saveBtn);

        auto* root = new QVBoxLayout(this);
        root->addWidget(_scroll, 1);
        root->addLayout(controls);

        QObject::connect(_windowLSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            rebuildImage();
        });
        QObject::connect(_windowRSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            rebuildImage();
        });
        for (auto* s : { _windowLSpin, _windowRSpin }) {
            if (QLineEdit* le = s->findChild<QLineEdit*>()) {
                QObject::connect(le, &QLineEdit::textChanged, this, [this]() {
                    syncSliderFromSpinText();
                });
            }
            QObject::connect(s, &QDoubleSpinBox::editingFinished, this, [this]() {
                rebuildImage();
            });
        }

        rebuildImage();

        const int guessW = std::clamp(_iw + 80, 480, 1000);
        const int guessH = std::clamp(_ih + 100, 360, 900);
        resize(guessW, guessH);
    }

    /** Replace captured depth (e.g. each time Export → Depth Buffer is chosen). */
    void reloadDepthData(std::vector<float>&& depth, int iw, int ih, QString suggestedFileName) {
        if (_stripImageThrottleTimer) {
            _stripImageThrottleTimer->stop();
        }
        _stripImageDirtyFromDrag = false;

        _depth = std::move(depth);
        _iw = iw;
        _ih = ih;
        _suggestedFileName = std::move(suggestedFileName);

        const auto rng = computeDepthDataRange(_depth);
        _dataMin = rng.first;
        _dataMax = rng.second;
        if (_strip) {
            _strip->setDataRange(_dataMin, _dataMax);
        }
        {
            const QSignalBlocker b0(_windowLSpin);
            const QSignalBlocker b1(_windowRSpin);
            _windowLSpin->setValue(roundDepthWindow1(static_cast<double>(rng.first)));
            _windowRSpin->setValue(roundDepthWindow1(static_cast<double>(rng.second)));
        }
        if (_strip) {
            _strip->setWindowValues(_windowLSpin->value(), _windowRSpin->value());
        }
        rebuildImage();
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        hide();
        e->ignore();
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != _imageLabel) {
            return QDialog::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            const auto* me = static_cast<QMouseEvent*>(event);
            const int ix = me->pos().x();
            const int iy = me->pos().y();
            if (ix < 0 || iy < 0 || ix >= _iw || iy >= _ih) {
                _readoutLabel->setText(QStringLiteral("—"));
                return false;
            }
            const float pv = _depth[static_cast<size_t>(iy) * static_cast<size_t>(_iw) + static_cast<size_t>(ix)];
            _readoutLabel->setText(
                QStringLiteral("%1 %2 %3")
                    .arg(ix)
                    .arg(iy)
                    .arg(static_cast<double>(pv), 0, 'f', 1));
            return false;
        }
        if (event->type() == QEvent::Leave) {
            _readoutLabel->setText(QStringLiteral("—"));
            return false;
        }
        return QDialog::eventFilter(watched, event);
    }

private:
    /** Move strip handles while typing (before spin commits). One decimal, same as spins. */
    void syncSliderFromSpinText() {
        if (!_strip || _strip->isDragging()) {
            return;
        }
        bool okL = true;
        bool okR = true;
        double wl = _windowLSpin->text().toDouble(&okL);
        double wr = _windowRSpin->text().toDouble(&okR);
        if (!okL) {
            wl = _windowLSpin->value();
        }
        if (!okR) {
            wr = _windowRSpin->value();
        }
        wl = roundDepthWindow1(wl);
        wr = roundDepthWindow1(wr);
        if (wl > wr) {
            std::swap(wl, wr);
        }
        _strip->setWindowValues(wl, wr);
    }

    void rebuildImage() {
        double mn = roundDepthWindow1(_windowLSpin->value());
        double mx = roundDepthWindow1(_windowRSpin->value());
        if (!(mx > mn)) {
            mx = mn + 1e-9;
        }
        // Avoid feeding values back into the strip while it is actively dragging;
        // this removes visible flicker/jitter from redundant strip repaints.
        if (_strip && !_strip->isDragging()) {
            _strip->setWindowValues(mn, mx);
        }
        depthFloatToRgbaWindow(_depth, _iw, _ih, mn, mx, _image);
        _imageLabel->setPixmap(QPixmap::fromImage(_image));
        _imageLabel->resize(_iw, _ih);
    }

    void savePng() {
        auto restoreFocus = [this]() {
            QTimer::singleShot(0, this, [this]() {
                raise();
                activateWindow();
                if (_strip) {
                    _strip->setFocus(Qt::OtherFocusReason);
                }
            });
        };

        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Save depth view"),
            _suggestedFileName,
            QStringLiteral("PNG (*.png);;All files (*)"));
        if (path.isEmpty()) {
            restoreFocus();
            return;
        }
        if (!_image.save(path, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("Save"), QStringLiteral("Failed to save the image."));
        }
        restoreFocus();
    }

    std::vector<float> _depth;
    int _iw = 0;
    int _ih = 0;
    QString _suggestedFileName;
    QImage _image;
    QScrollArea* _scroll = nullptr;
    QLabel* _imageLabel = nullptr;
    QLabel* _readoutLabel = nullptr;
    QDoubleSpinBox* _windowLSpin = nullptr;
    QDoubleSpinBox* _windowRSpin = nullptr;
    DepthWindowStripWidget* _strip = nullptr;
    double _dataMin = 0.0;
    double _dataMax = 1.0;

    QTimer* _stripImageThrottleTimer = nullptr;
    bool _stripImageDirtyFromDrag = false;
};

void showDepthBufferViewer(QWidget* parent, Application* app) {
    static QPointer<DepthBufferViewDialog> s_dialog;

    std::vector<float> depth;
    uint32_t w = 0;
    uint32_t h = 0;
    if (!app->captureDepthBufferFloat01(depth, w, h) || w == 0 || h == 0) {
        QMessageBox::warning(
            parent,
            QStringLiteral("Depth Buffer"),
            QStringLiteral("Could not read the depth buffer (Vulkan may not be ready)."));
        return;
    }
    const QString suggested = QStringLiteral("DepthBuffer_%1_%2.png").arg(w).arg(h);
    const QString titleDetail = QStringLiteral("Depth Buffer — %1×%2").arg(w).arg(h);

    if (s_dialog) {
        s_dialog->reloadDepthData(std::move(depth), static_cast<int>(w), static_cast<int>(h), suggested);
        s_dialog->setWindowTitle(titleDetail);
        s_dialog->show();
        s_dialog->raise();
        s_dialog->activateWindow();
        return;
    }
    s_dialog = new DepthBufferViewDialog(parent, std::move(depth), static_cast<int>(w), static_cast<int>(h), suggested);
    s_dialog->setWindowTitle(titleDetail);
    s_dialog->show();
}

/** VS-style dock pin: vertical = pinned (stay open), horizontal = auto-hide. */
static QIcon makeDockPinIcon(bool autoHideOn) {
    constexpr QSize sz(16, 16);
    QPixmap pm(sz);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255), 2.0));
    if (autoHideOn) {
        painter.drawLine(2, 8, 14, 8);
        painter.drawLine(2, 5, 2, 11);
    } else {
        painter.drawLine(8, 2, 8, 14);
        painter.drawLine(5, 4, 11, 4);
    }
    return QIcon(pm);
}

static void syncDockPinButtonState(QToolButton* pin, bool autoHideOn) {
    if (!pin) {
        return;
    }
    const QSignalBlocker b(pin);
    pin->setChecked(autoHideOn);
    pin->setIcon(makeDockPinIcon(autoHideOn));
    pin->setToolTip(autoHideOn ? QStringLiteral("Pin open (disable auto hide)")
                               : QStringLiteral("Auto Hide"));
}

/** VS-style title strip for the floating overlay panel (Vulkan renders underneath). */
class ViewOverlayTitleBar final : public QWidget {
public:
    using AutoHideFn = std::function<void(bool)>;

    ViewOverlayTitleBar(const QString& titleText, AutoHideFn onAutoHide, QWidget* parent)
        : QWidget(parent)
        , _onAutoHide(std::move(onAutoHide)) {
        setObjectName(QStringLiteral("viewOverlayTitleBar"));
        setFixedHeight(28);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setStyleSheet(QStringLiteral(
            "QWidget#viewOverlayTitleBar { background-color: #007ACC; border: none; }"
            "QWidget#viewOverlayTitleBar QToolButton { background: transparent; border: none; padding: 2px; }"
            "QWidget#viewOverlayTitleBar QToolButton:hover { background-color: rgba(255,255,255,0.14); "
            "border-radius: 2px; }"
            "QWidget#viewOverlayTitleBar QLabel { color: #FFFFFF; font-weight: 600; font-size: 13px; }"));

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 0, 4, 0);
        lay->setSpacing(4);

        auto* title = new QLabel(titleText, this);
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        title->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        _pin = new QToolButton(this);
        _pin->setCheckable(true);
        _pin->setChecked(false);
        _pin->setAutoRaise(true);
        _pin->setFixedSize(26, 22);
        syncDockPinButtonState(_pin, false);
        QObject::connect(_pin, &QToolButton::toggled, this, [this](bool autoHideOn) {
            if (_onAutoHide) {
                _onAutoHide(autoHideOn);
            }
        });

        lay->addWidget(title, 1);
        lay->addWidget(_pin, 0, Qt::AlignVCenter);
    }

    QToolButton* pinButton() const {
        return _pin;
    }

private:
    AutoHideFn _onAutoHide;
    QToolButton* _pin = nullptr;
};

} // namespace

MainWindow::MainWindow(Application* app, QVulkanInstance* vulkanInstance, QWidget* parent)
    : QMainWindow(parent)
    , _app(app)
    , _vulkanInstance(vulkanInstance) {
    setWindowTitle(QStringLiteral("MINGJIE2026@GMAIL.COM"));

    // createWindowContainer embeds a native Vulkan window; on X11/Wayland it usually paints above
    // overlapping Qt siblings, so an overlaid panel is invisible. We lay out the panel/peek in a
    // non-overlapping strip and shrink the VK container to the remaining rect (see relayoutCentralOverlays).
    _centralStack = new QWidget(this);
    _centralStack->setObjectName(QStringLiteral("centralRenderHost"));
    setCentralWidget(_centralStack);

    _vkWindow = new QWindow();
    _vkWindow->setSurfaceType(QWindow::VulkanSurface);
    _vkWindow->setVulkanInstance(_vulkanInstance);

    _vkContainer = QWidget::createWindowContainer(_vkWindow, _centralStack);
    _vkContainer->setFocusPolicy(Qt::StrongFocus);

    _autoHidePeekTab = new QToolButton(_centralStack);
    _autoHidePeekTab->setObjectName(QStringLiteral("viewAutoHidePeekTab"));
    _autoHidePeekTab->setText(QStringLiteral("View"));
    _autoHidePeekTab->setToolTip(QStringLiteral("Show view parameters (auto-hide)"));
    _autoHidePeekTab->setAutoRaise(true);
    _autoHidePeekTab->setCheckable(false);
    _autoHidePeekTab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    _autoHidePeekTab->setFixedWidth(22);
    _autoHidePeekTab->hide();
    _autoHidePeekTab->setStyleSheet(QStringLiteral(
        "QToolButton#viewAutoHidePeekTab {"
        "  background-color: #3C3C3C;"
        "  color: #E0E0E0;"
        "  border: 1px solid #555;"
        "  border-radius: 0px;"
        "  font-size: 11px;"
        "  padding: 4px 2px;"
        "}"
        "QToolButton#viewAutoHidePeekTab:hover { background-color: #505050; }"));
    QObject::connect(_autoHidePeekTab, &QToolButton::clicked, this, [this]() {
        revealViewDockFromAutoHide();
    });
    _autoHidePeekTab->installEventFilter(this);

    _viewAutoHideTimer = new QTimer(this);
    _viewAutoHideTimer->setSingleShot(true);
    _viewAutoHideTimer->setInterval(350);
    QObject::connect(_viewAutoHideTimer, &QTimer::timeout, this, [this]() {
        if (!_viewAutoHideEnabled || !_viewOverlayPanel) {
            return;
        }
        if (!_viewOverlayPanel->isVisible()) {
            return;
        }
        _viewOverlayPanel->hide();
        _autoHidePeekTab->show();
        relayoutCentralOverlays();
        syncViewPanelMenuCheck();
    });

    buildViewOverlayPanel();
    buildMenuBar();

    resize(1280, 840);
    relayoutCentralOverlays();
}

void MainWindow::buildMenuBar() {
    _readoutLabel = new QLabel(QStringLiteral(" "));
    _readoutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _readoutLabel->setMinimumWidth(320);
    _readoutLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    menuBar()->setCornerWidget(_readoutLabel, Qt::TopRightCorner);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    QObject::connect(fileMenu->addAction(QStringLiteral("Open…")), &QAction::triggered, this, [this]() {
        if (!_app) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Open OBJ"),
            QString(),
            QStringLiteral("Wavefront OBJ (*.obj);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive)) {
            QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Please select an .obj file."));
            return;
        }
        _app->openObjFile(path.toStdString());
    });
    QObject::connect(fileMenu->addAction(QStringLiteral("Cube")), &QAction::triggered, this, [this]() {
        if (_app) {
            _app->showCubeMesh();
        }
    });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    QObject::connect(viewMenu->addAction(QStringLiteral("Front View")), &QAction::triggered, this, [this]() {
        if (_app) {
            _app->setViewRotation(0.0f, 0.0f);
        }
    });
    QObject::connect(viewMenu->addAction(QStringLiteral("Right View")), &QAction::triggered, this, [this]() {
        if (_app) {
            _app->setViewRotation(0.0f, -90.0f);
        }
    });
    QObject::connect(viewMenu->addAction(QStringLiteral("Top View")), &QAction::triggered, this, [this]() {
        if (_app) {
            _app->setViewRotation(90.0f, 0.0f);
        }
    });

    viewMenu->addSeparator();
    _actViewPanelToggle = viewMenu->addAction(QStringLiteral("View parameters panel"));
    _actViewPanelToggle->setCheckable(true);
    _actViewPanelToggle->setChecked(true);
    QObject::connect(_actViewPanelToggle, &QAction::toggled, this, [this](bool on) {
        cancelViewDockAutoHide();
        if (!_viewOverlayPanel) {
            return;
        }
        if (on) {
            _autoHidePeekTab->hide();
            _viewOverlayPanel->show();
        } else {
            _autoHidePeekTab->hide();
            _viewOverlayPanel->hide();
        }
        relayoutCentralOverlays();
    });

    QObject::connect(viewMenu->addAction(QStringLiteral("Panel on left")), &QAction::triggered, this, [this]() {
        cancelViewDockAutoHide();
        _panelOnLeft = true;
        relayoutCentralOverlays();
    });
    QObject::connect(viewMenu->addAction(QStringLiteral("Panel on right")), &QAction::triggered, this, [this]() {
        cancelViewDockAutoHide();
        _panelOnLeft = false;
        relayoutCentralOverlays();
    });

    _actAutoHidePanel = viewMenu->addAction(QStringLiteral("Auto-hide view panel"));
    _actAutoHidePanel->setCheckable(true);
    QObject::connect(_actAutoHidePanel, &QAction::toggled, this, [this](bool on) {
        applyAutoHideFromUi(on);
    });

    QMenu* exportMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    QObject::connect(exportMenu->addAction(QStringLiteral("Depth Buffer")), &QAction::triggered, this, [this]() {
        if (_app) {
            showDepthBufferViewer(this, _app);
        }
    });

    QMenu* testMenu = menuBar()->addMenu(QStringLiteral("Test"));
    QObject::connect(testMenu->addAction(QStringLiteral("lscm")), &QAction::triggered, this, [this]() {
        if (_app) {
            _app->runLscmAndShowMesh();
        }
    });
}

void MainWindow::buildViewOverlayPanel() {
    _viewOverlayPanel = new QWidget(_centralStack);
    _viewOverlayPanel->setObjectName(QStringLiteral("viewParametersOverlay"));
    _viewOverlayPanel->setMouseTracking(true);
    _viewOverlayPanel->setAttribute(Qt::WA_Hover, true);
    _viewOverlayPanel->setStyleSheet(QStringLiteral(
        "QWidget#viewParametersOverlay {"
        "  background-color: #252526;"
        "  border: 1px solid #3F3F46;"
        "}"));

    auto* rootLay = new QVBoxLayout(_viewOverlayPanel);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    auto* titleBar = new ViewOverlayTitleBar(
        QStringLiteral("View parameters"),
        [this](bool on) { applyAutoHideFromUi(on); },
        _viewOverlayPanel);
    _viewDockPinButton = titleBar->pinButton();
    rootLay->addWidget(titleBar);

    auto* panel = new QWidget(_viewOverlayPanel);
    panel->setObjectName(QStringLiteral("viewParamsContent"));
    panel->setMouseTracking(true);
    panel->setAttribute(Qt::WA_Hover, true);
    panel->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet(QStringLiteral(
        "QWidget#viewParamsContent { background-color: #252526; }"
        "QWidget#viewParamsContent QDoubleSpinBox { background-color: #3C3C3C; color: #E0E0E0; border: 1px solid #555; }"
        "QWidget#viewParamsContent QLabel { background: transparent; color: #E0E0E0; }"));
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(8, 8, 8, 8);

    auto makeSpin = [](double lo, double hi, int decimals) {
        auto* s = new QDoubleSpinBox();
        s->setRange(lo, hi);
        s->setDecimals(decimals);
        s->setSingleStep(0.1);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(100);
        return s;
    };

    _coordX = makeSpin(-1e6, 1e6, 2);
    _coordY = makeSpin(-1e6, 1e6, 2);
    _coordZ = makeSpin(-1e6, 1e6, 2);
    _rotX = makeSpin(0.0, 360.0, 2);
    _rotY = makeSpin(0.0, 360.0, 2);
    _orthoL = makeSpin(-1e6, 1e6, 2);
    _orthoR = makeSpin(-1e6, 1e6, 2);
    _orthoB = makeSpin(-1e6, 1e6, 2);
    _orthoT = makeSpin(-1e6, 1e6, 2);
    _viewportW = makeSpin(500, 16000, 0);
    _viewportH = makeSpin(500, 16000, 0);

    form->addRow(QStringLiteral("coordinateX"), _coordX);
    form->addRow(QStringLiteral("coordinateY"), _coordY);
    form->addRow(QStringLiteral("coordinateZ"), _coordZ);
    form->addRow(QStringLiteral("rotateX"), _rotX);
    form->addRow(QStringLiteral("rotateY"), _rotY);
    form->addRow(QStringLiteral("orthoT"), _orthoT);
    form->addRow(QStringLiteral("orthoB"), _orthoB);
    form->addRow(QStringLiteral("orthoR"), _orthoR);
    form->addRow(QStringLiteral("orthoL"), _orthoL);
    form->addRow(QStringLiteral("viewportW"), _viewportW);
    form->addRow(QStringLiteral("viewportH"), _viewportH);

    auto apply = [this]() { applyViewPanelToApp(); };
    for (auto* s : { _coordX, _coordY, _coordZ, _rotX, _rotY, _orthoL, _orthoR, _orthoB, _orthoT, _viewportW, _viewportH }) {
        s->setMouseTracking(true);
        s->setAttribute(Qt::WA_Hover, true);
        s->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        QObject::connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [apply](double) { apply(); });
    }

    rootLay->addWidget(panel, 1);

    _viewOverlayPanel->installEventFilter(this);
    _viewOverlayPanel->show();
    _viewOverlayPanel->raise();
}

void MainWindow::syncViewPanelFromApp() {
    if (!_app) {
        return;
    }
    const auto s = _app->viewStateSnapshot();
    const QSignalBlocker bx(_coordX);
    const QSignalBlocker by(_coordY);
    const QSignalBlocker bz(_coordZ);
    const QSignalBlocker brx(_rotX);
    const QSignalBlocker bry(_rotY);
    const QSignalBlocker bol(_orthoL);
    const QSignalBlocker bor(_orthoR);
    const QSignalBlocker bob(_orthoB);
    const QSignalBlocker bot(_orthoT);
    const QSignalBlocker bvw(_viewportW);
    const QSignalBlocker bvh(_viewportH);
    _coordX->setValue(s.coordinate.x);
    _coordY->setValue(s.coordinate.y);
    _coordZ->setValue(s.coordinate.z);
    _rotX->setValue(s.rotation.x);
    _rotY->setValue(s.rotation.y);
    _orthoL->setValue(s.ortho.x);
    _orthoR->setValue(s.ortho.y);
    _orthoB->setValue(s.ortho.z);
    _orthoT->setValue(s.ortho.w);
    _viewportW->setValue(s.viewport.x);
    _viewportH->setValue(s.viewport.y);
}

void MainWindow::applyViewPanelToApp() {
    if (!_app) {
        return;
    }
    Application::ViewState s;
    s.coordinate = { static_cast<float>(_coordX->value()), static_cast<float>(_coordY->value()), static_cast<float>(_coordZ->value()) };
    s.rotation = { static_cast<float>(_rotX->value()), static_cast<float>(_rotY->value()) };
    s.ortho = { static_cast<float>(_orthoL->value()), static_cast<float>(_orthoR->value()),
        static_cast<float>(_orthoB->value()), static_cast<float>(_orthoT->value()) };
    s.viewport = { static_cast<int>(_viewportW->value()), static_cast<int>(_viewportH->value()) };
    _app->applyViewState(s);
}

void MainWindow::updateReadoutLabel(const QString& text) {
    if (_readoutLabel) {
        _readoutLabel->setText(text);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (_app) {
        _app->shutdownBeforeQtTeardown();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    relayoutCentralOverlays();
}

void MainWindow::logVulkanSurfaceSize(const char* reason) const {
    if (!_vkWindow) {
        std::printf("[Vulkan window] %s: (null QWindow)\n", reason);
        std::fflush(stdout);
        return;
    }
    const int lw = _vkWindow->width();
    const int lh = _vkWindow->height();
    const qreal dpr = _vkWindow->devicePixelRatio();
    const int fw = static_cast<int>(std::lround(static_cast<qreal>(lw) * dpr));
    const int fh = static_cast<int>(std::lround(static_cast<qreal>(lh) * dpr));
    std::printf(
        "[Vulkan window] %s: logical %dx%d, dpr %.3f, framebuffer ~%dx%d\n",
        reason,
        lw,
        lh,
        dpr,
        fw,
        fh);
    std::fflush(stdout);
}

void MainWindow::relayoutCentralOverlays() {
    if (!_centralStack || !_vkContainer) {
        return;
    }
    const int W = _centralStack->width();
    const int H = _centralStack->height();
    if (W <= 0 || H <= 0) {
        return;
    }

    const int pw = _viewPanelWidth;
    const int edge = 22;
    int vkX = 0;
    int vkY = 0;
    int vkW = W;
    int vkH = H;

    if (_viewOverlayPanel && _viewOverlayPanel->isVisible()) {
        const int stripW = std::min(pw, std::max(1, W - 1));
        if (_panelOnLeft) {
            _viewOverlayPanel->setGeometry(0, 0, stripW, H);
            vkX = stripW;
            vkW = W - stripW;
        } else {
            _viewOverlayPanel->setGeometry(W - stripW, 0, stripW, H);
            vkX = 0;
            vkW = W - stripW;
        }
        _viewOverlayPanel->raise();
    } else if (_autoHidePeekTab && _autoHidePeekTab->isVisible()) {
        if (_panelOnLeft) {
            _autoHidePeekTab->setGeometry(0, 0, edge, H);
            vkX = edge;
            vkW = W - edge;
        } else {
            _autoHidePeekTab->setGeometry(W - edge, 0, edge, H);
            vkX = 0;
            vkW = W - edge;
        }
        _autoHidePeekTab->raise();
    }

    vkW = std::max(1, vkW);
    vkH = std::max(1, vkH);
    _vkContainer->setGeometry(vkX, vkY, vkW, vkH);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (_app && watched == _vkWindow) {
        _app->handleQtEvent(watched, event);
    }
    if (_viewOverlayPanel && watched == _viewOverlayPanel) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            cancelViewDockAutoHide();
            const bool visible = (event->type() == QEvent::Show);
            const bool wantPanel = _actViewPanelToggle && _actViewPanelToggle->isChecked();
            if (visible) {
                _autoHidePeekTab->hide();
            } else if (_viewAutoHideEnabled && wantPanel) {
                _autoHidePeekTab->show();
            } else {
                _autoHidePeekTab->hide();
            }
            relayoutCentralOverlays();
            logVulkanSurfaceSize(visible ? "view panel shown" : "view panel hidden");
            syncViewPanelMenuCheck();
        }
        if (_viewAutoHideEnabled) {
            if (event->type() == QEvent::Leave) {
                scheduleViewDockAutoHide();
            } else if (event->type() == QEvent::Enter) {
                cancelViewDockAutoHide();
            }
        }
    }
    if (_autoHidePeekTab && _viewAutoHideEnabled && watched == _autoHidePeekTab) {
        if (event->type() == QEvent::Enter) {
            revealViewDockFromAutoHide();
        }
    }
    return false;
}

void MainWindow::scheduleViewDockAutoHide() {
    if (!_viewAutoHideEnabled || !_viewOverlayPanel) {
        return;
    }
    if (!_viewOverlayPanel->isVisible()) {
        return;
    }
    if (_viewAutoHideTimer) {
        _viewAutoHideTimer->start();
    }
}

void MainWindow::cancelViewDockAutoHide() {
    if (_viewAutoHideTimer) {
        _viewAutoHideTimer->stop();
    }
}

void MainWindow::revealViewDockFromAutoHide() {
    if (!_viewOverlayPanel) {
        return;
    }
    cancelViewDockAutoHide();
    _autoHidePeekTab->hide();
    _viewOverlayPanel->show();
    relayoutCentralOverlays();
    if (_actViewPanelToggle) {
        const QSignalBlocker b(_actViewPanelToggle);
        _actViewPanelToggle->setChecked(true);
    }
}

void MainWindow::syncViewPanelMenuCheck() {
    if (!_actViewPanelToggle || !_viewOverlayPanel) {
        return;
    }
    const bool panelAvailable =
        _viewOverlayPanel->isVisible() || (_autoHidePeekTab && _autoHidePeekTab->isVisible());
    const QSignalBlocker b(_actViewPanelToggle);
    _actViewPanelToggle->setChecked(panelAvailable);
}

void MainWindow::applyAutoHideFromUi(bool on) {
    _viewAutoHideEnabled = on;
    cancelViewDockAutoHide();
    syncDockPinButtonState(_viewDockPinButton, on);
    if (_actAutoHidePanel) {
        const QSignalBlocker b(_actAutoHidePanel);
        _actAutoHidePanel->setChecked(on);
    }
    if (!on) {
        _autoHidePeekTab->hide();
        if (_viewOverlayPanel && !_viewOverlayPanel->isVisible() && _actViewPanelToggle &&
            _actViewPanelToggle->isChecked()) {
            _viewOverlayPanel->show();
        }
        relayoutCentralOverlays();
    }
    syncViewPanelMenuCheck();
}
