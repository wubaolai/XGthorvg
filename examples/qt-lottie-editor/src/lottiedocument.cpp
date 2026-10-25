#include "lottiedocument.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QSaveFile>
#include <QUrl>
#include <cmath>

namespace {
constexpr int maxDimension = 16384;
constexpr qint64 maxPixels = 64 * 1024 * 1024;

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool validDimension(double value)
{
    return std::isfinite(value) && value > 0 && value <= maxDimension && std::floor(value) == value;
}

bool validSize(const QSize& size)
{
    return validDimension(size.width()) && validDimension(size.height())
        && qint64(size.width()) * size.height() <= maxPixels;
}

QJsonValue atPath(QJsonValue value, const QStringList& path)
{
    for (const auto& component : path) {
        if (value.isArray()) value = value.toArray().at(component.toInt());
        else value = value.toObject().value(component);
    }
    return value;
}

QJsonValue replaceAt(QJsonValue value, const QStringList& path, int depth, const QJsonValue& replacement)
{
    if (depth == path.size()) return replacement;
    if (value.isArray()) {
        auto array = value.toArray();
        const int index = path[depth].toInt();
        array[index] = replaceAt(array[index], path, depth + 1, replacement);
        return array;
    }
    auto object = value.toObject();
    const auto& key = path[depth];
    object[key] = replaceAt(object.value(key), path, depth + 1, replacement);
    return object;
}

void collectLayers(const QJsonArray& layers, const QStringList& path, const QString& prefix,
                   QList<LottieDocument::Entry>& entries)
{
    for (int i = 0; i < layers.size(); ++i) {
        const auto layer = layers[i].toObject();
        const auto layerPath = path + QStringList{QString::number(i)};
        const auto name = layer.value("nm").toString(QString("Layer %1").arg(i + 1));
        const auto label = prefix + name;
        if (layer.value("ty").toInt(-1) == 5) {
            const auto documents = layer.value("t").toObject()["d"].toObject()["k"].toArray();
            bool hasText = false;
            for (const auto& key : documents) {
                if (key.toObject()["s"].toObject()["t"].isString()) { hasText = true; break; }
            }
            if (hasText) entries.append({LottieDocument::Entry::Kind::Text, label, layerPath});
        }
        if (layer.value("layers").isArray())
            collectLayers(layer.value("layers").toArray(), layerPath + QStringList{"layers"}, label + " / ", entries);
    }
}

QImage readImage(QByteArray data, QString* error)
{
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (!validSize(reader.size())) {
        fail(error, QStringLiteral("图片尺寸无效或过大（单边最多 16384，最多 64M 像素）。"));
        return {};
    }
    const auto result = reader.read();
    if (result.isNull()) fail(error, QStringLiteral("无法解码图片：%1").arg(reader.errorString()));
    return result;
}

QImage readAsset(const QJsonObject& asset, const QString& baseDir, QString* error)
{
    const auto path = asset.value("p").toString();
    QByteArray data;
    if (path.startsWith("data:")) {
        const auto comma = path.indexOf(',');
        if (comma < 0 || !path.left(comma).endsWith(";base64")) {
            fail(error, QStringLiteral("图片资源 %1 不是有效的 base64 数据。" ).arg(asset.value("id").toString()));
            return {};
        }
        const auto decoded = QByteArray::fromBase64Encoding(path.mid(comma + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            fail(error, QStringLiteral("图片资源 %1 的 base64 数据无效。" ).arg(asset.value("id").toString()));
            return {};
        }
        data = decoded.decoded;
    } else {
        const auto reference = asset.value("u").toString() + path;
        const QUrl url(reference);
        if (!url.scheme().isEmpty() && !url.isLocalFile() && !QDir::isAbsolutePath(reference)) {
            fail(error, QStringLiteral("不支持远程图片资源：%1。请先下载到本地。" ).arg(reference));
            return {};
        }
        const auto filename = url.isLocalFile() ? url.toLocalFile() : QDir(baseDir).absoluteFilePath(reference);
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(error, QStringLiteral("无法打开图片 %1：%2").arg(filename, file.errorString()));
            return {};
        }
        if (file.size() > 128 * 1024 * 1024) {
            fail(error, QStringLiteral("图片文件过大：%1").arg(filename));
            return {};
        }
        data = file.readAll();
    }
    return readImage(data, error);
}

bool embedImage(QJsonObject& asset, const QImage& image, QString* error)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return fail(error, QStringLiteral("无法将图片编码为 PNG。"));
    asset["p"] = "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
    asset["u"] = QString();
    asset["e"] = 1;
    return true;
}
}

