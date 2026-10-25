#include "mainwindow.h"
#include "lottiewidget.h"
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    resize(1120, 760);
    setMinimumSize(800, 560);
    auto* tools = addToolBar(QStringLiteral("文件"));
    tools->setMovable(false);
    auto* open = tools->addAction(QStringLiteral("打开动画…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 Lottie JSON"), {},
                                                     QStringLiteral("Lottie (*.json *.lot);;所有文件 (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    auto* save = tools->addAction(QStringLiteral("另存为…"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { saveAs(); });
    tools->addSeparator();
    connect(tools->addAction(QStringLiteral("内置示例")), &QAction::triggered,
            this, [this] { if (mayDiscard()) loadSample(); });
    connect(tools->addAction(QStringLiteral("预览字体…")), &QAction::triggered,
            this, &MainWindow::chooseFont);

    auto* splitter = new QSplitter(this);
    auto* previewPanel = new QWidget(splitter);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(12, 12, 12, 12);
    auto* heading = new QLabel(QStringLiteral("LOTTIE / 实时预览"));
    heading->setStyleSheet(QStringLiteral("font-weight:600; font-size:15px;"));
    previewLayout->addWidget(heading);
    player_ = new LottieWidget;
    player_->setMinimumSize(320, 240);
    previewLayout->addWidget(player_, 1);
    auto* transport = new QHBoxLayout;
    play_ = new QPushButton(QStringLiteral("播放"));
    play_->setObjectName(QStringLiteral("playButton"));
    transport->addWidget(play_);
    connect(play_, &QPushButton::clicked, this, [this] { player_->setPlaying(!player_->isPlaying()); });
    connect(player_, &LottieWidget::playingChanged, this, [this](bool playing) {
        play_->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    });
    auto* loop = new QCheckBox(QStringLiteral("循环"));
    loop->setChecked(true);
    connect(loop, &QCheckBox::toggled, player_, &LottieWidget::setLooping);
    transport->addWidget(loop);
    timeline_ = new QSlider(Qt::Horizontal);
    timeline_->setObjectName(QStringLiteral("timeline"));
    timeline_->setRange(0, 10000);
    transport->addWidget(timeline_, 1);
    time_ = new QLabel;
    time_->setMinimumWidth(110);
    transport->addWidget(time_);
    previewLayout->addLayout(transport);
    connect(timeline_, &QSlider::valueChanged, this, [this](int v) {
        player_->setPosition(v / 10000.0);
    });
    connect(timeline_, &QSlider::sliderPressed, this, [this] { player_->setPlaying(false); });
    connect(player_, &LottieWidget::positionChanged, this, [this](double p) {
        const QSignalBlocker blocker(timeline_);
        timeline_->setValue(qRound(p * 10000));
        const double duration = document_.frameRate() > 0
            ? document_.frameCount() / document_.frameRate() : 0;
        time_->setText(QStringLiteral("%1 / %2 s").arg(p * duration, 0, 'f', 2).arg(duration, 0, 'f', 2));
    });
    gpu_ = new QLabel(QStringLiteral("正在创建 OpenGL GPU 上下文…"));
    gpu_->setWordWrap(true);
    gpu_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLayout->addWidget(gpu_);
    connect(player_, &LottieWidget::gpuReady, gpu_, &QLabel::setText);
    connect(player_, &LottieWidget::renderError, this, [this](const QString& error) {
        gpu_->setText(error);
        gpu_->setStyleSheet(QStringLiteral("color:#d65151"));
        statusBar()->showMessage(error);
    });

    auto* inspector = new QWidget(splitter);
    inspector->setMinimumWidth(290);
    auto* editLayout = new QVBoxLayout(inspector);
    editLayout->setContentsMargins(12, 12, 12, 12);
    auto* title = new QLabel(QStringLiteral("文字与图片"));
    title->setStyleSheet(heading->styleSheet());
    editLayout->addWidget(title);
    info_ = new QLabel;
    info_->setWordWrap(true);
    editLayout->addWidget(info_);
    entries_ = new QListWidget;
    entries_->setObjectName(QStringLiteral("editableEntries"));
    editLayout->addWidget(entries_, 1);
    editor_ = new QStackedWidget;
    auto* empty = new QLabel(QStringLiteral("选择一个文字图层或图片资源。\n轮廓文字无法编辑。"));
    empty->setWordWrap(true);
    editor_->addWidget(empty);
    auto* textPanel = new QWidget;
    auto* textLayout = new QVBoxLayout(textPanel);
    textLayout->setContentsMargins(0, 0, 0, 0);
    text_ = new QPlainTextEdit;
    text_->setObjectName(QStringLiteral("textEditor"));
    text_->setPlaceholderText(QStringLiteral("输入文字，自动应用到所有文字关键帧"));
    text_->setMaximumHeight(160);
    textLayout->addWidget(text_);
    auto* apply = new QPushButton(QStringLiteral("应用并预览"));
    textLayout->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, [this] { applyText(); });
    editor_->addWidget(textPanel);
    auto* imagePanel = new QWidget;
    auto* imageLayout = new QVBoxLayout(imagePanel);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imagePreview_ = new QLabel;
    imagePreview_->setAlignment(Qt::AlignCenter);
    imagePreview_->setMinimumHeight(120);
    imageLayout->addWidget(imagePreview_);
    auto* replace = new QPushButton(QStringLiteral("更换图片…"));
    replace->setObjectName(QStringLiteral("replaceImageButton"));
    imageLayout->addWidget(replace);
    connect(replace, &QPushButton::clicked, this, &MainWindow::replaceImage);
    auto* note = new QLabel(QStringLiteral("图片按原资源尺寸等比缩放并留透明边，\n保留图层变换；共享资源的实例会一起更新。"));
    note->setWordWrap(true);
    imageLayout->addWidget(note);
    editor_->addWidget(imagePanel);
    editLayout->addWidget(editor_);
    font_ = new QLabel(player_->fontDescription());
    font_->setWordWrap(true);
    editLayout->addWidget(font_);
    auto* caveat = new QLabel(QStringLiteral("文字会应用到该图层的全部文字关键帧。\n另存 JSON 会嵌入图片；字体需单独提供。"));
    caveat->setWordWrap(true);
    editLayout->addWidget(caveat);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({760, 320});
    setCentralWidget(splitter);
    textTimer_.setSingleShot(true);
    textTimer_.setInterval(350);
    connect(&textTimer_, &QTimer::timeout, this, [this] { applyText(); });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        pendingText_ = true;
        textTimer_.start();
        updateTitle();
    });
    connect(entries_, &QListWidget::currentRowChanged, this, &MainWindow::selectEntry);
    loadSample();
}

