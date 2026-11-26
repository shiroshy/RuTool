#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <algorithm>
#include <cstring>

class FixtureReply final : public QNetworkReply {
    QByteArray body_;
    qint64 offset_{};
public:
    FixtureReply(const QNetworkRequest& request, QByteArray body, int status, bool badRange, QObject* parent)
        : QNetworkReply(parent), body_(std::move(body)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentLengthHeader, body_.size());
        if (status == 302) setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(QStringLiteral("https://evil.invalid/file")));
        if (status == 206) setRawHeader("Content-Range", badRange ? QByteArray("invalid") : QByteArray("bytes 3-") + QByteArray::number(body_.size() + 2) + '/' + QByteArray::number(body_.size() + 3));
        QTimer::singleShot(0, this, [this] {
            emit metaDataChanged();
            if (isFinished()) return;
            emit readyRead();
            if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, QStringLiteral("aborted")); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, static_cast<qint64>(body_.size()) - offset_);
        if (count <= 0) return -1;
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(count)); offset_ += count; return count;
    }
};
class FixtureTransport final : public QNetworkAccessManager {
public:
    QByteArray payload{"download fixture bytes"};
    QByteArray authorization;
    int requests{};
    int status{200}; bool badRange{};
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        authorization = request.rawHeader("Authorization");
        return new FixtureReply(request, status == 206 ? payload.mid(3) : payload, status, badRange, this);
    }
};