bool LottieDocument::load(const QByteArray& data, const QString& baseDir, QString* error)
{
    if (error) error->clear();
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("JSON 解析失败（位置 %1）：%2").arg(parseError.offset).arg(parseError.errorString()));
    if (!parsed.isObject()) return fail(error, QStringLiteral("Lottie JSON 顶层必须是对象。"));
    auto candidate = parsed.object();
    if (!validDimension(candidate.value("w").toDouble()) || !validDimension(candidate.value("h").toDouble())
        || candidate.value("w").toDouble() * candidate.value("h").toDouble() > maxPixels)
        return fail(error, QStringLiteral("动画尺寸无效或过大（单边最多 16384，最多 64M 像素）。"));
    const auto rate = candidate.value("fr").toDouble();
    const auto first = candidate.value("ip").toDouble();
    const auto last = candidate.value("op").toDouble();
    if (!std::isfinite(rate) || rate <= 0 || !candidate.value("ip").isDouble()
        || !candidate.value("op").isDouble() || !std::isfinite(first) || !std::isfinite(last)
        || !std::isfinite(last - first) || last <= first)
        return fail(error, QStringLiteral("动画的 fr、ip、op 无效；帧率必须大于零，结束帧必须大于开始帧。"));
    if (!candidate.value("layers").isArray()) return fail(error, QStringLiteral("Lottie JSON 缺少 layers 数组。"));
    if (candidate.contains("assets") && !candidate.value("assets").isArray())
        return fail(error, QStringLiteral("assets 必须是数组。"));

    QList<Entry> inventory;
    collectLayers(candidate.value("layers").toArray(), {"layers"}, {}, inventory);
    auto assets = candidate.value("assets").toArray();
    for (int i = 0; i < assets.size(); ++i) {
        auto asset = assets[i].toObject();
        const auto path = QStringList{"assets", QString::number(i)};
        if (asset.value("layers").isArray()) {
            collectLayers(asset.value("layers").toArray(), path + QStringList{"layers"}, asset.value("id").toString() + " / ", inventory);
        } else if (asset.value("p").isString() && !asset.value("p").toString().isEmpty()) {
            const QSize dimensions(asset.value("w").toInt(), asset.value("h").toInt());
            if (!validDimension(asset.value("w").toDouble()) || !validDimension(asset.value("h").toDouble()) || !validSize(dimensions))
                return fail(error, QStringLiteral("图片资源 %1 的宽高无效。" ).arg(asset.value("id").toString()));
            const auto decoded = readAsset(asset, baseDir, error);
            if (decoded.isNull()) return false;
            if (!embedImage(asset, decoded, error)) return false;
            assets[i] = asset;
            inventory.append({Entry::Kind::Image, asset.value("nm").toString(asset.value("id").toString(QString("Image %1").arg(i + 1))), path});
        }
    }
    if (candidate.contains("assets")) candidate["assets"] = assets;
    m_root = candidate;
    m_entries = inventory;
    return true;
}

QList<LottieDocument::Entry> LottieDocument::entries() const { return m_entries; }

QString LottieDocument::text(int index) const
{
    if (index < 0 || index >= m_entries.size() || m_entries[index].kind != Entry::Kind::Text) return {};
    const auto layer = atPath(m_root, m_entries[index].path).toObject();
    const auto documents = layer.value("t").toObject()["d"].toObject()["k"].toArray();
    for (const auto& key : documents) {
        const auto value = key.toObject()["s"].toObject()["t"];
        if (value.isString()) return value.toString().replace('\r', '\n');
    }
    return {};
}

