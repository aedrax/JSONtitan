#include "shell/theme_manager.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QSettings>

namespace {

const auto kThemeSettingsKey = QStringLiteral("view/theme");

auto qssPath(ThemeManager::Theme theme) -> QString {
    return theme == ThemeManager::Theme::Light
               ? QStringLiteral(":/resources/style_light.qss")
               : QStringLiteral(":/resources/style_dark.qss");
}

auto settingsValue(ThemeManager::Theme theme) -> QString {
    return theme == ThemeManager::Theme::Light ? QStringLiteral("light")
                                               : QStringLiteral("dark");
}

}  // namespace

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
    QSettings settings;
    const QString stored =
        settings.value(kThemeSettingsKey, QStringLiteral("dark")).toString();
    m_current = (stored.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0)
                    ? Theme::Light
                    : Theme::Dark;
}

jsontitan::shell::SyntaxTheme ThemeManager::syntaxTheme() const {
    return m_current == Theme::Light ? jsontitan::shell::catppuccinLatteTheme()
                                     : jsontitan::shell::catppuccinMochaTheme();
}

void ThemeManager::apply(Theme theme) {
    m_current = theme;

    QString sheet;
    QFile styleFile(qssPath(theme));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sheet = QString::fromUtf8(styleFile.readAll());
    } else {
        // Test binaries without the resource bundle land here; an empty
        // stylesheet (native styling) is the sane fallback.
        qWarning() << "ThemeManager: failed to load" << qssPath(theme);
    }
    if (qApp != nullptr) {
        qApp->setStyleSheet(sheet);
    }

    QSettings settings;
    settings.setValue(kThemeSettingsKey, settingsValue(theme));

    emit themeChanged(theme);
}

void ThemeManager::toggle() {
    apply(m_current == Theme::Dark ? Theme::Light : Theme::Dark);
}
