#include "shell/file_loader.h"

#include "core/json_node.h"
#include "core/parse_orchestrator.h"
#include "core/union_engine.h"

#include <QFile>
#include <QFileInfo>

#include <utility>
#include <vector>

using namespace jsontitan::core;

namespace {

// Computes the 1-based line/column of `byteOffset` by re-reading the file
// from disk (the parsed buffer was moved into parseBuffer and is not
// retained on error). Error path only, so the extra read is acceptable.
// Returns {0, 0} when the file cannot be re-read.
std::pair<qint64, qint64> lineColumnForOffset(const QString& filePath,
                                              qint64 byteOffset) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {0, 0};
    }

    qint64 line = 1;
    qint64 column = 1;
    qint64 remaining = byteOffset;
    constexpr qint64 kChunk = 1 << 20;  // 1 MB
    while (remaining > 0) {
        const QByteArray chunk = file.read(qMin(kChunk, remaining));
        if (chunk.isEmpty()) {
            break;  // file shrank since the parse — report what we have
        }
        remaining -= chunk.size();
        for (const char c : chunk) {
            if (c == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    }
    return {line, column};
}

}  // namespace

// ---------------------------------------------------------------------------
// FileLoaderWorker
// ---------------------------------------------------------------------------

FileLoaderWorker::FileLoaderWorker(QObject* parent)
    : QObject(parent) {}

bool FileLoaderWorker::readWholeFile(
    const QString& filePath, quint64 requestId, std::string& out,
    QString& errorMessage, const std::function<void(float)>& onProgress) {
    errorMessage.clear();

    QFile file(filePath);
    if (!file.exists()) {
        errorMessage = tr("File not found: %1").arg(filePath);
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = tr("Cannot open file: %1 — %2")
                           .arg(filePath, file.errorString());
        return false;
    }

    const qint64 totalSize = file.size();
    if (totalSize == 0) {
        errorMessage = tr("File is empty: %1").arg(filePath);
        return false;
    }

    onProgress(0.0f);

    // Read the file in chunks with incremental progress.
    constexpr qint64 kReadChunkSize = 1024 * 1024;  // 1 MB chunks
    out.clear();
    // Reserve room for the simdjson padding up front: SourceBuffer's
    // constructor resizes by kSimdjsonPadding bytes, and without this slack
    // that resize would reallocate (and copy) the entire multi-GB string.
    out.reserve(static_cast<std::size_t>(totalSize) + kSimdjsonPadding);

    qint64 bytesRead = 0;
    while (bytesRead < totalSize) {
        if (isStale(requestId)) {
            return false;  // errorMessage stays empty: silent abort
        }

        QByteArray chunk = file.read(kReadChunkSize);
        if (chunk.isEmpty()) {
            break;  // EOF or error
        }

        out.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        bytesRead += chunk.size();
        onProgress(static_cast<float>(bytesRead) /
                   static_cast<float>(totalSize));
    }

    // A short read is an I/O error (removable media, network share,
    // permission revoked mid-read): parsing the truncated buffer would show
    // a misleading parse error — or silently display a truncated document.
    if (bytesRead != totalSize || file.error() != QFileDevice::NoError) {
        errorMessage = tr("Failed to read %1: %2")
                           .arg(filePath,
                                file.error() != QFileDevice::NoError
                                    ? file.errorString()
                                    : tr("unexpected end of file"));
        return false;
    }

    onProgress(1.0f);
    return true;
}

void FileLoaderWorker::process(const QString& filePath, quint64 requestId) {
    if (isStale(requestId)) {
        return;
    }

    // Everything below allocates proportionally to the file size; a file
    // larger than available memory must surface as an error dialog, not a
    // std::terminate from an exception escaping the worker slot.
    try {

    // Phase 1: Read file in chunks; fractions map to the [0, 50] range.
    std::string input;
    QString readError;
    int lastProgress = -1;
    const bool readOk = readWholeFile(
        filePath, requestId, input, readError,
        [this, requestId, &lastProgress](float fraction) {
            int progress = static_cast<int>(fraction * 50.0f);
            if (progress != lastProgress) {
                emit progressUpdated(progress, requestId);
                lastProgress = progress;
            }
        });
    if (!readOk) {
        if (!readError.isEmpty()) {
            emit parseError(readError, requestId);
        }
        return;  // error or stale request
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
        const auto byteOffset =
            static_cast<qint64>(arenaResult.error->byteOffset);
        const QString description =
            QString::fromStdString(arenaResult.error->description);
        // byteOffset 0 means "location unavailable" (simdjson does not
        // always know where it failed): no line/column prefix then.
        if (byteOffset > 0) {
            const auto [line, column] =
                lineColumnForOffset(filePath, byteOffset);
            if (line > 0) {
                emit parseError(
                    tr("Parse error at line %1, column %2: %3 (byte %4)")
                        .arg(line)
                        .arg(column)
                        .arg(description)
                        .arg(byteOffset),
                    requestId);
                return;
            }
            // File no longer readable — fall back to the byte-only form.
            emit parseError(tr("Parse error at byte %1: %2")
                                .arg(byteOffset)
                                .arg(description),
                            requestId);
            return;
        }
        emit parseError(tr("Parse error: %1").arg(description), requestId);
        return;
    }

    // Phase 3: Wrap in shared_ptr and emit directly (no deep-copy)
    auto sharedResult = std::make_shared<ArenaParseResult>(std::move(arenaResult));

    emit progressUpdated(100, requestId);
    emit arenaParseComplete(std::move(sharedResult), requestId);

    } catch (const std::bad_alloc&) {
        emit parseError(tr("Out of memory loading %1").arg(filePath),
                        requestId);
    } catch (const std::exception& e) {
        emit parseError(tr("Failed to load %1: %2")
                            .arg(filePath, QString::fromUtf8(e.what())),
                        requestId);
    }
}

