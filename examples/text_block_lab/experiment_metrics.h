#pragma once

#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <mutex>

// Demo-only instrumentation. The reusable TextBlock module has no dependency on it.
class ExperimentMetrics : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ExperimentMetrics(QQuickItem *parent = nullptr);
    Q_INVOKABLE void measureBatch(QQuickItem *root, QJSValue mutation);
    Q_INVOKABLE void cancel();

signals:
    void batchMeasured(double guiMicroseconds, double submissionMilliseconds,
                       double framePreparationMilliseconds, bool settled);
    void failed(const QString &message);

private:
    void observeWindow(QQuickWindow *window);

    QPointer<QQuickWindow> m_observedWindow;
    QList<QMetaObject::Connection> m_connections;
    QTimer m_timeout;
    std::mutex m_sampleMutex;
    std::atomic_bool m_pending{false};
    std::atomic<quint64> m_generation{0};
    std::atomic<qint64> m_syncStarted{0};
    std::atomic<qint64> m_renderEnded{0};
    qint64 m_started = 0;
    qint64 m_ready = 0;
    double m_guiMicroseconds = 0;
    bool m_settled = false;
    bool m_busy = false;
};
