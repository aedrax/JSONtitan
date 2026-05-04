#pragma once

#include <QObject>
#include <QMetaType>
#include <QThread>
#include <QString>

#include <atomic>
#include <memory>

namespace jsontitan::core {
struct JsonNode;
}

Q_DECLARE_METATYPE(std::shared_ptr<const jsontitan::core::JsonNode>)

class FileLoaderWorker : public QObject {
    Q_OBJECT
public:
    explicit FileLoaderWorker(QObject* parent = nullptr);

public slots:
    void process(const QString& filePath);
    void cancel();

signals:
    void progressUpdated(int percentage);
    void parseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root);
    void parseError(QString errorMessage);

private:
    std::atomic<bool> m_cancelled{false};
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
    void parseError(QString errorMessage);

private:
    QThread* m_workerThread = nullptr;
    FileLoaderWorker* m_worker = nullptr;
};
