#include "shell/recent_files_manager.h"

#include <QSettings>
#include <QStringList>

#include "core/recent_files.h"

RecentFilesManager::RecentFilesManager(QMenu* recentMenu, QObject* parent)
    : QObject(parent), m_menu(recentMenu) {
    loadFromSettings();
    rebuildMenu();
}

void RecentFilesManager::fileOpened(const QString& filePath) {
    m_list = jsontitan::core::addRecentFile(m_list, filePath.toStdString());
    saveToSettings();
    rebuildMenu();
}

void RecentFilesManager::removeFile(const QString& filePath) {
    m_list = jsontitan::core::removeRecentFile(m_list, filePath.toStdString());
    saveToSettings();
    rebuildMenu();
}

QString RecentFilesManager::pathForAction(QAction* action) const {
    return m_actionToPath.value(action);
}

void RecentFilesManager::loadFromSettings() {
    QSettings settings;
    const QStringList paths =
        settings.value("recentFiles/paths").toStringList();

    m_list.clear();
    m_list.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& p : paths) {
        m_list.push_back(p.toStdString());
    }
}

void RecentFilesManager::saveToSettings() {
    QStringList paths;
    paths.reserve(static_cast<int>(m_list.size()));
    for (const auto& entry : m_list) {
        paths.append(QString::fromStdString(entry));
    }

    QSettings settings;
    settings.setValue("recentFiles/paths", paths);
}

void RecentFilesManager::rebuildMenu() {
    m_menu->clear();
    m_actionToPath.clear();

    if (!m_list.empty()) {
        for (const auto& entry : m_list) {
            const std::string formatted =
                jsontitan::core::formatRecentEntry(entry);
            QAction* action =
                m_menu->addAction(QString::fromStdString(formatted));

            const QString path = QString::fromStdString(entry);
            m_actionToPath.insert(action, path);

            connect(action, &QAction::triggered, this,
                    [this, path]() { emit recentFileSelected(path); });
        }
    } else {
        QAction* noRecent = m_menu->addAction("No Recent Files");
        noRecent->setEnabled(false);
    }

    m_menu->addSeparator();

    QAction* clearAction = m_menu->addAction("Clear Recent Files");
    clearAction->setEnabled(!m_list.empty());

    connect(clearAction, &QAction::triggered, this, [this]() {
        m_list = jsontitan::core::clearRecentFiles();
        saveToSettings();
        rebuildMenu();
    });
}
