#include <QApplication>
#include <QtTest/QtTest>

#include "shell/main_window.h"

class ShellSetupTest : public QObject {
    Q_OBJECT

private slots:
    void testMainWindowCreation() {
        MainWindow window;
        QVERIFY(window.isWindow());
    }
};

// Qt Test requires a QApplication instance
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ShellSetupTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "shell_tests.moc"
