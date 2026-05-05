#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QMetaType>

#include <memory>

#include "core/json_node.h"
#include "shell/main_window.h"

int main(int argc, char* argv[]) {
    // Register metatypes for cross-thread signal/slot parameters
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>(
        "std::shared_ptr<const jsontitan::core::JsonNode>");

    QApplication app(argc, argv);
    app.setOrganizationName("JSONTitan");
    app.setApplicationName("JSONTitan");
    app.setApplicationVersion("0.1.0");

    QFile styleFile(":/resources/style.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    } else {
        qWarning() << "Failed to load stylesheet from resources";
    }

    MainWindow window;
    window.show();

    return app.exec();
}
