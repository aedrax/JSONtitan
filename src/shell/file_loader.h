#pragma once

#include <QObject>
#include <QMetaType>
#include <QThread>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

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
    // Multi-file union parse: reads and parses each file in turn (progress
    // mapped to the file's slice of [0,100]), converts each tree out of its
    // arena immediately (so at most one arena is alive at a time), then
    // unions the trees. Emits unionParseError naming the offending file and
    // stops on the first failure.
    void processUnion(const QStringList& filePaths, quint64 requestId = 0);
    void cancel();

signals:
    void progressUpdated(int percentage, quint64 requestId);
    void arenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                            quint64 requestId);
    void parseError(QString errorMessage, quint64 requestId);
    void unionParseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root,
                            QStringList filePaths, quint64 requestId);
    void unionParseError(QString fileName, QString errorMessage,
                         quint64 requestId);

private:
    // Chunked, error-checked whole-file read shared by process() and
    // processUnion(). Progress is reported through onProgress as a 0.0–1.0
    // fraction (0.0 once the pre-read checks pass, 1.0 after the last
    // chunk). Returns false on failure with errorMessage set — or with
    // errorMessage EMPTY when the request went stale mid-read.
    bool readWholeFile(const QString& filePath, quint64 requestId,
                       std::string& out, QString& errorMessage,
                       const std::function<void(float)>& onProgress);

    std::atomic<quint64> m_latestRequest{0};
};

class FileLoader : public QObject {
    Q_OBJECT
public:
    explicit FileLoader(QObject* parent = nullptr);
    ~FileLoader() override;

    void startParse(const QString& filePath);
    // Start a multi-file union parse on the worker thread. Supersedes any
    // in-flight request (single-file or union) exactly like startParse.
    void startUnionParse(const QStringList& filePaths);
    void cancelParse();

signals:
    void progressUpdated(int percentage);
    void arenaParseComplete(std::shared_ptr<jsontitan::core::ArenaParseResult> result);
    void parseError(QString errorMessage);
    void unionParseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root,
                            QStringList filePaths);
    void unionParseError(QString fileName, QString errorMessage);

private:
    QThread* m_workerThread = nullptr;
    FileLoaderWorker* m_worker = nullptr;
    // Id of the most recently started request; results tagged with any other
    // id are stale and dropped instead of being forwarded.
    quint64 m_activeRequest = 0;
};
