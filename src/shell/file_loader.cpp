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

    emit progressUpdated(0);

    // Phase 1: Read entire file into memory
    QByteArray raw = file.readAll();
    std::string input(raw.constData(), static_cast<std::size_t>(raw.size()));
    emit progressUpdated(50);

    // Cancellation check after read
    if (m_cancelled.load(std::memory_order_relaxed)) {
        return;
    }

    // Phase 2: Parse via optimized pipeline
    auto arenaResult = parseBuffer(std::move(input));

    if (arenaResult.error) {
        emit parseError(QStringLiteral("Parse error at byte %1: %2")
                            .arg(arenaResult.error->byteOffset)
                            .arg(QString::fromStdString(arenaResult.error->description)));
        return;
    }

    // Phase 3: Convert to public type
    auto result = arenaResult.toParseResult();

    // Cancellation check after parse
    if (m_cancelled.load(std::memory_order_relaxed)) {
        return;
    }

    emit progressUpdated(100);
    emit parseComplete(std::move(result.root));
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
