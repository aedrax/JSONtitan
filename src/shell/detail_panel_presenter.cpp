#include "shell/detail_panel_presenter.h"

#include <QItemSelectionModel>
#include <QStringList>

#include <string>
#include <variant>

#include "core/pretty_printer.h"
#include "core/token_emitter.h"
#include "shell/clipboard_utils.h"
#include "shell/model_paths.h"

DetailPanelPresenter::DetailPanelPresenter(QTextEdit* detailPanel,
                                           QLabel* breadcrumbLabel,
                                           QTreeView* treeView,
                                           FilterProxyModel* filterProxy,
                                           TreeModel* treeModel,
                                           DocumentSession* session,
                                           QObject* parent)
    : QObject(parent),
      m_detailPanel(detailPanel),
      m_breadcrumbLabel(breadcrumbLabel),
      m_treeView(treeView),
      m_filterProxy(filterProxy),
      m_treeModel(treeModel),
      m_session(session) {
    // Connect tree selection changes
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &DetailPanelPresenter::onSelectionChanged);
    if (m_breadcrumbLabel) {
        connect(m_breadcrumbLabel, &QLabel::linkActivated,
                this, &DetailPanelPresenter::onBreadcrumbLinkActivated);
    }
}

std::optional<jsontitan::core::NodeView> DetailPanelPresenter::selectedNodeView() const {
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return std::nullopt;
    }

    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return std::nullopt;
    }

    // The model hands out raw pointers into whichever backing is live; a
    // NodeView over them is valid as long as that backing is kept alive,
    // which DocumentSession guarantees.
    if (const auto* jn = m_treeModel->jsonNodeForIndex(sourceIndex)) {
        return jsontitan::core::NodeView(*jn);
    }
    if (const auto* an = m_treeModel->arenaNodeForIndex(sourceIndex)) {
        return jsontitan::core::NodeView(*an);
    }
    return std::nullopt;
}

jsontitan::core::NodePath DetailPanelPresenter::currentSelectionPath() const {
    QModelIndex proxyIndex = m_treeView->currentIndex();
    if (!proxyIndex.isValid()) {
        return {};
    }
    QModelIndex sourceIndex = m_filterProxy->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) {
        return {};
    }
    return jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex);
}

QString DetailPanelPresenter::breadcrumbHtml(
    const jsontitan::core::NodePath& path) {
    const auto segmentText =
        [](const jsontitan::core::PathSegment& segment) -> QString {
        if (const auto* key = std::get_if<std::string>(&segment)) {
            return QString::fromStdString(*key).toHtmlEscaped();
        }
        return QStringLiteral("[%1]").arg(std::get<std::size_t>(segment));
    };
    const auto link = [](std::size_t prefixLen, const QString& text) {
        return QStringLiteral("<a href=\"%1\">%2</a>")
            .arg(prefixLen)
            .arg(text);
    };

    QStringList parts;
    parts << link(0, QStringLiteral("$"));

    // Manual middle eliding: beyond this many segments show the first one, a
    // plain (non-link) ellipsis, then the last four. Hrefs stay absolute
    // prefix lengths, so navigation is unaffected by the eliding.
    constexpr std::size_t kMaxSegments = 6;
    if (path.size() <= kMaxSegments) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            parts << link(i + 1, segmentText(path[i]));
        }
    } else {
        parts << link(1, segmentText(path[0]));
        parts << QStringLiteral("…");
        for (std::size_t i = path.size() - 4; i < path.size(); ++i) {
            parts << link(i + 1, segmentText(path[i]));
        }
    }
    return parts.join(QStringLiteral(" › "));
}

void DetailPanelPresenter::onBreadcrumbLinkActivated(const QString& link) {
    bool ok = false;
    const int prefixLen = link.toInt(&ok);
    if (!ok || prefixLen < 0) {
        return;
    }

    if (prefixLen == 0) {
        // Root: not a selectable row — clear the selection instead.
        m_treeView->setCurrentIndex(QModelIndex());
        return;
    }

    auto path = currentSelectionPath();
    if (static_cast<std::size_t>(prefixLen) > path.size()) {
        return;
    }
    jsontitan::core::NodePath prefix(path.begin(), path.begin() + prefixLen);
    QModelIndex sourceIndex =
        jsontitan::shell::indexForPath(*m_treeModel, prefix);
    if (!sourceIndex.isValid()) {
        return;
    }
    QModelIndex proxyIndex = m_filterProxy->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) {
        return;
    }
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex);
}

void DetailPanelPresenter::onSelectionChanged() {
    // Breadcrumb: mirrors the selection; cleared when nothing resolves.
    if (m_breadcrumbLabel) {
        QModelIndex proxyIndex = m_treeView->currentIndex();
        QModelIndex sourceIndex =
            proxyIndex.isValid() ? m_filterProxy->mapToSource(proxyIndex)
                                 : QModelIndex();
        if (sourceIndex.isValid()) {
            auto path =
                jsontitan::shell::nodePathForIndex(*m_treeModel, sourceIndex);
            m_breadcrumbLabel->setText(breadcrumbHtml(path));
            m_breadcrumbLabel->setToolTip(jsontitan::shell::jsonPathText(path));
        } else {
            m_breadcrumbLabel->clear();
            m_breadcrumbLabel->setToolTip(QString());
        }
    }

    // Emit tokens directly over whichever backing the selection resolves to —
    // no toJsonNode() deep copy for arena-backed nodes.
    auto node = selectedNodeView();
    if (!node && m_session->currentRoot()) {
        // Legacy behavior: an unresolved selection over a JsonNode-backed
        // tree falls back to showing the root.
        node = jsontitan::core::NodeView(*m_session->currentRoot());
    }
    if (!node) {
        m_detailPanel->clear();
        return;
    }

    jsontitan::core::PrettyPrintOptions opts;
    opts.maxOutputSize = kDetailPanelByteLimit;

    auto tokenResult = jsontitan::core::emitTokens(*node, opts);
    jsontitan::shell::renderHighlighted(m_detailPanel, tokenResult, m_syntaxTheme);
}
