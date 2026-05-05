#pragma once

#include <QMimeData>
#include <QString>
#include <QUrl>

struct DropValidationResult {
    bool accepted = false;
    QString filePath;  // Valid only when accepted == true
};

class DropValidator {
public:
    /// Validates MIME data for a single .json file drop.
    /// Returns accepted=true with the file path if valid,
    /// or accepted=false if the drop should be rejected.
    static DropValidationResult validate(const QMimeData* mimeData) {
        if (!mimeData || !mimeData->hasUrls()) {
            return {false, {}};
        }

        const QList<QUrl> urls = mimeData->urls();
        if (urls.size() != 1) {
            return {false, {}};
        }

        const QUrl& url = urls.first();
        if (!url.isLocalFile()) {
            return {false, {}};
        }

        const QString path = url.toLocalFile();
        if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
            return {false, {}};
        }

        return {true, path};
    }
};
