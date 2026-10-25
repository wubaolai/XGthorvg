#pragma once

#include "lottiedocument.h"
#include <QMainWindow>
#include <QTimer>

class LottieWidget;
class QListWidget;
class QPlainTextEdit;
class QLabel;
class QSlider;
class QPushButton;
class QStackedWidget;
class QCloseEvent;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    LottieDocument document_;
    QString filePath_;
    bool dirty_ = false;
    bool pendingText_ = false;
    int activeEntry_ = -1;
    QTimer textTimer_;
    LottieWidget* player_;
    QListWidget* entries_;
    QPlainTextEdit* text_;
    QStackedWidget* editor_;
    QLabel* imagePreview_;
    QLabel* info_;
    QLabel* gpu_;
    QLabel* font_;
    QLabel* time_;
    QSlider* timeline_;
    QPushButton* play_;
    void loadSample();
    bool loadBytes(const QByteArray& bytes, const QString& path);
    bool saveAs();
    bool mayDiscard();
    bool applyText();
    void replaceImage();
    void chooseFont();
    void selectEntry(int row);
    void refreshEntries();
    void updateTitle();
    void showError(const QString& message);
};
