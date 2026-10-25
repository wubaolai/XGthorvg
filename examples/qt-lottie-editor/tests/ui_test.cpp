#include "mainwindow.h"
#include "lottiewidget.h"
#include <QApplication>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QtTest>

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    void editsTheSampleThroughTheWindow()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* player = window.findChild<LottieWidget*>();
        auto* entries = window.findChild<QListWidget*>("editableEntries");
        auto* editor = window.findChild<QPlainTextEdit*>("textEditor");
        auto* play = window.findChild<QPushButton*>("playButton");
        QVERIFY(player && entries && editor && play);
        QTRY_VERIFY(player->renderedFrames() > 0);
        QVERIFY2(player->lastError().isEmpty(), qPrintable(player->lastError()));
        QCOMPARE(entries->count(), 3);
        QCOMPARE(editor->toPlainText(), QStringLiteral("Hello, Lottie!"));
        QVERIFY(player->isPlaying());
        QTest::mouseClick(play, Qt::LeftButton);
        QVERIFY(!player->isPlaying());
        player->setPosition(0.25);
        const auto before = player->grabFramebuffer();
        if (!qEnvironmentVariableIsEmpty("LOTTIE_UI_CAPTURE"))
            QVERIFY(window.grab().save(qEnvironmentVariable("LOTTIE_UI_CAPTURE")));

        const auto changed = QStringLiteral("你好，Lottie！");
        editor->setPlainText(changed);
        QTRY_VERIFY_WITH_TIMEOUT(player->grabFramebuffer() != before, 3000);
        QCOMPARE(player->position(), 0.25);
        QVERIFY(!player->isPlaying());
        QVERIFY(window.windowTitle().startsWith('*'));
        entries->setCurrentRow(1);
        QCOMPARE(editor->toPlainText(), QStringLiteral("Qt 6 + ThorVG GPU"));
        // Switching selection must flush a pending edit to the OLD selected layer.
        const auto second = QStringLiteral("第二行文字");
        editor->setPlainText(second);
        entries->setCurrentRow(0);
        QCOMPARE(editor->toPlainText(), changed);
        entries->setCurrentRow(1);
        QCOMPARE(editor->toPlainText(), second);
        entries->setCurrentRow(2);
        QVERIFY(window.findChild<QPushButton*>("replaceImageButton")->isVisible());
        QVERIFY2(player->lastError().isEmpty(), qPrintable(player->lastError()));
    }
};

int main(int argc, char** argv)
{
    LottieWidget::configureSurfaceFormat();
    QApplication app(argc, argv);
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_test.moc"
