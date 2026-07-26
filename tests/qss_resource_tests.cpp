#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "shell/syntax_highlighter.h"
#include "shell/theme_manager.h"

// ---------------------------------------------------------------------------
// QSS resource + ThemeManager tests
// Task 6.1 (original): QSS resource loading.
// Phase 5c commit 2: both theme stylesheets ship and load; ThemeManager
// persists the choice and notifies listeners.
// ---------------------------------------------------------------------------

namespace {

QByteArray readResource(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return file.readAll();
}

}  // namespace

class QssResourceTest : public QObject {
    Q_OBJECT

private slots:
    void testBothStylesheetResourcesOpenNonEmpty() {
        const QByteArray dark = readResource(":/resources/style_dark.qss");
        QVERIFY2(!dark.isEmpty(),
                 "Failed to load :/resources/style_dark.qss (or it is empty)");

        const QByteArray light = readResource(":/resources/style_light.qss");
        QVERIFY2(!light.isEmpty(),
                 "Failed to load :/resources/style_light.qss (or it is empty)");
    }

    void testBothThemesKeepFocusVisibilityStyles() {
        // The accessibility work added visible focus rings; both themes must
        // keep them (a light theme silently dropping them would regress
        // keyboard navigation).
        for (const auto* path :
             {":/resources/style_dark.qss", ":/resources/style_light.qss"}) {
            const QString qss = QString::fromUtf8(readResource(path));
            QVERIFY2(qss.contains("QTreeView:focus"), path);
            QVERIFY2(qss.contains("QLineEdit#searchBar:focus"), path);
            QVERIFY2(qss.contains("QTextEdit#detailPanel:focus"), path);
        }
    }

    void testBothThemesCoverTheSameSelectors() {
        // Every selector-ish rule of the dark theme must exist in the light
        // theme too (same structure, different palette).
        for (const auto* selector :
             {"QMainWindow", "QMenuBar::item:selected", "QMenu::separator",
              "QToolButton:checked:hover", "QTreeView::item:selected",
              "QTreeView::branch:open:has-children",
              "QProgressBar::chunk", "QScrollBar::handle:vertical:hover",
              "QScrollBar::handle:horizontal:hover", "QStatusBar::item",
              "QSplitter::handle:hover", "QLabel#matchCountLabel",
              "QLabel#searchErrorLabel", "QLabel#noResultsLabel",
              "QLabel#welcomeLabel", "QLabel#dropOverlay", "QToolTip"}) {
            for (const auto* path : {":/resources/style_dark.qss",
                                     ":/resources/style_light.qss"}) {
                const QString qss = QString::fromUtf8(readResource(path));
                QVERIFY2(qss.contains(selector),
                         qPrintable(QStringLiteral("%1 missing in %2")
                                        .arg(selector, path)));
            }
        }
    }

    void testLightThemeHasNoDarkPaletteLeftovers() {
        const QString light =
            QString::fromUtf8(readResource(":/resources/style_light.qss"));
        // Mocha base/surface/text colors must not leak into the Latte sheet.
        QVERIFY(!light.contains("#1e1e2e", Qt::CaseInsensitive));
        QVERIFY(!light.contains("#313244", Qt::CaseInsensitive));
        QVERIFY(!light.contains("#cdd6f4", Qt::CaseInsensitive));
        // And it actually uses the Latte base + text.
        QVERIFY(light.contains("#eff1f5", Qt::CaseInsensitive));
        QVERIFY(light.contains("#4c4f69", Qt::CaseInsensitive));
    }
};

class ThemeManagerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("JSONTitanTest");
        QCoreApplication::setApplicationName("ThemeManagerTests");
        QSettings().clear();
    }

    void cleanup() {
        QSettings().clear();
        qApp->setStyleSheet(QString());
    }

    void testDefaultsToDarkWithoutSettings() {
        ThemeManager manager;
        QCOMPARE(manager.current(), ThemeManager::Theme::Dark);
        // Dark palette = Catppuccin Mocha.
        QCOMPARE(manager.syntaxTheme().keyColor,
                 jsontitan::shell::catppuccinMochaTheme().keyColor);
    }

    void testApplySetsStylesheetPersistsAndNotifies() {
        ThemeManager manager;
        QSignalSpy spy(&manager, &ThemeManager::themeChanged);

        manager.apply(ThemeManager::Theme::Light);

        QCOMPARE(manager.current(), ThemeManager::Theme::Light);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<ThemeManager::Theme>(),
                 ThemeManager::Theme::Light);
        // The application stylesheet now holds the Latte sheet.
        QVERIFY(qApp->styleSheet().contains("#eff1f5", Qt::CaseInsensitive));
        // The choice is persisted...
        QCOMPARE(QSettings().value("view/theme").toString(),
                 QStringLiteral("light"));
        // ...and a fresh ThemeManager reads it back (settings round-trip).
        ThemeManager restored;
        QCOMPARE(restored.current(), ThemeManager::Theme::Light);
        QCOMPARE(restored.syntaxTheme().keyColor,
                 jsontitan::shell::catppuccinLatteTheme().keyColor);
    }

    void testToggleFlipsThemeAndPersists() {
        ThemeManager manager;
        QSignalSpy spy(&manager, &ThemeManager::themeChanged);

        manager.toggle();
        QCOMPARE(manager.current(), ThemeManager::Theme::Light);
        manager.toggle();
        QCOMPARE(manager.current(), ThemeManager::Theme::Dark);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(QSettings().value("view/theme").toString(),
                 QStringLiteral("dark"));
        QVERIFY(qApp->styleSheet().contains("#1e1e2e", Qt::CaseInsensitive));
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int status = 0;

    QssResourceTest resourceTest;
    status |= QTest::qExec(&resourceTest, argc, argv);

    ThemeManagerTest themeManagerTest;
    status |= QTest::qExec(&themeManagerTest, argc, argv);

    return status;
}

#include "qss_resource_tests.moc"
