#include "shell/file_loader.h"

void FileLoaderWorker::process(const QString& /*filePath*/) {
    // Stub — will be implemented in Task 13
}

void FileLoaderWorker::cancel() {
    // Stub — will be implemented in Task 13
}

FileLoader::FileLoader(QObject* parent)
    : QObject(parent) {
    // Stub — will be implemented in Task 13
}

FileLoader::~FileLoader() = default;

void FileLoader::startParse(const QString& /*filePath*/) {
    // Stub — will be implemented in Task 13
}

void FileLoader::cancelParse() {
    // Stub — will be implemented in Task 13
}
