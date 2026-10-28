#include "shell/find_replace_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog(const QString& initialPattern,
                                     ReplaceAllFn replaceAll, QWidget* parent)
    : QDialog(parent), m_replaceAll(std::move(replaceAll)) {
    setWindowTitle(tr("Find and Replace"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    m_findEdit = new QLineEdit(initialPattern, this);
    m_findEdit->setObjectName("findEdit");
    form->addRow(tr("Find:"), m_findEdit);
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setObjectName("replaceEdit");
    m_replaceEdit->setPlaceholderText(tr("Regex mode: $1, $2... insert groups"));
    form->addRow(tr("Replace with:"), m_replaceEdit);

    auto* options = new QHBoxLayout();
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_regex = new QCheckBox(tr("Regular expression"), this);
    options->addWidget(m_caseSensitive);
    options->addWidget(m_regex);
    options->addStretch(1);
    layout->addLayout(options);

    auto* scope = new QHBoxLayout();
    scope->addWidget(new QLabel(tr("Replace in:"), this));
    m_inValues = new QCheckBox(tr("String values"), this);
    m_inValues->setChecked(true);
    m_inKeys = new QCheckBox(tr("Keys"), this);
    scope->addWidget(m_inValues);
    scope->addWidget(m_inKeys);
    scope->addStretch(1);
    layout->addLayout(scope);

    m_resultLabel = new QLabel(this);
    m_resultLabel->setObjectName("replaceResultLabel");
    m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* replaceAllButton =
        buttons->addButton(tr("Replace &All"), QDialogButtonBox::ActionRole);
    replaceAllButton->setDefault(true);
    connect(replaceAllButton, &QPushButton::clicked, this,
            &FindReplaceDialog::runReplaceAll);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Nothing to do without a pattern or a place to look.
    const auto updateEnabled = [this, replaceAllButton]() {
        replaceAllButton->setEnabled(!m_findEdit->text().isEmpty() &&
                                     (m_inValues->isChecked() || m_inKeys->isChecked()));
    };
    connect(m_findEdit, &QLineEdit::textChanged, this, updateEnabled);
    connect(m_inValues, &QCheckBox::toggled, this, updateEnabled);
    connect(m_inKeys, &QCheckBox::toggled, this, updateEnabled);
    updateEnabled();

    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

jsontitan::core::ReplaceOptions FindReplaceDialog::options() const {
    return {.pattern = m_findEdit->text().toStdString(),
            .replacement = m_replaceEdit->text().toStdString(),
            .regex = m_regex->isChecked(),
            .caseSensitive = m_caseSensitive->isChecked(),
            .inValues = m_inValues->isChecked(),
            .inKeys = m_inKeys->isChecked()};
}

void FindReplaceDialog::runReplaceAll() {
    m_resultLabel->setText(m_replaceAll(options()));
}
