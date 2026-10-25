#pragma once

#include "lottiedocument.h"
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>

class LottieWidget final : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit LottieWidget(QWidget* parent = nullptr);
    ~LottieWidget() override;
    static void configureSurfaceFormat();
    bool setDocument(const LottieDocument& document, bool preservePosition = true,
                     QString* error = nullptr);
    bool setFontFile(const QString& path, QString* error = nullptr);
    QString fontDescription() const;
    QString gpuDescription() const;
    QString lastError() const;
    double position() const { return position_; }
    bool isPlaying() const { return playing_; }
    bool isLooping() const { return looping_; }
    quint64 renderedFrames() const { return renderedFrames_; }
    void setPlaying(bool playing);
    void setLooping(bool looping) { looping_ = looping; }
    void setPosition(double position);

signals:
    void positionChanged(double position);
    void playingChanged(bool playing);
    void renderError(const QString& message);
    void gpuReady(const QString& description);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QTimer timer_;
    QElapsedTimer clock_;
    double position_ = 0;
    bool playing_ = false;
    bool looping_ = true;
    quint64 renderedFrames_ = 0;
    void tick();
    void cleanupGL();
    void fail(const QString& message);
};
