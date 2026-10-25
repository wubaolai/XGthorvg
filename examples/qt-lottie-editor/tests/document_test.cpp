#include "lottiedocument.h"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QJsonObject textLayer(const QString& name, const QString& first, const QString& second = {})
{
    QJsonArray keys{QJsonObject{{"t", 0}, {"s", QJsonObject{{"t", first}, {"f", "DemoFont"}, {"s", 42}, {"fc", QJsonArray{1, 0, 0}}}}}};
    if (!second.isNull()) keys.append(QJsonObject{{"t", 30}, {"s", QJsonObject{{"t", second}, {"f", "DemoFont"}, {"s", 36}}}});
    return {{"ty", 5}, {"nm", name}, {"t", QJsonObject{{"d", QJsonObject{{"k", keys}}}}}};
}
QJsonObject base()
{
    return {{"v", "5.7.0"}, {"w", 640}, {"h", 360}, {"fr", 30}, {"ip", 10}, {"op", 100},
            {"layers", QJsonArray{textLayer("Title", "Old", "Later")}},
            {"assets", QJsonArray{QJsonObject{{"id", "comp"}, {"layers", QJsonArray{textLayer("Nested", "Inside")}}}}}};
}
QByteArray encode(const QJsonObject& obj) { return QJsonDocument(obj).toJson(QJsonDocument::Compact); }
QJsonObject root(const LottieDocument& doc) { return QJsonDocument::fromJson(doc.json()).object(); }
QJsonArray keys(const QJsonObject& layer) { return layer["t"].toObject()["d"].toObject()["k"].toArray(); }
QByteArray png(const QImage& image)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}
}

class DocumentTest : public QObject
{
    Q_OBJECT
private slots:
    void inventoriesNestedText();
    void changesEveryKeyframePreservingStyle();
    void embedsReplacementAndPreservesDimensions();
    void embedsLocalImagesAndCanSaveElsewhere();
    void preservesUneditedImageSlotBindings();
    void readOnlyProbesDoNotInsertNullFields();
    void missingImagesKeepPriorDocument();
    void rejectsInvalidDocuments_data();
    void rejectsInvalidDocuments();
    void invalidEditsKeepDocument();
};

void DocumentTest::inventoriesNestedText()
{
    LottieDocument doc;
    QString error;
    QVERIFY2(doc.load(encode(base()), {}, &error), qPrintable(error));
    QCOMPARE(doc.entries().size(), 2);
    QCOMPARE(doc.entries()[0].kind, LottieDocument::Entry::Kind::Text);
    QCOMPARE(doc.text(0), QString("Old"));
    QCOMPARE(doc.text(1), QString("Inside"));
    QVERIFY(doc.entries()[1].label.contains("Nested"));
    QCOMPARE(doc.size(), QSize(640, 360));
    QCOMPARE(doc.frameRate(), 30.0);
    QCOMPARE(doc.frameCount(), 90.0);
    QVERIFY(!doc.isEmpty());
}

void DocumentTest::changesEveryKeyframePreservingStyle()
{
    LottieDocument doc;
    QVERIFY(doc.load(encode(base()), {}));
    const auto originalKeys = keys(root(doc)["layers"].toArray()[0].toObject());
    QVERIFY(doc.replaceText(0, QString::fromUtf8("新标题\nsecond line")));
    const auto editedKeys = keys(root(doc)["layers"].toArray()[0].toObject());
    QCOMPARE(editedKeys.size(), originalKeys.size());
    for (int i = 0; i < editedKeys.size(); ++i) {
        QCOMPARE(editedKeys[i].toObject()["t"], originalKeys[i].toObject()["t"]);
        auto originalStyle = originalKeys[i].toObject()["s"].toObject();
        auto editedStyle = editedKeys[i].toObject()["s"].toObject();
        QCOMPARE(editedStyle.take("t").toString(), QString::fromUtf8("新标题\rsecond line"));
        originalStyle.remove("t");
        QCOMPARE(editedStyle, originalStyle);
    }
    QCOMPARE(doc.text(1), QString("Inside"));
    QVERIFY(doc.replaceText(1, "Nested changed"));
    QCOMPARE(doc.text(1), QString("Nested changed"));
}

void DocumentTest::embedsReplacementAndPreservesDimensions()
{
    auto source = base();
    QImage original(80, 40, QImage::Format_ARGB32);
    original.fill(Qt::red);
    auto assets = source["assets"].toArray();
    assets.append(QJsonObject{{"id", "hero"}, {"w", 80}, {"h", 40}, {"e", 1}, {"p", "data:image/png;base64," + QString::fromLatin1(png(original).toBase64())}});
    source["assets"] = assets;
    LottieDocument doc;
    QVERIFY(doc.load(encode(source), {}));
    const int imageIndex = doc.entries().size() - 1;
    QCOMPARE(doc.entries()[imageIndex].kind, LottieDocument::Entry::Kind::Image);
    QImage replacement(40, 80, QImage::Format_ARGB32);
    replacement.fill(Qt::green);
    QVERIFY(doc.replaceImage(imageIndex, replacement));
    const auto asset = root(doc)["assets"].toArray().last().toObject();
    QCOMPARE(asset["w"].toInt(), 80);
    QCOMPARE(asset["h"].toInt(), 40);
    QCOMPARE(asset["e"].toInt(), 1);
    QCOMPARE(asset["u"].toString(), QString());
    QVERIFY(asset["p"].toString().startsWith("data:image/png;base64,"));
    const auto decoded = doc.image(imageIndex);
    QCOMPARE(decoded.size(), QSize(80, 40));
    QCOMPARE(decoded.pixelColor(40, 20), QColor(Qt::green));
    QCOMPARE(decoded.pixelColor(0, 0).alpha(), 0);
}