void MainWindow::loadSample()
{
    QFile f(QStringLiteral(":/assets/demo.json"));
    if (!f.open(QIODevice::ReadOnly)) { showError(f.errorString()); return; }
    loadBytes(f.readAll(), {});
}

bool MainWindow::openFile(const QString& path)
{
    if (!mayDiscard()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (pendingText_) textTimer_.start();
        showError(file.errorString());
        return false;
    }
    const bool loaded = loadBytes(file.readAll(), QFileInfo(path).absoluteFilePath());
    if (!loaded && pendingText_) textTimer_.start();
    return loaded;
}

bool MainWindow::loadBytes(const QByteArray& bytes, const QString& path)
{
    LottieDocument candidate;
    QString error;
    if (!candidate.load(bytes, path.isEmpty() ? QString() : QFileInfo(path).absolutePath(), &error)
        || !player_->setDocument(candidate, false, &error)) {
        showError(error);
        return false;
    }
    textTimer_.stop();
    pendingText_ = false;
    document_ = candidate;
    filePath_ = path;
    dirty_ = false;
    activeEntry_ = -1;
    refreshEntries();
    const auto count = document_.entries().size();
    info_->setText(QStringLiteral("%1 × %2 · %3 fps\n%4 个可编辑项目")
        .arg(document_.size().width()).arg(document_.size().height())
        .arg(document_.frameRate()).arg(count));
    updateTitle();
    player_->setPosition(0);
    player_->setPlaying(true);
    statusBar()->showMessage(QStringLiteral("已加载动画"), 3000);
    return true;
}

