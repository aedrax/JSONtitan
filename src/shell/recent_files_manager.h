#pragma once

#include <QAction>
#include <QHash>
#include <QMenu>
#include <QObject>
#include <QString>

#include "core/recent_files.h"

class RecentFilesManager : public QObject {
    Q_OBJECT
public:
    explicit RecentFilesManager(QMenu* recentMenu, QObject* parent = nullptr);

    // Call after a file is successfully parsed to record it.
    void fileOpened(const QString& filePath);

    // Remove a file from the recent list (e.g., when file no longer exists).
    void removeFile(const QString& filePath);

    // Returns the file path for a given menu action (used by MainWindow).
    QString pathForAction(QAction* action) const;

signals:
    // Emitted when the user clicks a recent file entry.
    void recentFileSelected(const QString& filePath);

private:
    void loadFromSettings();
    void saveToSettings();
    void rebuildMenu();

    QMenu* m_menu = nullptr;
    jsontitan::core::RecentFilesList m_list;
    QHash<QAction*, QString> m_actionToPath;
};
