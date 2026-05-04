#pragma once

#include <QSortFilterProxyModel>

namespace jsontitan::core {
struct FilterResult;
}

class FilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject* parent = nullptr);

    void applyFilter(const jsontitan::core::FilterResult& result);
    void clearFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
};
