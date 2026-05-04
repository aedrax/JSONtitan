#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QLineEdit>
#include <QTreeView>
#include <QTextEdit>
#include <QStatusBar>
#include <QProgressBar>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
