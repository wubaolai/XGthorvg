#include "lottiewidget.h"
#include <QApplication>
#include <QFile>
#include <QOpenGLContext>
#include <QSignalSpy>
#include <QtTest>

class GpuTest : public QObject
{
    Q_OBJECT
private slots:
    void contextRecreation()
    {
        QFile file(":/assets/demo.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        LottieDocument doc;
        QString error;
        QVERIFY(doc.load(file.readAll(), {}, &error));
        QWidget first, second;
        first.resize(400, 300);
        second.resize(400, 300);
        LottieWidget view(&first);
        view.resize(400, 300);
        QSignalSpy errors(&view, &LottieWidget::renderError);
        QVERIFY2(view.setDocument(doc, false, &error), qPrintable(error));
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(view.renderedFrames() > 0 || !errors.isEmpty());
        QVERIFY2(errors.isEmpty(), qPrintable(view.lastError()));
        view.setPosition(0.4);
        const auto before = view.grabFramebuffer();
        const auto frames = view.renderedFrames();
        QSignalSpy destroyed(view.context(), &QOpenGLContext::aboutToBeDestroyed);
        second.show();
        view.setParent(&second);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(view.renderedFrames() > frames || !errors.isEmpty());
        QVERIFY2(errors.isEmpty(), qPrintable(view.lastError()));
        QCOMPARE(destroyed.size(), 1);
        QCOMPARE(view.position(), 0.4);
        QVERIFY(!view.isPlaying());
        QCOMPARE(view.grabFramebuffer(), before);
    }

    void playbackEditResizeAndReload()
    {
        QFile file(":/assets/demo.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        LottieDocument doc;
        QString error;
        QVERIFY2(doc.load(file.readAll(), {}, &error), qPrintable(error));
        if (!qEnvironmentVariableIsEmpty("LOTTIE_TEST_JSON"))
            QVERIFY(doc.save(qEnvironmentVariable("LOTTIE_TEST_JSON"), &error));
        LottieWidget view;
        view.resize(640, 400);
        QSignalSpy errors(&view, &LottieWidget::renderError);
        QVERIFY2(view.setDocument(doc, false, &error), qPrintable(error));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY_WITH_TIMEOUT(view.renderedFrames() > 0 || !errors.isEmpty(), 10000);
        QVERIFY2(errors.isEmpty(), qPrintable(view.lastError()));
        QVERIFY(view.isValid());
        QVERIFY(!view.gpuDescription().isEmpty());
        qInfo().noquote() << "GPU:" << view.gpuDescription();
        qInfo().noquote() << "Font:" << view.fontDescription();

        view.setPosition(0.25);
        const QImage original = view.grabFramebuffer();
        QVERIFY(!original.isNull());
        const auto background = original.pixel(original.width()/2, original.height()/20);
        bool nonuniform = false;
        for (int y = 0; y < original.height(); y += 5)
            for (int x = 0; x < original.width(); x += 5)
                nonuniform |= original.pixel(x, y) != background;
        QVERIFY2(nonuniform, "GPU output must contain the sample, not only a clear color");

        int textIndex = -1, imageIndex = -1;
        const auto entries = doc.entries();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == LottieDocument::Entry::Kind::Text && textIndex < 0) textIndex = i;
            if (entries[i].kind == LottieDocument::Entry::Kind::Image) imageIndex = i;
        }
        QVERIFY(textIndex >= 0);
        QVERIFY(imageIndex >= 0);
        QVERIFY(doc.replaceText(textIndex, QStringLiteral("GPU text replacement"), &error));
        QVERIFY(view.setDocument(doc, true, &error));
        QCOMPARE(view.position(), 0.25);
        QVERIFY(!view.isPlaying());
        const auto textChanged = view.grabFramebuffer();
        QVERIFY2(textChanged != original, "Changing text must change the real GPU framebuffer");

        QImage replacement(80, 120, QImage::Format_ARGB32);
        replacement.fill(QColor(240, 75, 100));
        QVERIFY(doc.replaceImage(imageIndex, replacement, &error));
        QVERIFY(view.setDocument(doc, true, &error));
        const auto imageChanged = view.grabFramebuffer();
        QVERIFY2(imageChanged != textChanged, "Replacing an image must change the GPU framebuffer");

        view.setPlaying(true);
        QTRY_VERIFY(view.position() > 0.26);
        view.setPlaying(false);
        const double paused = view.position();
        QTest::qWait(70);
        QCOMPARE(view.position(), paused);
        view.setPlaying(true);
        QVERIFY(view.setDocument(doc, true, &error));
        QVERIFY(view.isPlaying());
        view.setPlaying(false);
        view.resize(480, 480);
        QTRY_COMPARE(view.size(), QSize(480, 480));
        QVERIFY(!view.grabFramebuffer().isNull());
        QCOMPARE(errors.size(), 0);
        view.setLooping(false);
        view.setPosition(0.999);
        view.setPlaying(true);
        QTRY_VERIFY(!view.isPlaying());
        QCOMPARE(view.position(), 1.0);
        view.setLooping(true);
        view.setPosition(0.999);
        view.setPlaying(true);
        QTRY_VERIFY(view.position() < 0.5);
        view.setPlaying(false);

        for (int i = 0; i < 5; ++i) {
            QVERIFY(view.setDocument(doc, true, &error));
            QVERIFY(!view.grabFramebuffer().isNull());
        }
        QCOMPARE(errors.size(), 0);
        if (!qEnvironmentVariableIsEmpty("LOTTIE_TEST_CAPTURE"))
            QVERIFY(view.grabFramebuffer().save(qEnvironmentVariable("LOTTIE_TEST_CAPTURE")));
    }
};

int main(int argc, char** argv)
{
    LottieWidget::configureSurfaceFormat();
    QApplication app(argc, argv);
    GpuTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "gpu_test.moc"
