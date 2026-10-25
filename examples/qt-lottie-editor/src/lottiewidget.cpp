#include "lottiewidget.h"
#include <thorvg.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#ifdef Q_OS_WIN
#include <QtGui/qopenglcontext_platform.h>
#endif
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct Runtime {
    bool valid = tvg::Initializer::init(0) == tvg::Result::Success;
    ~Runtime() { if (valid) tvg::Initializer::term(); }
};
std::shared_ptr<Runtime> runtime()
{
    static std::weak_ptr<Runtime> existing;
    auto shared = existing.lock();
    if (!shared) { shared = std::make_shared<Runtime>(); existing = shared; }
    return shared;
}
QString resultError(const QString& operation, tvg::Result result)
{
    return QStringLiteral("%1失败（ThorVG 错误 %2）。").arg(operation).arg(int(result));
}
}

struct LottieWidget::Impl {
    std::shared_ptr<Runtime> engine = runtime();
    std::unique_ptr<tvg::Animation> animation;
    std::unique_ptr<tvg::GlCanvas> canvas;
    LottieDocument document;
    QStringList fonts;
    QByteArray fontAlias;
    QString fontPath;
    QString error;
    QString gpu;
    bool renderFailed = false;
};

LottieWidget::LottieWidget(QWidget* parent) : QOpenGLWidget(parent), d(std::make_unique<Impl>())
{
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &LottieWidget::tick);
    clock_.start();
    QStringList fonts;
#ifdef Q_OS_MACOS
    fonts << QStringLiteral("/System/Library/Fonts/Supplemental/Arial Unicode.ttf")
          << QStringLiteral("/System/Library/Fonts/Supplemental/Arial.ttf");
#elif defined(Q_OS_WIN)
    const auto directory = QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("Fonts");
    fonts << QDir(directory).filePath("simhei.ttf") << QDir(directory).filePath("arial.ttf");
#else
    fonts << QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
    for (const auto& path : fonts)
        if (QFileInfo::exists(path) && setFontFile(path)) break;
}

LottieWidget::~LottieWidget()
{
    timer_.stop();
    if (context()) disconnect(context(), nullptr, this, nullptr);
    cleanupGL();
    d->animation.reset();
    for (const auto& name : d->fonts) {
        const auto alias = name.toUtf8();
        tvg::Text::load(alias.constData(), nullptr, 0);
    }
}

void LottieWidget::configureSurfaceFormat()
{
    QSurfaceFormat f;
    f.setRenderableType(QSurfaceFormat::OpenGL);
#ifdef Q_OS_MACOS
    f.setVersion(4, 1);
#else
    f.setVersion(3, 3);
#endif
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    f.setSamples(0);
    QSurfaceFormat::setDefaultFormat(f);
}

bool LottieWidget::setDocument(const LottieDocument& document, bool preserve, QString* error)
{
    if (error) error->clear();
    auto reject = [&](const QString& message) { if (error) *error = message; return false; };
    if (!d->engine->valid) return reject(QStringLiteral("ThorVG 初始化失败。"));
    if (document.isEmpty()) return reject(QStringLiteral("动画文档为空。"));
    const auto bytes = document.json();
    if (bytes.size() > std::numeric_limits<uint32_t>::max())
        return reject(QStringLiteral("动画数据过大。"));
    std::unique_ptr<tvg::Animation> candidate(tvg::Animation::gen());
    const auto alias = d->fontAlias;
    candidate->picture()->resolver([alias](tvg::Paint* paint, const char*, void*) {
        if (paint->type() != tvg::Type::Text || alias.isEmpty()) return false;
        return static_cast<tvg::Text*>(paint)->font(alias.constData()) == tvg::Result::Success;
    }, nullptr);
    const auto loaded = candidate->picture()->load(bytes.constData(), uint32_t(bytes.size()), "lottie+json", nullptr, true);
    if (loaded != tvg::Result::Success) return reject(resultError(QStringLiteral("加载 Lottie"), loaded));
    if (!std::isfinite(candidate->duration()) || candidate->duration() <= 0
        || !std::isfinite(candidate->totalFrame()) || candidate->totalFrame() <= 0)
        return reject(QStringLiteral("ThorVG 未能读取有效的动画时长或帧数。"));

    if (d->canvas) {
        const bool alreadyCurrent = QOpenGLContext::currentContext() == context();
        if (!alreadyCurrent) makeCurrent();
        if (QOpenGLContext::currentContext() != context())
            return reject(QStringLiteral("无法激活 OpenGL 上下文。"));
        d->canvas->sync();
        const auto added = d->canvas->add(candidate->picture());
        if (added != tvg::Result::Success) {
            if (!alreadyCurrent) doneCurrent();
            return reject(resultError(QStringLiteral("添加动画"), added));
        }
        if (d->animation) d->canvas->remove(d->animation->picture());
        // Destroy the old animation while its renderer's context is current.
        d->animation = std::move(candidate);
        if (!alreadyCurrent) doneCurrent();
    } else {
        d->animation = std::move(candidate);
    }
    d->document = document;
    if (d->canvas) { d->error.clear(); d->renderFailed = false; }
    if (!preserve) position_ = 0;
    clock_.restart();
    emit positionChanged(position_);
    update();
    return true;
}

