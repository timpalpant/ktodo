#pragma once

#include "data/types.h"

#include <QDate>
#include <QString>
#include <memory>

class Repository;

/**
 * Evaluator for Todoist's saved-filter language.
 *
 * Supports the operators that appear in practice: boolean composition with
 * &, | and , plus ! and parentheses, date terms (today, overdue, no date,
 * due before/after, next N days), #project, @label, /section, p1..p4,
 * assignment terms, and bare text search.
 *
 * Anything unrecognised evaluates to false rather than throwing, so a filter
 * using a syntax we do not model yet shows an empty list instead of breaking.
 */
class FilterQuery
{
public:
    FilterQuery(const QString &query, const Repository *repo);
    ~FilterQuery();

    FilterQuery(const FilterQuery &) = delete;
    FilterQuery &operator=(const FilterQuery &) = delete;

    bool matches(const Todoist::Item &item) const;

    /// False when the query could not be parsed at all.
    bool isValid() const;

    QString errorString() const;

private:
    struct Node;
    struct Parser;

    std::unique_ptr<Node> m_root;
    const Repository *m_repo = nullptr;
    QString m_error;
};
