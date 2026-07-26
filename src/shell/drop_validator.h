#pragma once

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QUrl>

struct DropValidationResult {
    bool accepted = false;
    // Plausible JSON files, in drop order. Valid only when accepted == true.
    // One file -> normal open flow; several -> union flow.
    QStringList filePaths;
    // User-facing explanation, set when accepted == false.
    QString rejectReason;
};

class DropValidator {
    Q_DECLARE_TR_FUNCTIONS(DropValidator)

public:
    /// Validates MIME data for a drop of 1..N local JSON files.
    ///
    /// A file is plausible when it either has a .json extension (accepted
    /// without opening it) or its first non-whitespace byte (of at most 64
    /// read) looks like the start of a JSON value. Directories and remote
    /// URLs are rejected. A drop is accepted when at least one payload file
    /// is plausible; implausible files are skipped.
    static DropValidationResult validate(const QMimeData* mimeData) {
        if (!mimeData || !mimeData->hasUrls()) {
            return {false, {}, tr("Drop rejected: no local files in drop")};
        }

        QStringList accepted;
        QString firstReason;
        for (const QUrl& url : mimeData->urls()) {
            QString reason;
            const QString path = validateUrl(url, reason);
            if (!path.isEmpty()) {
                accepted.append(path);
            } else if (firstReason.isEmpty()) {
                firstReason = reason;
            }
        }

        if (accepted.isEmpty()) {
            return {false, {}, firstReason};
        }
        return {true, accepted, {}};
    }

private:
    // Returns the local path when the URL is a plausible JSON file, or an
    // empty string with `reason` set.
    static QString validateUrl(const QUrl& url, QString& reason) {
        if (!url.isLocalFile()) {
            reason = tr("Drop rejected: \"%1\" is not a local file")
                         .arg(url.toDisplayString());
            return {};
        }

        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            reason = tr("Drop rejected: \"%1\" is a directory")
                         .arg(info.fileName());
            return {};
        }

        // .json extension: accept without opening the file (preserves the
        // historical fast path; the parser reports any real problem).
        if (path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
            return path;
        }

        if (looksLikeJson(path)) {
            return path;
        }

        reason = tr("Drop rejected: \"%1\" does not look like a JSON file")
                     .arg(info.fileName());
        return {};
    }

    // Cheap content sniff: the first non-whitespace byte of a JSON document
    // is one of { [ " - t f n or a digit.
    static bool looksLikeJson(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray head = file.read(64);
        for (const char c : head) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                continue;
            }
            return c == '{' || c == '[' || c == '"' || c == '-' ||
                   c == 't' || c == 'f' || c == 'n' ||
                   (c >= '0' && c <= '9');
        }
        return false;
    }
};