bool LottieWidget::setFontFile(const QString& path, QString* error)
{
    auto reject = [&](const QString& message) { if (error) *error = message; return false; };
    if (error) error->clear();
    if (!d->engine->valid) return reject(QStringLiteral("ThorVG 初始化失败。"));
    QFile font(path);
    if (!font.open(QIODevice::ReadOnly)) return reject(font.errorString());
    if (font.size() > 64 * 1024 * 1024) return reject(QStringLiteral("字体文件超过 64 MB。"));
    const auto bytes = font.readAll();
    static quint64 nextFont = 0;
    const auto alias = QStringLiteral("qt-lottie-preview-%1").arg(++nextFont).toUtf8();
    const auto suffix = QFileInfo(path).suffix().toLatin1();
    const auto loaded = tvg::Text::load(alias.constData(), bytes.constData(), uint32_t(bytes.size()), suffix.constData(), true);
    if (loaded != tvg::Result::Success) return reject(resultError(QStringLiteral("加载字体（请选择 TTF/OTF）"), loaded));
    const auto previous = d->fontAlias;
    d->fontAlias = alias;
    if (!d->document.isEmpty() && !setDocument(d->document, true, error)) {
        d->fontAlias = previous;
        tvg::Text::load(alias.constData(), nullptr, 0);
        return false;
    }
    for (const auto& name : d->fonts) {
        const auto old = name.toUtf8();
        tvg::Text::load(old.constData(), nullptr, 0);
    }
    d->fonts = {QString::fromUtf8(alias)};
    d->fontPath = path;
    return true;
}

QString LottieWidget::fontDescription() const
{
    return d->fontPath.isEmpty() ? QStringLiteral("缺失字体：请使用“预览字体”选择 TTF/OTF。")
        : QStringLiteral("缺失字体回退：%1").arg(QFileInfo(d->fontPath).fileName());
}
QString LottieWidget::gpuDescription() const { return d->gpu; }
QString LottieWidget::lastError() const { return d->error; }

void LottieWidget::setPlaying(bool playing)
{
    if (playing_ == playing) return;
    if (playing && (!d->animation || d->renderFailed)) return;
    if (playing && !looping_ && position_ >= 1) setPosition(0);
    playing_ = playing;
    clock_.restart();
    if (playing) timer_.start(); else timer_.stop();
    emit playingChanged(playing);
}

void LottieWidget::setPosition(double position)
{
    if (!std::isfinite(position)) return;
    position_ = std::clamp(position, 0.0, 1.0);
    clock_.restart();
    emit positionChanged(position_);
    update();
}

void LottieWidget::tick()
{
    const auto elapsed = clock_.nsecsElapsed() / 1e9;
    clock_.restart();
    if (!playing_ || !d->animation) return;
    const auto next = position_ + elapsed / d->animation->duration();
    if (looping_) position_ = std::fmod(next, 1.0);
    else {
        position_ = std::min(next, 1.0);
        if (position_ >= 1) setPlaying(false);
    }
    emit positionChanged(position_);
    update();
}

