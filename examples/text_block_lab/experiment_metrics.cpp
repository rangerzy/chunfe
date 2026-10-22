#include "experiment_metrics.h"

#include <QQuickWindow>

#include <chrono>

namespace {
qint64 nowNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

QVector<qreal> geometrySnapshot(QQuickItem *item)
{
    QVector<qreal> result{item->x(), item->y(), item->width(), item->height(),
                          item->implicitWidth(), item->implicitHeight()};
    for (auto *child : item->childItems())
        result += geometrySnapshot(child);
    return result;
}

void polishTree(QQuickItem *item)
{
    item->ensurePolished();
    for (auto *child : item->childItems())
        polishTree(child);
    // A child's new implicit size can schedule its parent again.
    item->ensurePolished();
}
}

ExperimentMetrics::ExperimentMetrics(QQuickItem *parent) : QQuickItem(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        cancel();
        emit failed(tr("未收到可测量的帧，请保持实验窗口可见后重试。"));
    });
}

void ExperimentMetrics::cancel()
{
    const std::scoped_lock lock(m_sampleMutex);
    ++m_generation;
    m_pending.store(false);
    m_busy = false;
    m_timeout.stop();
}

void ExperimentMetrics::observeWindow(QQuickWindow *window)
{
    if (m_observedWindow == window)
        return;
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_observedWindow = window;
    m_connections.append(connect(window, &QQuickWindow::beforeSynchronizing, this, [this] {
        const std::scoped_lock lock(m_sampleMutex);
        if (m_pending.load(std::memory_order_acquire))
            m_syncStarted.store(nowNanoseconds());
    }, Qt::DirectConnection));
    m_connections.append(connect(window, &QQuickWindow::afterRendering, this, [this] {
        const std::scoped_lock lock(m_sampleMutex);
        if (m_pending.load(std::memory_order_acquire))
            m_renderEnded.store(nowNanoseconds());
    }, Qt::DirectConnection));
    m_connections.append(connect(window, &QQuickWindow::afterFrameEnd, this, [this] {
        const std::scoped_lock lock(m_sampleMutex);
        if (!m_pending.load(std::memory_order_acquire))
            return;
        const auto syncStarted = m_syncStarted.load();
        const auto renderEnded = m_renderEnded.load();
        // Ignore a frame that was already rendering when the batch started.
        if (syncStarted < m_ready || renderEnded < syncStarted)
            return;
        const auto generation = m_generation.load();
        const auto submissionMs = (nowNanoseconds() - m_started) / 1e6;
        const auto framePreparationMs = (renderEnded - syncStarted) / 1e6;
        const auto guiUs = m_guiMicroseconds;
        const auto settled = m_settled;
        m_pending.store(false);
        QMetaObject::invokeMethod(this, [this, generation, guiUs, submissionMs, framePreparationMs, settled] {
            if (generation != m_generation.load())
                return;
            m_busy = false;
            m_timeout.stop();
            emit batchMeasured(guiUs, submissionMs, framePreparationMs, settled);
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection));
}

void ExperimentMetrics::measureBatch(QQuickItem *root, QJSValue mutation)
{
    if (m_busy || !root || !root->window() || !mutation.isCallable()) {
        emit failed(tr("性能样本未就绪，或上一批尚未完成。"));
        return;
    }
    observeWindow(root->window());
    m_busy = true;
    const auto generation = ++m_generation;
    const auto started = nowNanoseconds();
    const auto result = mutation.call();
    if (generation != m_generation.load())
        return;
    if (result.isError()) {
        cancel();
        emit failed(result.toString());
        return;
    }
    bool settled = false;
    // Bound instrumentation work. A stable geometry snapshot is an observation,
    // not a claim about Qt's internal number of layout passes.
    for (int pass = 0; pass < 8; ++pass) {
        const auto before = geometrySnapshot(root);
        polishTree(root);
        if (generation != m_generation.load())
            return;
        if (geometrySnapshot(root) == before) {
            settled = true;
            break;
        }
    }
    {
        const std::scoped_lock lock(m_sampleMutex);
        if (generation != m_generation.load())
            return;
        m_started = started;
        m_ready = nowNanoseconds();
        m_settled = settled;
        m_guiMicroseconds = (m_ready - m_started) / 1000.0;
        m_syncStarted.store(0);
        m_renderEnded.store(0);
        m_pending.store(true, std::memory_order_release);
    }
    m_timeout.start();
    root->window()->update();
}
