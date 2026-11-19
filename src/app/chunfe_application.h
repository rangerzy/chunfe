#pragma once

#include <QtCore/QEvent>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>

class ChunfeApplication : public QGuiApplication
{
public:
    using QGuiApplication::QGuiApplication;

    void setMainWindow(QObject *window) { m_mainWindow = window; }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::Quit || !m_mainWindow)
            return QGuiApplication::event(event);

        // Qt asks each top-level window to close before it accepts Quit.
        m_mainWindow->setProperty("applicationQuitting", true);
        if (m_mainWindow && m_mainWindow->property("conversationQuitPending").toBool()) {
            event->ignore();
            return true;
        }
        if (m_mainWindow && m_mainWindow->property("conversationBlocksQuit").toBool()) {
            event->ignore();
            m_mainWindow->setProperty("applicationQuitting", false);
            QMetaObject::invokeMethod(m_mainWindow, "showUnsavedConversationQuitDialog", Qt::QueuedConnection);
            return true;
        }
        const bool handled = QGuiApplication::event(event);
        if (!event->isAccepted() && m_mainWindow)
            m_mainWindow->setProperty("applicationQuitting", false);
        return handled;
    }

private:
    QPointer<QObject> m_mainWindow;
};
