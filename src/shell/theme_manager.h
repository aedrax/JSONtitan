#pragma once

#include <QObject>

#include "shell/syntax_highlighter.h"

// Owns the application theme: loads the matching QSS resource into the
// application stylesheet, persists the choice in QSettings ("view/theme"),
// and exposes the matching syntax-highlighting palette. Constructed by
// MainWindow (before any widgets) so the initial widget polish happens with
// the persisted stylesheet already applied.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Theme { Dark, Light };
    Q_ENUM(Theme)

    // Reads the persisted theme from QSettings "view/theme" (default Dark).
    // Does NOT apply it — call apply(current()) once the QApplication exists.
    explicit ThemeManager(QObject* parent = nullptr);

    [[nodiscard]] Theme current() const { return m_current; }

    // Syntax-highlighting palette matching the current theme (Catppuccin
    // Mocha for Dark, Catppuccin Latte for Light).
    [[nodiscard]] jsontitan::shell::SyntaxTheme syntaxTheme() const;

    // Loads the theme's QSS resource, installs it as the application
    // stylesheet, persists the choice, and emits themeChanged(theme).
    void apply(Theme theme);

    // Convenience: apply the other theme.
    void toggle();

signals:
    void themeChanged(Theme theme);

private:
    Theme m_current = Theme::Dark;
};
