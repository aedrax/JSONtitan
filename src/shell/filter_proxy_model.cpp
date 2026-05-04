#include "shell/filter_proxy_model.h"
#include "core/search_engine.h"

FilterProxyModel::FilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
}

void FilterProxyModel::applyFilter(const jsontitan::core::FilterResult& /*result*/) {
    // Stub -- will be implemented in Task 12
    beginFilterChange();
    endFilterChange();
}

void FilterProxyModel::clearFilter() {
    // Stub -- will be implemented in Task 12
    beginFilterChange();
    endFilterChange();
}

bool FilterProxyModel::filterAcceptsRow(int /*sourceRow*/,
                                        const QModelIndex& /*sourceParent*/) const {
    return true;
}
