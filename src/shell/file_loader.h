#pragma once

#include <QObject>
#include <QMetaType>
#include <QThread>
#include <QString>

#include <atomic>
#include <memory>

namespace jsontitan::core {
struct JsonNode;
struct ArenaParseResult;
}

Q_DECLARE_METATYPE(std::shared_ptr<const jsontitan::core::JsonNode>)
Q_DECLARE_METATYPE(std::shared_ptr<jsontitan::core::ArenaParseResult>)

class FileLoaderWorker : public QObject {
    Q_OBJECT
public:
    explicit FileLoaderWorker(QObject* parent = nullptr);

    // Thread-safe (atomic): start a new request, invalidating any in-flight
    // parse. Returns the id to pass to process(). Never call cancel via a
    // queued slot — the worker's event loop is blocked while process() runs,
    // so a queued call could not be delivered until the parse finished.
    quint64 beginRequest();
    // Thread-safe (atomic): invalidate all in-flight requests.
    void invalidateAll();
    // Thread-safe: true if requestId no longer identifies the latest request.
    bool isStale(quint64 requestId) const;

public slots:
    void process(const QString& filePath, quint64 requestId = 0);
    void cancel();

signals:
    void progressUpdated(int percentage, quint64 requestId);
    void parseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root);
    void arenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                            quint64 requestId);
    void parseError(QString errorMessage, quint64 requestId);

private:
    std::atomic<quint64> m_latestRequest{0};
};

class FileLoader : public QObject {
    Q_OBJECT
public:
    explicit FileLoader(QObject* parent = nullptr);
    ~FileLoader() override;

    void startParse(const QString& filePath);
    void cancelParse();

signals:
    void progressUpdated(int percentage);
    void parseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root);
    void arenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result);
    void parseError(QString errorMessage);

private:
    QThread* m_workerThread = nullptr;
    FileLoaderWorker* m_worker = nullptr;
    // Id of the most recently started request; results tagged with any other
    // id are stale and dropped instead of being forwarded.
    quint64 m_activeRequest = 0;
};
