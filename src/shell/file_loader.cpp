#include "shell/file_loader.h"

#include "core/parse_orchestrator.h"

#include <QFile>
#include <QFileInfo>

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// FileLoaderWorker
// ---------------------------------------------------------------------------

FileLoaderWorker::FileLoaderWorker(QObject* parent)
    : QObject(parent) {}

void FileLoaderWorker::process(const QString& filePath, quint64 requestId) {
    if (isStale(requestId)) {
        return;
    }

    QFile file(filePath);
    if (!file.exists()) {
        emit parseError(QStringLiteral("File not found: %1").arg(filePath), requestId);
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit parseError(QStringLiteral("Cannot open file: %1 — %2")
                            .arg(filePath, file.errorString()), requestId);
        return;
    }

    const qint64 totalSize = file.size();
    if (totalSize == 0) {
        emit parseError(QStringLiteral("File is empty: %1").arg(filePath), requestId);
        return;
    }

    emit progressUpdated(0, requestId);

    // Phase 1: Read file in chunks with incremental progress
    constexpr qint64 kReadChunkSize = 1024 * 1024;  // 1 MB chunks
    std::string input;
    input.reserve(static_cast<std::size_t>(totalSize));

    qint64 bytesRead = 0;
    int lastProgress = 0;

    while (bytesRead < totalSize) {
        if (isStale(requestId)) {
            return;
        }

        QByteArray chunk = file.read(kReadChunkSize);
        if (chunk.isEmpty()) {
            break;  // EOF or error
        }

        input.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        bytesRead += chunk.size();

        // Calculate progress in [0, 50] range
        int progress = static_cast<int>((bytesRead * 50) / totalSize);
        if (progress != lastProgress) {
            emit progressUpdated(progress, requestId);
            lastProgress = progress;
        }
    }

    // Ensure we emit 50 at the end of read phase
    if (lastProgress != 50) {
        emit progressUpdated(50, requestId);
    }

    // Cancellation check after read
    if (isStale(requestId)) {
        return;
    }

    // Phase 2: Parse via optimized pipeline with progress reporting
    ParseBufferOptions parseOptions;
    int lastParseProgress = 50;
    parseOptions.progressCallback = [this, requestId, &lastParseProgress](float coreProgress) {
        if (isStale(requestId)) {
            return;
        }
        // Map core's 0.0–1.0 to shell's 50–100 range
        int progress = 50 + static_cast<int>(coreProgress * 50);
        if (progress != lastParseProgress) {
            emit progressUpdated(progress, requestId);
            lastParseProgress = progress;
        }
    };
    // Abort the parse itself (not just the surrounding phases) once superseded.
    parseOptions.cancelCallback = [this, requestId]() {
        return isStale(requestId);
    };

    auto arenaResult = parseBuffer(std::move(input), parseOptions);

    if (isStale(requestId)) {
        return;
    }

    if (arenaResult.error) {
        emit parseError(QStringLiteral("Parse error at byte %1: %2")
                            .arg(arenaResult.error->byteOffset)
                            .arg(QString::fromStdString(arenaResult.error->description)),
                        requestId);
        return;
    }

    // Phase 3: Wrap in shared_ptr and emit directly (no deep-copy)
    auto sharedResult = std::make_shared<ArenaParseResult>(std::move(arenaResult));

    emit progressUpdated(100, requestId);
    emit arenaParseComplete(std::move(sharedResult), requestId);
}

quint64 FileLoaderWorker::beginRequest() {
    return m_latestRequest.fetch_add(1, std::memory_order_relaxed) + 1;
}

void FileLoaderWorker::invalidateAll() {
    m_latestRequest.fetch_add(1, std::memory_order_relaxed);
}

bool FileLoaderWorker::isStale(quint64 requestId) const {
    return requestId != m_latestRequest.load(std::memory_order_relaxed);
}

void FileLoaderWorker::cancel() {
    invalidateAll();
}

// ---------------------------------------------------------------------------
// FileLoader
// ---------------------------------------------------------------------------

FileLoader::FileLoader(QObject* parent)
    : QObject(parent)
    , m_workerThread(new QThread(this))
    , m_worker(new FileLoaderWorker()) {
    // Register metatype for cross-thread signal/slot
    qRegisterMetaType<std::shared_ptr<jsontitan::core::ArenaParseResult>>();

    // Move worker to the background thread
    m_worker->moveToThread(m_workerThread);

    // Connect worker signals to FileLoader signals (queued across thread
    // boundary). Results are tagged with a request id; anything not matching
    // the latest startParse() is a stale in-flight parse and is dropped here,
    // so consumers never see results for a superseded file.
    connect(m_worker, &FileLoaderWorker::progressUpdated, this,
            [this](int percentage, quint64 requestId) {
                if (requestId == m_activeRequest) {
                    emit progressUpdated(percentage);
                }
            }, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::parseComplete,
            this, &FileLoader::parseComplete, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::arenaParseComplete, this,
            [this](std::shared_ptr<jsontitan::core::ArenaParseResult> result,
                   quint64 requestId) {
                if (requestId == m_activeRequest) {
                    emit arenaParseComplete(std::move(result));
                }
            }, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::parseError, this,
            [this](const QString& errorMessage, quint64 requestId) {
                if (requestId == m_activeRequest) {
                    emit parseError(errorMessage);
                }
            }, Qt::QueuedConnection);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

FileLoader::~FileLoader() {
    cancelParse();
    m_workerThread->quit();
    m_workerThread->wait();
}

void FileLoader::startParse(const QString& filePath) {
    // beginRequest() atomically invalidates any in-progress parse (it aborts
    // at its next cancellation check) and returns the id for the new one.
    m_activeRequest = m_worker->beginRequest();

    // Invoke worker's process slot on the worker thread
    QMetaObject::invokeMethod(m_worker, "process",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath),
                              Q_ARG(quint64, m_activeRequest));
}

void FileLoader::cancelParse() {
    if (m_worker) {
        // Direct atomic store — never queue this: the worker's event loop is
        // blocked inside process() for the whole parse, so a queued cancel
        // would only be delivered after the parse already finished.
        m_worker->invalidateAll();
        m_activeRequest = 0;
    }
}
