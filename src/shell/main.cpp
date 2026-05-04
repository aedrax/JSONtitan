#include <QApplication>
#include <QMetaType>

#include <memory>

#include "core/json_node.h"
#include "shell/main_window.h"

int main(int argc, char* argv[]) {
    // Register metatypes for cross-thread signal/slot parameters
    qRegisterMetaType<std::shared_ptr<const jsontitan::core::JsonNode>>(
        "std::shared_ptr<const jsontitan::core::JsonNode>");

    QApplication app(argc, argv);
    app.setApplicationName("JSONTitan");
    app.setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