void MainWindow::refreshEntries()
{
    const QSignalBlocker block(entries_);
    entries_->clear();
    for (const auto& entry : document_.entries()) {
        entries_->addItem((entry.kind == LottieDocument::Entry::Kind::Text
            ? QStringLiteral("文字 · ") : QStringLiteral("图片 · ")) + entry.label);
    }
    if (entries_->count()) entries_->setCurrentRow(0);
    selectEntry(entries_->currentRow());
}

void MainWindow::selectEntry(int row)
{
    if (!applyText()) {
        const QSignalBlocker block(entries_);
        entries_->setCurrentRow(activeEntry_);
        return;
    }
    activeEntry_ = row;
    if (row < 0 || row >= document_.entries().size()) { editor_->setCurrentIndex(0); return; }
    const auto entry = document_.entries()[row];
    if (entry.kind == LottieDocument::Entry::Kind::Text) {
        const QSignalBlocker block(text_);
        QString value = document_.text(row);
        text_->setPlainText(value.replace('\r', '\n'));
        editor_->setCurrentIndex(1);
    } else {
        const auto image = document_.image(row);
        imagePreview_->setPixmap(QPixmap::fromImage(image).scaled(220, 130, Qt::KeepAspectRatio,
                                                                Qt::SmoothTransformation));
        editor_->setCurrentIndex(2);
    }
}

bool MainWindow::applyText()
{
    textTimer_.stop();
    if (!pendingText_ || activeEntry_ < 0) return true;
    LottieDocument candidate = document_;
    QString error;
    if (!candidate.replaceText(activeEntry_, text_->toPlainText(), &error)
        || !player_->setDocument(candidate, true, &error)) {
        showError(error);
        return false;
    }
    document_ = candidate;
    pendingText_ = false;
    dirty_ = true;
    updateTitle();
    return true;
}

void MainWindow::replaceImage()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择替换图片"), {},
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (image.isNull()) { showError(reader.errorString()); return; }
    LottieDocument candidate = document_;
    QString error;
    if (!candidate.replaceImage(activeEntry_, image, &error)
        || !player_->setDocument(candidate, true, &error)) {
        showError(error);
        return;
    }
    document_ = candidate;
    dirty_ = true;
    updateTitle();
    selectEntry(activeEntry_);
}

void MainWindow::chooseFont()
{
    if (!applyText()) return;
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("为缺失字体选择 TTF / OTF"), {},
                                                 QStringLiteral("字体 (*.ttf *.otf)"));
    if (path.isEmpty()) return;
    QString error;
    if (!player_->setFontFile(path, &error)) { showError(error); return; }
    font_->setText(player_->fontDescription());
    statusBar()->showMessage(QStringLiteral("预览字体已更新；保存时请另行分发字体文件。"), 6000);
}

bool MainWindow::saveAs()
{
    if (!applyText()) return false;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("另存 Lottie JSON"),
        filePath_.isEmpty() ? QStringLiteral("edited-animation.json") : filePath_,
        QStringLiteral("Lottie JSON (*.json)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
    QString error;
    if (!document_.save(path, &error)) { showError(error); return false; }
    filePath_ = path;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage(QStringLiteral("已保存：") + path, 5000);
    return true;
}

bool MainWindow::mayDiscard()
{
    if (!dirty_ && !pendingText_) return true;
    textTimer_.stop();
    const auto choice = QMessageBox::question(this, QStringLiteral("保存修改"),
        QStringLiteral("动画有未保存的修改，是否先保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        if (pendingText_) textTimer_.start();
        return false;
    }
    if (choice == QMessageBox::Save) return saveAs();
    return true;
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1%2 — Lottie GPU Editor")
        .arg(dirty_ || pendingText_ ? QStringLiteral("* ") : QString())
        .arg(filePath_.isEmpty() ? QStringLiteral("内置示例") : QFileInfo(filePath_).fileName()));
}

void MainWindow::showError(const QString& message)
{
    QMessageBox::warning(this, QStringLiteral("操作失败"), message);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (mayDiscard()) event->accept(); else event->ignore();
}
