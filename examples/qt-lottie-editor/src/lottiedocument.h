#pragma once
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QStringList>

class LottieDocument
{
public:
    struct Entry {
        enum class Kind { Text, Image };
        Kind kind;
        QString label;
        QStringList path;
    };
    bool load(const QByteArray&, const QString& baseDir, QString* error = nullptr);
    QList<Entry> entries() const;
    QString text(int index) const;
    QImage image(int index) const;
    bool replaceText(int index, const QString&, QString* error = nullptr);
    bool replaceImage(int index, const QImage&, QString* error = nullptr);
    QByteArray json() const;
    bool save(const QString&, QString* error = nullptr) const;
    QSize size() const;
    double frameRate() const;
    double frameCount() const;
    bool isEmpty() const;
private:
    QJsonObject m_root;
    QList<Entry> m_entries;
};
