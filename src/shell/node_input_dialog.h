#pragma once

#include <QDialog>
#include <QSet>
#include <QString>

#include <memory>

#include "core/json_node.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Modal dialog for "Add Child…", "Insert After…" and "Edit as JSON…": an
// optional key field (object members only) and a multi-line value field.
// The value is read with core::parseValueInput — JSON when it parses, a
// plain scalar otherwise — and validated on OK, so callers get a ready
// node and never see invalid input. Keys listed in `takenKeys` are rejected
// as duplicates on OK as well.
class NodeInputDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        QString title;
        bool showKey = false;
        QString key;
        QSet<QString> takenKeys;
        QString value;
    };

    explicit NodeInputDialog(const Options& options, QWidget* parent = nullptr);

    QString key() const;
    // The parsed value; valid once the dialog was accepted.
    std::shared_ptr<const jsontitan::core::JsonNode> value() const { return m_value; }

    void accept() override;

private:
    QLineEdit* m_keyEdit = nullptr;
    QPlainTextEdit* m_valueEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QSet<QString> m_takenKeys;
    std::shared_ptr<const jsontitan::core::JsonNode> m_value;
};
