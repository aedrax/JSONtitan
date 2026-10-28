#pragma once

#include <QDialog>
#include <QString>

#include <functional>

#include "core/replace_engine.h"

class QCheckBox;
class QLabel;
class QLineEdit;

// Edit > Find and Replace...: collects ReplaceOptions and hands them to
// `replaceAll` on "Replace All", showing the summary it returns. The dialog
// stays open so several replacements can be run in a row (each one is its
// own undo step).
class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    using ReplaceAllFn = std::function<QString(const jsontitan::core::ReplaceOptions&)>;

    FindReplaceDialog(const QString& initialPattern, ReplaceAllFn replaceAll,
                      QWidget* parent = nullptr);

    jsontitan::core::ReplaceOptions options() const;

private:
    void runReplaceAll();

    ReplaceAllFn m_replaceAll;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QCheckBox* m_regex = nullptr;
    QCheckBox* m_inValues = nullptr;
    QCheckBox* m_inKeys = nullptr;
    QLabel* m_resultLabel = nullptr;
};
