#include "text_clipboard.h"

#include <QAccessible>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

class TextCopyActionsTests : public QObject
{
    Q_OBJECT

private slots:
    void callerReceivesCurrentTextAndControlsFeedback()
    {
        QQmlEngine engine;
        qmlRegisterType<TextClipboard>("CopyActionsTest", 1, 0, "TextClipboard");
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import CopyActionsTest
            Window {
                width: 300; height: 180; visible: true
                TextClipboard { id: clipboard }
                TextCopyActions {
                    objectName: "actions"
                    width: 240; height: 80
                    text: "第一段 👩🏽‍💻 é"
                    onCopyRequested: function(text) { clipboard.copyText(text); }
                }
                Item { objectName: "other"; y: 100; width: 100; height: 50 }
            }
        )", QUrl::fromLocalFile(QStringLiteral(TEXT_COPY_ACTIONS_SOURCE_DIR "/actions_test.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        auto *actions = root->findChild<QQuickItem *>("actions");
        auto *other = root->findChild<QQuickItem *>("other");
        QVERIFY(actions && other);
        QSignalSpy requested(actions, SIGNAL(copyRequested(QString)));
        QVERIFY(requested.isValid());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QTRY_VERIFY(actions->hasActiveFocus());
        QVERIFY(!actions->property("visualFocus").toBool());
        QTest::keySequence(window, QKeySequence::Copy);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), QString::fromUtf8("第一段 👩🏽‍💻 é"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QString::fromUtf8("第一段 👩🏽‍💻 é"));

        const QString updated = QString::fromUtf8("更新后的整段 ABC\n第二行");
        actions->setProperty("text", updated);
        QVERIFY(QMetaObject::invokeMethod(actions, "copy"));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.takeFirst().at(0).toString(), updated);
        QCOMPARE(QGuiApplication::clipboard()->text(), updated);

        auto *accessible = QAccessible::queryAccessibleInterface(actions);
        QVERIFY(accessible);
        auto *accessibleActions = accessible->actionInterface();
        QVERIFY(accessibleActions);
        accessibleActions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(requested.count(), 1);
        requested.clear();

        auto *feedback = actions->findChild<QObject *>("copyFeedback");
        QVERIFY(feedback);
        QVERIFY(!feedback->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(actions, "showCopied"));
        QTRY_VERIFY(feedback->property("visible").toBool());

        other->forceActiveFocus();
        QTRY_VERIFY(!actions->hasActiveFocus());
        QTest::keySequence(window, QKeySequence::Copy);
        QCOMPARE(requested.count(), 0);
        actions->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(actions->property("visualFocus").toBool());
    }
};

QTEST_MAIN(TextCopyActionsTests)
#include "text_copy_actions_tests.moc"
