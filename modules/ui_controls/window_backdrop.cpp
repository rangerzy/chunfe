#include "window_backdrop.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QQuickWindow>
#include <QTimer>

#include <cmath>

WindowBackdrop::WindowBackdrop(QQuickItem *parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &QQuickItem::windowChanged, this, &WindowBackdrop::attachWindow);
    connect(this, &QQuickItem::visibleChanged, this, &WindowBackdrop::scheduleUpdate);
}

WindowBackdrop::~WindowBackdrop()
{
    if (m_window)
        m_window->removeEventFilter(this);
    clearNativeBackdrop();
}

void WindowBackdrop::setCornerRadius(qreal radius)
{
    if (!std::isfinite(radius))
        return;
    radius = qMax(qreal(0), radius);
    if (m_cornerRadius == radius)
        return;
    m_cornerRadius = radius;
    emit cornerRadiusChanged();
    scheduleUpdate();
}

void WindowBackdrop::componentComplete()
{
    QQuickItem::componentComplete();
    attachWindow(window());
    scheduleUpdate();
}

void WindowBackdrop::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    scheduleUpdate();
}

void WindowBackdrop::attachWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    clearNativeBackdrop();
    setAvailable(false);
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::visibleChanged, this, &WindowBackdrop::scheduleUpdate);
        connect(m_window, &QObject::destroyed, this, [this] {
            clearNativeBackdrop();
            setAvailable(false);
        });
    }
    scheduleUpdate();
}

bool WindowBackdrop::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::PlatformSurface:
            if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                    == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                clearNativeBackdrop();
                setAvailable(false);
                break;
            }
            scheduleUpdate();
            break;
        case QEvent::Hide:
            clearNativeBackdrop();
            setAvailable(false);
            break;
        case QEvent::Show:
        case QEvent::Expose:
        case QEvent::Resize:
        case QEvent::WinIdChange:
            scheduleUpdate();
            break;
        default:
            break;
        }
    }
    return QQuickItem::eventFilter(watched, event);
}

void WindowBackdrop::scheduleUpdate()
{
    if (m_updatePending)
        return;
    m_updatePending = true;
    QTimer::singleShot(0, this, [this] {
        m_updatePending = false;
        updateBackdrop();
    });
}

void WindowBackdrop::updateBackdrop()
{
    const bool canAttach = isComponentComplete() && isVisible() && m_window
        && m_window->isVisible() && m_window->handle()
        && QGuiApplication::platformName() == QStringLiteral("cocoa");
    const QRectF rect = mapRectToScene(boundingRect());
    const bool attached = canAttach && !rect.isEmpty() && updateNativeBackdrop(m_window, rect);
    if (!attached)
        clearNativeBackdrop();
    setAvailable(attached);
}

void WindowBackdrop::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}

#ifndef Q_OS_MACOS
bool WindowBackdrop::updateNativeBackdrop(QQuickWindow *, const QRectF &)
{
    return false;
}

void WindowBackdrop::clearNativeBackdrop()
{
}
#endif
