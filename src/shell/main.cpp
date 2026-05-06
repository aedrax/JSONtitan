#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QMetaType>
#include <QTimer>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/cli_parser.h"
#include "core/json_node.h"
#include "shell/main_window.h"

int main(int argc, char* argv[]) {
    // 3.1: Convert argv (excluding argv[0]) to std::vector<std::string>
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // 3.2: Call parseCli() with the argument vector and current working directory
    auto cliResult = jsontitan::core::parseCli(args,
        std::filesystem::current_path().string());

    // 3.5: Handle parse errors — print to stderr, return 1 (before QApplication)
    if (cliResult.error.has_value()) {
        std::cerr << cliResult.error.value() << "\n";
        return 1;
    }

    // 3.3: Handle ShowHelp — print helpText() to stdout, return 0 (before QApplication)
    if (cliResult.action == jsontitan::core::CliAction::ShowHelp) {
        std::cout << jsontitan::core::helpText("jsontitan");
        return 0;
    }

    // 3.4: Handle ShowVersion — print versionText() to stdout, return 0 (before QApplication)
    if (cliResult.action == jsontitan::core::CliAction::ShowVersion) {
        std::cout << jsontitan::core::versionText("0.1.0");
        return 0;
    }

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

    // 3.6: If action is OpenFiles, defer opening until event loop starts
    if (cliResult.action == jsontitan::core::CliAction::OpenFiles) {
        auto filePaths = std::move(cliResult.filePaths);
        QTimer::singleShot(0, &window, [&window, paths = std::move(filePaths)]() {
            window.openFromCliArgs(paths);
        });
    }

    return app.exec();
}
