#pragma once

#include <QObject>
#include <QThread>
#include <QString>

#include <memory>

namespace jsontitan::core {
struct JsonNode;
}

class FileLoaderWorker : public QObject {
    Q_OBJECT
public slots:
    void process(const QString& filePath);
    void cancel();

signals:
    void progressUpdated(int percentage);
    void parseComplete(std::shared_ptr<const jsontitan::core::JsonNode> root);
    void parseError(QString errorMessage);
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