void FileLoaderWorker::processUnion(const QStringList& filePaths,
                                    quint64 requestId) {
    if (isStale(requestId)) {
        return;
    }

    const int fileCount = static_cast<int>(filePaths.size());
    if (fileCount == 0) {
        return;
    }

    std::vector<jsontitan::core::FileEntry> entries;
    entries.reserve(static_cast<std::size_t>(fileCount));

    // Progress: file i of N owns the slice [i, i+1] * 100/N — read fills the
    // first half of the slice, parse the second half.
    int lastProgress = -1;
    auto emitSliceProgress = [this, requestId, fileCount, &lastProgress](
                                 int fileIndex, float halfOffset,
                                 float fraction) {
        const float slice = 100.0f / static_cast<float>(fileCount);
        const float base = slice * static_cast<float>(fileIndex);
        int progress =
            static_cast<int>(base + (halfOffset + fraction * 0.5f) * slice);
        if (progress != lastProgress) {
            emit progressUpdated(progress, requestId);
            lastProgress = progress;
        }
    };

    for (int i = 0; i < fileCount; ++i) {
        // Staleness check between files: a superseding request aborts the
        // whole union silently.
        if (isStale(requestId)) {
            return;
        }

        const QString& filePath = filePaths.at(i);
        const QString fileName = QFileInfo(filePath).fileName();

        try {
            std::string input;
            QString readError;
            const bool readOk = readWholeFile(
                filePath, requestId, input, readError,
                [&](float fraction) { emitSliceProgress(i, 0.0f, fraction); });
            if (!readOk) {
                if (!readError.isEmpty()) {
                    emit unionParseError(fileName, readError, requestId);
                }
                return;  // error or stale request
            }

            ParseBufferOptions parseOptions;
            parseOptions.progressCallback = [&](float coreProgress) {
                if (isStale(requestId)) {
                    return;
                }
                emitSliceProgress(i, 0.5f, coreProgress);
            };
            parseOptions.cancelCallback = [this, requestId]() {
                return isStale(requestId);
            };

            // The arena lives only inside this scope: the tree is converted
            // to a JsonNode tree immediately and the arena dropped before
            // the next file is read.
            auto arenaResult = parseBuffer(std::move(input), parseOptions);

            if (isStale(requestId)) {
                return;
            }

            if (arenaResult.error) {
                emit unionParseError(
                    fileName,
                    QString::fromStdString(arenaResult.error->description),
                    requestId);
                return;
            }

            jsontitan::core::FileEntry entry;
            entry.filename = fileName.toStdString();
            entry.root = arenaResult.root->toJsonNode();
            entries.push_back(std::move(entry));
        } catch (const std::bad_alloc&) {
            emit unionParseError(fileName, tr("Out of memory loading %1").arg(filePath),
                                 requestId);
            return;
        } catch (const std::exception& e) {
            emit unionParseError(fileName,
                                 tr("Failed to load %1: %2")
                                     .arg(filePath, QString::fromUtf8(e.what())),
                                 requestId);
            return;
        }
    }

    if (isStale(requestId)) {
        return;
    }

    auto unionRoot = jsontitan::core::unionTrees(entries);

    emit progressUpdated(100, requestId);
    emit unionParseComplete(std::move(unionRoot), filePaths, requestId);
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
    // Register metatypes for cross-thread signal/slot
    qRegisterMetaType<std::shared_ptr<jsontitan::core::ArenaParseResult>>();
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>();

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
    connect(m_worker, &FileLoaderWorker::unionParseComplete, this,
            [this](std::shared_ptr<const jsontitan::core::JsonNode> root,
                   const QStringList& filePaths, quint64 requestId) {
                if (requestId == m_activeRequest) {
                    emit unionParseComplete(std::move(root), filePaths);
                }
            }, Qt::QueuedConnection);
    connect(m_worker, &FileLoaderWorker::unionParseError, this,
            [this](const QString& fileName, const QString& errorMessage,
                   quint64 requestId) {
                if (requestId == m_activeRequest) {
                    emit unionParseError(fileName, errorMessage);
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

void FileLoader::startUnionParse(const QStringList& filePaths) {
    // beginRequest() atomically invalidates any in-progress parse (single
    // file or union) and returns the id for the new one.
    m_activeRequest = m_worker->beginRequest();

    QMetaObject::invokeMethod(m_worker, "processUnion",
                              Qt::QueuedConnection,
                              Q_ARG(QStringList, filePaths),
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