QImage LottieDocument::image(int index) const
{
    if (index < 0 || index >= m_entries.size() || m_entries[index].kind != Entry::Kind::Image) return {};
    return readAsset(atPath(m_root, m_entries[index].path).toObject(), {}, nullptr);
}

bool LottieDocument::replaceText(int index, const QString& value, QString* error)
{
    if (error) error->clear();
    if (index < 0 || index >= m_entries.size() || m_entries[index].kind != Entry::Kind::Text)
        return fail(error, QStringLiteral("请选择可编辑的文字图层。"));
    const auto keyPath = m_entries[index].path + QStringList{"t", "d", "k"};
    auto documents = atPath(m_root, keyPath).toArray();
    auto normalized = value;
    normalized.replace("\r\n", "\n").replace('\r', '\n').replace('\n', '\r');
    for (int i = 0; i < documents.size(); ++i) {
        auto key = documents[i].toObject();
        if (!key.value("s").isObject()) continue;
        auto style = key.value("s").toObject();
        style["t"] = normalized;
        key["s"] = style;
        documents[i] = key;
    }
    auto candidate = replaceAt(m_root, keyPath, 0, documents).toObject();
    // Text document slots can otherwise mask the edited keyframes.
    const auto documentPath = m_entries[index].path + QStringList{"t", "d"};
    auto document = atPath(candidate, documentPath).toObject();
    document.remove("sid");
    m_root = replaceAt(candidate, documentPath, 0, document).toObject();
    return true;
}

bool LottieDocument::replaceImage(int index, const QImage& replacement, QString* error)
{
    if (error) error->clear();
    if (index < 0 || index >= m_entries.size() || m_entries[index].kind != Entry::Kind::Image)
        return fail(error, QStringLiteral("请选择图片资源。"));
    if (!validSize(replacement.size())) return fail(error, QStringLiteral("替换图片无效或过大。"));
    auto asset = atPath(m_root, m_entries[index].path).toObject();
    const QSize dimensions(asset.value("w").toInt(), asset.value("h").toInt());
    QImage normalized(dimensions, QImage::Format_ARGB32_Premultiplied);
    if (normalized.isNull()) return fail(error, QStringLiteral("无法分配图片内存。"));
    normalized.fill(Qt::transparent);
    const auto scaled = replacement.scaled(dimensions, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) return fail(error, QStringLiteral("无法缩放图片。"));
    {
        QPainter painter(&normalized);
        painter.drawImage(QPoint((dimensions.width() - scaled.width()) / 2, (dimensions.height() - scaled.height()) / 2), scaled);
    }
    if (!embedImage(asset, normalized, error)) return false;
    // An existing slot override would take precedence over this explicit edit.
    asset.remove("sid");
    m_root = replaceAt(m_root, m_entries[index].path, 0, asset).toObject();
    return true;
}

QByteArray LottieDocument::json() const
{
    return isEmpty() ? QByteArray() : QJsonDocument(m_root).toJson(QJsonDocument::Compact);
}

bool LottieDocument::save(const QString& filename, QString* error) const
{
    if (error) error->clear();
    if (isEmpty()) return fail(error, QStringLiteral("没有可保存的动画。"));
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("无法保存 %1：%2").arg(filename, file.errorString()));
    const auto data = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
        return fail(error, QStringLiteral("保存失败：%1").arg(file.errorString()));
    return true;
}

QSize LottieDocument::size() const { return {m_root.value("w").toInt(), m_root.value("h").toInt()}; }
double LottieDocument::frameRate() const { return m_root.value("fr").toDouble(); }
double LottieDocument::frameCount() const { return m_root.value("op").toDouble() - m_root.value("ip").toDouble(); }
bool LottieDocument::isEmpty() const { return m_root.isEmpty(); }
