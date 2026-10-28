#include "shell/node_input_dialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "core/edit_engine.h"

NodeInputDialog::NodeInputDialog(const Options& options, QWidget* parent)
    : QDialog(parent), m_takenKeys(options.takenKeys) {
    setWindowTitle(options.title);
    resize(520, 360);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    if (options.showKey) {
        m_keyEdit = new QLineEdit(options.key, this);
        m_keyEdit->setObjectName("nodeKeyEdit");
        m_keyEdit->selectAll();
        form->addRow(tr("Key:"), m_keyEdit);
    }

    m_valueEdit = new QPlainTextEdit(options.value, this);
    m_valueEdit->setObjectName("nodeValueEdit");
    m_valueEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_valueEdit->setTabChangesFocus(true);
    m_valueEdit->setPlaceholderText(
        tr("Text, a number, true/false/null, or JSON such as {} or [1, 2]"));
    form->addRow(tr("Value:"), m_valueEdit);

    auto* hint = new QLabel(
        tr("Anything that parses as JSON is inserted as JSON; other text "
           "becomes a string. Wrap text in quotes to force a string."),
        this);
    hint->setWordWrap(true);
    hint->setEnabled(false);  // muted, theme-aware
    layout->addWidget(hint);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("inputErrorLabel");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &NodeInputDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NodeInputDialog::reject);
    layout->addWidget(buttons);

    if (m_keyEdit) {
        m_keyEdit->setFocus();
    } else {
        m_valueEdit->setFocus();
        m_valueEdit->selectAll();
    }
}

QString NodeInputDialog::key() const {
    return m_keyEdit ? m_keyEdit->text() : QString();
}

void NodeInputDialog::accept() {
    const auto showError = [this](const QString& message) {
        m_errorLabel->setText(message);
        m_errorLabel->show();
    };

    if (m_keyEdit && m_takenKeys.contains(m_keyEdit->text())) {
        showError(tr("Key \"%1\" already exists in this object.")
                      .arg(m_keyEdit->text()));
        m_keyEdit->setFocus();
        return;
    }

    auto parsed =
        jsontitan::core::parseValueInput(m_valueEdit->toPlainText().toStdString());
    if (!parsed) {
        showError(QString::fromStdString(parsed.error().description));
        m_valueEdit->setFocus();
        return;
    }
    m_value = std::move(*parsed);
    QDialog::accept();
}