void DocumentTest::preservesUneditedImageSlotBindings()
{
    auto source = base();
    QImage original(20, 20, QImage::Format_ARGB32);
    original.fill(Qt::red);
    source["assets"] = QJsonArray{QJsonObject{{"id", "hero"}, {"sid", "image-slot"}, {"w", 20}, {"h", 20},
        {"p", "data:image/png;base64," + QString::fromLatin1(png(original).toBase64())}, {"e", 1}}};
    LottieDocument doc;
    QVERIFY(doc.load(encode(source), {}));
    QCOMPARE(root(doc)["assets"].toArray()[0].toObject()["sid"].toString(), QString("image-slot"));
    QVERIFY(doc.replaceImage(doc.entries().size() - 1, original));
    QVERIFY(!root(doc)["assets"].toArray()[0].toObject().contains("sid"));
}

void DocumentTest::readOnlyProbesDoNotInsertNullFields()
{
    auto source = base();
    QImage original(20, 20, QImage::Format_ARGB32);
    original.fill(Qt::red);
    source["assets"] = QJsonArray{QJsonObject{{"id", "hero"}, {"w", 20}, {"h", 20},
        {"p", "data:image/png;base64," + QString::fromLatin1(png(original).toBase64())}, {"e", 1}}};
    LottieDocument doc;
    QVERIFY(doc.load(encode(source), {}));
    const auto asset = root(doc)["assets"].toArray()[0].toObject();
    QVERIFY2(!asset.contains("layers"), "An image asset must not acquire layers:null; ThorVG expects an array if layers exists.");
    QVERIFY(!asset.contains("nm"));
    source.remove("assets");
    QVERIFY(doc.load(encode(source), {}));
    QVERIFY2(!root(doc).contains("assets"), "An absent optional assets array must remain absent.");
}

void DocumentTest::embedsLocalImagesAndCanSaveElsewhere()
{
    QTemporaryDir input, output;
    QVERIFY(input.isValid() && output.isValid());
    QDir(input.path()).mkdir("images");
    QImage original(24, 16, QImage::Format_ARGB32);
    original.fill(Qt::blue);
    QVERIFY(original.save(input.filePath("images/hero.png")));
    auto source = base();
    source["assets"] = QJsonArray{QJsonObject{{"id", "hero"}, {"w", 24}, {"h", 16}, {"u", "images/"}, {"p", "hero.png"}, {"e", 0}}};
    LottieDocument doc;
    QString error;
    QVERIFY2(doc.load(encode(source), input.path(), &error), qPrintable(error));
    QVERIFY(doc.save(output.filePath("edited.json"), &error));
    QFile saved(output.filePath("edited.json"));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    LottieDocument reloaded;
    QVERIFY2(reloaded.load(saved.readAll(), output.path(), &error), qPrintable(error));
    QCOMPARE(reloaded.image(reloaded.entries().size() - 1).pixelColor(2, 2), QColor(Qt::blue));
}

void DocumentTest::missingImagesKeepPriorDocument()
{
    LottieDocument doc;
    QVERIFY(doc.load(encode(base()), {}));
    const auto before = doc.json();
    auto source = base();
    source["assets"] = QJsonArray{QJsonObject{{"id", "missing"}, {"w", 10}, {"h", 10}, {"u", "images/"}, {"p", "does-not-exist.png"}}};
    QString error;
    QVERIFY(!doc.load(encode(source), "/tmp/qt-lottie-no-such-assets", &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(doc.json(), before);
}

void DocumentTest::rejectsInvalidDocuments_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("malformed") << QByteArray("{broken");
    QTest::newRow("not-object") << QByteArray("[]");
    for (const auto& field : {QString("w"), QString("h"), QString("fr")}) {
        auto source = base(); source[field] = 0;
        QTest::newRow(qPrintable(field)) << encode(source);
    }
    auto source = base(); source["op"] = 5;
    QTest::newRow("negative-duration") << encode(source);
    source = base(); source["layers"] = QJsonObject{};
    QTest::newRow("bad-layers") << encode(source);
    source = base(); source["w"] = 1000000;
    QTest::newRow("huge-dimensions") << encode(source);
    source = base(); source["assets"] = QJsonArray{QJsonObject{{"id", "bad"}, {"w", -1}, {"h", 40}, {"p", "data:image/png;base64,garbage"}, {"e", 1}}};
    QTest::newRow("bad-image") << encode(source);
}

void DocumentTest::rejectsInvalidDocuments()
{
    QFETCH(QByteArray, data);
    LottieDocument doc;
    QString error;
    QVERIFY(!doc.load(data, {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(doc.isEmpty());
}

void DocumentTest::invalidEditsKeepDocument()
{
    LottieDocument doc;
    QVERIFY(doc.load(encode(base()), {}));
    const auto before = doc.json();
    QString error;
    QVERIFY(!doc.replaceText(-1, "bad", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!doc.replaceImage(0, QImage(), &error));
    QCOMPARE(doc.json(), before);
    QTemporaryDir output;
    QVERIFY(!doc.save(output.filePath("missing/fail.json"), &error));
    QVERIFY(!error.isEmpty());
    LottieDocument empty;
    QVERIFY(!empty.save(output.filePath("empty.json"), &error));
}

QTEST_GUILESS_MAIN(DocumentTest)
#include "document_test.moc"