void LottieWidget::initializeGL()
{
    d->error.clear();
    d->renderFailed = false;
    const auto format = context()->format();
    if (context()->isOpenGLES() || format.majorVersion() < 3
        || (format.majorVersion() == 3 && format.minorVersion() < 3)) {
        fail(QStringLiteral("GPU 后端需要桌面 OpenGL 3.3 或以上；请检查显卡驱动。"));
        return;
    }
    d->canvas.reset(tvg::GlCanvas::gen());
    if (!d->canvas) { fail(QStringLiteral("无法创建 ThorVG OpenGL GPU 后端。")); return; }
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &LottieWidget::cleanupGL,
            Qt::DirectConnection);
    if (!d->animation && !d->document.isEmpty()) {
        QString error;
        if (!setDocument(d->document, true, &error)) { fail(error); return; }
    } else if (d->animation) {
        const auto result = d->canvas->add(d->animation->picture());
        if (result != tvg::Result::Success) { fail(resultError(QStringLiteral("添加动画"), result)); return; }
    }
    auto* gl = context()->functions();
    d->gpu = QStringLiteral("GPU · %1 · OpenGL %2.%3")
        .arg(QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER))))
        .arg(format.majorVersion()).arg(format.minorVersion());
    emit gpuReady(d->gpu);
}

void LottieWidget::paintGL()
{
    auto* gl = context()->functions();
    const int w = qRound(width() * devicePixelRatioF());
    const int h = qRound(height() * devicePixelRatioF());
    // Qt and ThorVG share a context. Establish state explicitly each frame.
    gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    gl->glViewport(0, 0, w, h);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_CULL_FACE);
    gl->glDisable(GL_STENCIL_TEST);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_RASTERIZER_DISCARD);
    gl->glDisable(GL_FRAMEBUFFER_SRGB);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->glDepthMask(GL_TRUE);
    gl->glStencilMask(0xff);
    gl->glClearColor(0.06f, 0.075f, 0.105f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (!d->canvas || !d->animation || d->renderFailed || w <= 0 || h <= 0) return;

    void* nativeContext = context();
#ifdef Q_OS_WIN
    // ThorVG compares this handle with wglGetCurrentContext(); it needs HGLRC.
    const auto* wgl = context()->nativeInterface<QNativeInterface::QWGLContext>();
    if (!wgl) { fail(QStringLiteral("需要 Windows 桌面 WGL 上下文，不能使用 ANGLE。")); return; }
    nativeContext = wgl->nativeContext();
#endif
    auto result = d->canvas->target(nullptr, nullptr, nativeContext, int32_t(defaultFramebufferObject()),
                                    uint32_t(w), uint32_t(h), tvg::ColorSpace::ABGR8888S);
    if (result != tvg::Result::Success) { fail(resultError(QStringLiteral("设置 GPU 目标"), result)); return; }
    const auto size = d->document.size();
    const float scale = std::min(float(w) / size.width(), float(h) / size.height());
    const tvg::Matrix matrix{scale, 0, (w - size.width() * scale) / 2,
                            0, scale, (h - size.height() * scale) / 2, 0, 0, 1};
    d->animation->picture()->transform(matrix);
    const auto total = d->animation->totalFrame();
    const auto frame = float(std::min(position_ * total, double(std::nextafter(total, 0.0f))));
    result = d->animation->frame(frame);
    if (result != tvg::Result::Success && result != tvg::Result::InsufficientCondition) {
        fail(resultError(QStringLiteral("更新动画帧"), result)); return;
    }
    result = d->canvas->draw(false);
    if (result != tvg::Result::Success) { fail(resultError(QStringLiteral("GPU 绘制"), result)); return; }
    result = d->canvas->sync();
    if (result != tvg::Result::Success) { fail(resultError(QStringLiteral("GPU 同步"), result)); return; }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    ++renderedFrames_;
}

void LottieWidget::cleanupGL()
{
    if (!d->canvas) return;
    makeCurrent();
    d->canvas->sync();
    // Animation paints retain their renderer. Release them BEFORE the canvas,
    // while the old context is still current. JSON and playback state survive.
    d->animation.reset();
    d->canvas.reset();
    doneCurrent();
}

void LottieWidget::fail(const QString& message)
{
    d->renderFailed = true;
    setPlaying(false);
    if (d->error == message) return;
    d->error = message;
    emit renderError(message);
}
