#include "shell/file_loader.h"

#include "core/json_node.h"
#include "core/parser.h"

#include <QFile>
#include <QFileInfo>

using namespace jsontitan::core;

// ---------------------------------------------------------------------------
// FileLoaderWorker
// ---------------------------------------------------------------------------

FileLoaderWorker::FileLoaderWorker(QObject* parent)
    : QObject(parent) {}

void FileLoaderWorker::process(const QString& filePath) {
    m_cancelled.store(false, std::memory_order_relaxed);

    QFile file(filePath);
    if (!file.exists()) {
        emit parseError(QStringLiteral("File not found: %1").arg(filePath));
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit parseError(QStringLiteral("Cannot open file: %1 — %2")
                            .arg(filePath, file.errorString()));
        return;
    }

    const qint64 totalSize = file.size();
    if (totalSize == 0) {
        emit parseError(QStringLiteral("File is empty: %1").arg(filePath));
        return;
    }

    constexpr qint64 chunkSize = 64 * 1024; // 64 KB chunks
    auto state = makeParserState();
    qint64 bytesRead = 0;
    int lastReportedProgress = -1;

    while (!file.atEnd()) {
        // Check cancellation between chunks
        if (m_cancelled.load(std::memory_order_relaxed)) {
            return; // Cancelled — emit no result
        }

        QByteArray rawChunk = file.read(chunkSize);
        if (rawChunk.isEmpty()) {
            break;
        }

        bytesRead += rawChunk.size();

        // Convert QByteArray to span<const std::byte>
        auto chunkSpan = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(rawChunk.constData()),
            static_cast<std::size_t>(rawChunk.size()));

        auto result = parseChunk(*state, chunkSpan);

        if (result.error) {
            emit parseError(QStringLiteral("Parse error at byte %1: %2")
                                .arg(result.error->byteOffset)
                                .arg(QString::fromStdString(result.error->description)));
            return;
        }

        state = std::move(result.nextState);

        // Report progress
        int progress = static_cast<int>((bytesRead * 100) / totalSize);
        if (progress != lastReportedProgress) {
            lastReportedProgress = progress;
            emit progressUpdated(progress);
        }
    }

    // Check cancellation one final time before finalizing
    if (m_cancelled.load(std::memory_order_relaxed)) {
        return;
    }

    auto finalResult = finalizeParse(*state);

    if (finalResult.error) {
        emit parseError(QStringLiteral("Parse error at byte %1: %2")
                            .arg(finalResult.error->byteOffset)
                            .arg(QString::fromStdString(finalResult.error->description)));
        return;
    }

    emit parseComplete(std::move(finalResult.root));
}

void FileLoaderWorker::cancel() {
    m_cancelled.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// FileLoader
// ---------------------------------------------------------------------------

FileLoader::FileLoader(QObject* parent)
    : QObject(parent)
    , m_workerThread(new QThread(this))
    , m_worker(new FileLoaderWorker()) {
    // Move worker to the background thread
    m_worker->moveToThread(m_workerThread);

    // Connect worker signals to FileLoader signals (queued across thread boundary)
    connect(m_worker, &FileLoaderWorker::progressUpdated,
            this, &FileLoader::progressUpdated, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::parseComplete,
            this, &FileLoader::parseComplete, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::parseError,
            this, &FileLoader::parseError, Qt::QueuedConnection);

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
    // Cancel any in-progress parse
    cancelParse();

    // Invoke worker's process slot on the worker thread
    QMetaObject::invokeMethod(m_worker, "process",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
}

void FileLoader::cancelParse() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "cancel",
                                  Qt::QueuedConnection);
    }
}
