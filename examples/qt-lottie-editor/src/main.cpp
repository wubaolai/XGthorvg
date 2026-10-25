#include "lottiewidget.h"
#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char** argv)
{
    LottieWidget::configureSurfaceFormat();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ThorVG Lottie Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt 6 / C++17 / ThorVG OpenGL Lottie editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("animation"), QStringLiteral("Lottie JSON file to open"));
    parser.process(app);
    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        const auto path = parser.positionalArguments().first();
        QTimer::singleShot(0, &window, [&window, path] { window.openFile(path); });
    }
    return app.exec();
}
