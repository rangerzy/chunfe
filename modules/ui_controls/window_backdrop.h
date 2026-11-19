#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class QQuickWindow;

class WindowBackdrop : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged FINAL)

public:
    explicit WindowBackdrop(QQuickItem *parent = nullptr);
    ~WindowBackdrop() override;

    bool available() const { return m_available; }
    qreal cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(qreal radius);

signals:
    void availableChanged();
    void cornerRadiusChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct NativeBackdrop;

    void attachWindow(QQuickWindow *window);
    void scheduleUpdate();
    void updateBackdrop();
    void setAvailable(bool available);
    bool updateNativeBackdrop(QQuickWindow *window, const QRectF &rect);
    void clearNativeBackdrop();

    QPointer<QQuickWindow> m_window;
    NativeBackdrop *m_nativeBackdrop = nullptr;
    qreal m_cornerRadius = 12;
    bool m_available = false;
    bool m_updatePending = false;
};
