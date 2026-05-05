#include <QApplication>
#include <QFile>
#include <QtTest/QtTest>

// ---------------------------------------------------------------------------
// Task 6.1: Unit test for QSS resource loading
// Validates: Requirements 2.1, 1.1
// ---------------------------------------------------------------------------

class QssResourceTest : public QObject {
    Q_OBJECT

private slots:
    void testStylesheetResourceOpensSuccessfully() {
        QFile styleFile(":/resources/style.qss");
        QVERIFY2(styleFile.open(QIODevice::ReadOnly | QIODevice::Text),
                 "Failed to open :/resources/style.qss from Qt resource system");

        QByteArray content = styleFile.readAll();
        QVERIFY2(content.size() > 0,
                 "style.qss resource file is empty");

        styleFile.close();
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int status = 0;

    QssResourceTest resourceTest;
    status |= QTest::qExec(&resourceTest, argc, argv);

    return status;
}

#include "qss_resource_tests.moc"
