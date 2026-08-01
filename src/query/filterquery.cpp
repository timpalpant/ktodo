#include "filterquery.h"

#include "data/repository.h"
#include "util/duedate.h"

#include <QLocale>
#include <QRegularExpression>

#include <functional>

using namespace Todoist;

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------

struct FilterQuery::Node {
    enum Type {
        And,
        Or,
        Not,
        True,
        False,
        Today,
        Overdue,
        NoDate,
        Recurring,
        DueBefore,
        DueAfter,
        DueOn,
        NextDays,
        ProjectName,
        LabelName,
        NoLabels,
        SectionName,
        Priority,
        AssignedTo,
        AssignedBy,
        Assigned,
        NoAssignee,
        Shared,
        SubtaskOf,
        TextSearch,
    };

    Type type = True;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;

    QString text; ///< Project/label/section/user name or search text.
    QDate date;
    int number = 0;          ///< Priority level or day count.
    bool includeSub = false; ///< "##Project" also matches sub-projects.
};

namespace {

/// Resolves the date words accepted by due before:/after:/on: terms.
QDate parseDateWord(const QString &raw)
{
    const QString s = raw.trimmed().toLower();
    const QDate today = QDate::currentDate();

    if (s == QLatin1String("today")) {
        return today;
    }
    if (s == QLatin1String("tomorrow") || s == QLatin1String("tom")) {
        return today.addDays(1);
    }
    if (s == QLatin1String("yesterday")) {
        return today.addDays(-1);
    }

    static const QRegularExpression inDays(QStringLiteral("^\\+?(\\d+)\\s*(day|days)?$"));
    if (const auto m = inDays.match(s); m.hasMatch()) {
        return today.addDays(m.captured(1).toInt());
    }

    const Due guessed = DueDate::guessLocal(s);
    if (guessed.isValid()) {
        return guessed.localDate();
    }

    // Formats a user is likely to type into a filter.
    for (const QString &fmt : {QStringLiteral("yyyy-MM-dd"),
                               QStringLiteral("d MMM yyyy"),
                               QStringLiteral("d MMM"),
                               QStringLiteral("MMM d yyyy"),
                               QStringLiteral("MMM d"),
                               QStringLiteral("d/M/yyyy"),
                               QStringLiteral("M/d/yyyy"),
                               QStringLiteral("d/M")}) {
        QDate d = QLocale().toDate(raw.trimmed(), fmt);
        if (!d.isValid()) {
            d = QDate::fromString(raw.trimmed(), fmt);
        }
        if (d.isValid()) {
            if (d.year() == 1900) {
                d.setDate(today.year(), d.month(), d.day());
            }
            return d;
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

struct FilterQuery::Parser {
    QString src;
    int pos = 0;
    QString error;

    explicit Parser(const QString &s)
        : src(s)
    {}

    void skipSpace()
    {
        while (pos < src.size() && src.at(pos).isSpace()) {
            ++pos;
        }
    }

    bool atEnd()
    {
        skipSpace();
        return pos >= src.size();
    }

    QChar peek()
    {
        skipSpace();
        return pos < src.size() ? src.at(pos) : QChar();
    }

    std::unique_ptr<Node> parseExpression() { return parseOr(); }

    std::unique_ptr<Node> parseOr()
    {
        auto left = parseAnd();
        while (!atEnd()) {
            const QChar c = peek();
            if (c != QLatin1Char('|') && c != QLatin1Char(',')) {
                break;
            }
            ++pos;
            auto right = parseAnd();
            auto node = std::make_unique<Node>();
            node->type = Node::Or;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Node> parseAnd()
    {
        auto left = parseUnary();
        while (!atEnd() && peek() == QLatin1Char('&')) {
            ++pos;
            auto right = parseUnary();
            auto node = std::make_unique<Node>();
            node->type = Node::And;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Node> parseUnary()
    {
        skipSpace();
        if (!atEnd() && peek() == QLatin1Char('!')) {
            ++pos;
            auto node = std::make_unique<Node>();
            node->type = Node::Not;
            node->left = parseUnary();
            return node;
        }
        if (!atEnd() && peek() == QLatin1Char('(')) {
            ++pos;
            auto inner = parseExpression();
            skipSpace();
            if (!atEnd() && peek() == QLatin1Char(')')) {
                ++pos;
            } else {
                error = QStringLiteral("expected ')'");
            }
            return inner;
        }
        return parseTerm();
    }

    /// Reads up to the next top-level operator, keeping spaces inside names.
    QString readTermText()
    {
        skipSpace();
        const int start = pos;
        int depth = 0;
        while (pos < src.size()) {
            const QChar c = src.at(pos);
            if (c == QLatin1Char('(')) {
                ++depth;
            } else if (c == QLatin1Char(')')) {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (depth == 0 && (c == QLatin1Char('&') || c == QLatin1Char('|') || c == QLatin1Char(','))) {
                break;
            }
            ++pos;
        }
        return src.mid(start, pos - start).trimmed();
    }

    std::unique_ptr<Node> parseTerm()
    {
        const QString raw = readTermText();
        auto node = std::make_unique<Node>();

        if (raw.isEmpty()) {
            node->type = Node::False;
            return node;
        }

        const QString lower = raw.toLower();

        // Keyed terms first, since their values may contain spaces.
        const int colon = raw.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString key = raw.left(colon).trimmed().toLower();
            const QString value = raw.mid(colon + 1).trimmed();

            if (key == QLatin1String("due before") || key == QLatin1String("before")) {
                node->type = Node::DueBefore;
                node->date = parseDateWord(value);
                return node;
            }
            if (key == QLatin1String("due after") || key == QLatin1String("after")) {
                node->type = Node::DueAfter;
                node->date = parseDateWord(value);
                return node;
            }
            if (key == QLatin1String("due")) {
                node->type = Node::DueOn;
                node->date = parseDateWord(value);
                return node;
            }
            if (key == QLatin1String("assigned to")) {
                node->type = Node::AssignedTo;
                node->text = value;
                return node;
            }
            if (key == QLatin1String("assigned by")) {
                node->type = Node::AssignedBy;
                node->text = value;
                return node;
            }
            if (key == QLatin1String("search")) {
                node->type = Node::TextSearch;
                node->text = value;
                return node;
            }
            if (key == QLatin1String("subtask of")) {
                node->type = Node::SubtaskOf;
                node->text = value;
                return node;
            }
        }

        if (lower == QLatin1String("today")) {
            node->type = Node::Today;
            return node;
        }
        if (lower == QLatin1String("overdue") || lower == QLatin1String("od") || lower == QLatin1String("over due")) {
            node->type = Node::Overdue;
            return node;
        }
        if (lower == QLatin1String("tomorrow")) {
            node->type = Node::DueOn;
            node->date = QDate::currentDate().addDays(1);
            return node;
        }
        if (lower == QLatin1String("no date") || lower == QLatin1String("no due date") || lower == QLatin1String("nodate")) {
            node->type = Node::NoDate;
            return node;
        }
        if (lower == QLatin1String("recurring")) {
            node->type = Node::Recurring;
            return node;
        }
        if (lower == QLatin1String("no labels") || lower == QLatin1String("no label")) {
            node->type = Node::NoLabels;
            return node;
        }
        if (lower == QLatin1String("assigned")) {
            node->type = Node::Assigned;
            return node;
        }
        if (lower == QLatin1String("no assignee") || lower == QLatin1String("unassigned")) {
            node->type = Node::NoAssignee;
            return node;
        }
        if (lower == QLatin1String("shared")) {
            node->type = Node::Shared;
            return node;
        }

        static const QRegularExpression nextDays(QStringLiteral("^(?:next\\s+)?(\\d+)\\s+days?$"));
        if (const auto m = nextDays.match(lower); m.hasMatch()) {
            node->type = Node::NextDays;
            node->number = m.captured(1).toInt();
            return node;
        }

        static const QRegularExpression prio(QStringLiteral("^p(?:riority\\s*)?([1-4])$"));
        if (const auto m = prio.match(lower); m.hasMatch()) {
            node->type = Node::Priority;
            // Filter language uses p1 = urgent, the API stores that as 4.
            node->number = 5 - m.captured(1).toInt();
            return node;
        }

        if (raw.startsWith(QLatin1String("##"))) {
            node->type = Node::ProjectName;
            node->text = raw.mid(2).trimmed();
            node->includeSub = true;
            return node;
        }
        if (raw.startsWith(QLatin1Char('#'))) {
            node->type = Node::ProjectName;
            node->text = raw.mid(1).trimmed();
            return node;
        }
        if (raw.startsWith(QLatin1Char('@'))) {
            node->type = Node::LabelName;
            node->text = raw.mid(1).trimmed();
            return node;
        }
        if (raw.startsWith(QLatin1Char('/'))) {
            node->type = Node::SectionName;
            node->text = raw.mid(1).trimmed();
            return node;
        }

        node->type = Node::TextSearch;
        node->text = raw;
        return node;
    }
};

// ---------------------------------------------------------------------------
// FilterQuery
// ---------------------------------------------------------------------------

FilterQuery::FilterQuery(const QString &query, const Repository *repo)
    : m_repo(repo)
{
    Parser parser(query);
    m_root = parser.parseExpression();
    m_error = parser.error;
}

FilterQuery::~FilterQuery() = default;

bool FilterQuery::isValid() const
{
    return m_root != nullptr && m_error.isEmpty();
}

QString FilterQuery::errorString() const
{
    return m_error;
}

bool FilterQuery::matches(const Item &item) const
{
    if (!m_root) {
        return false;
    }

    const Repository *repo = m_repo;

    // Recursive evaluation; the tree is shallow enough that depth is a
    // non-issue for any human-written filter.
    const std::function<bool(const Node *)> eval = [&](const Node *n) -> bool {
        if (!n) {
            return false;
        }
        switch (n->type) {
        case Node::True:
            return true;
        case Node::False:
            return false;
        case Node::And:
            return eval(n->left.get()) && eval(n->right.get());
        case Node::Or:
            return eval(n->left.get()) || eval(n->right.get());
        case Node::Not:
            return !eval(n->left.get());

        case Node::Today:
            return item.due.isValid() && item.due.localDate() <= QDate::currentDate();
        case Node::Overdue:
            return DueDate::isOverdue(item.due);
        case Node::NoDate:
            return !item.due.isValid();
        case Node::Recurring:
            return item.due.isRecurring;
        case Node::DueBefore:
            return n->date.isValid() && item.due.isValid() && item.due.localDate() < n->date;
        case Node::DueAfter:
            return n->date.isValid() && item.due.isValid() && item.due.localDate() > n->date;
        case Node::DueOn:
            return n->date.isValid() && item.due.isValid() && item.due.localDate() == n->date;
        case Node::NextDays: {
            if (!item.due.isValid()) {
                return false;
            }
            const QDate d = item.due.localDate();
            return d <= QDate::currentDate().addDays(n->number);
        }

        case Node::Priority:
            return item.priority == n->number;

        case Node::LabelName:
            for (const QString &l : item.labels) {
                if (l.compare(n->text, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
            return false;
        case Node::NoLabels:
            return item.labels.isEmpty();

        case Node::ProjectName: {
            if (!repo) {
                return false;
            }
            const Todoist::Project p = repo->project(item.projectId);
            if (p.name.compare(n->text, Qt::CaseInsensitive) == 0) {
                return true;
            }
            if (n->includeSub) {
                // Walk up the parent chain so "##Work" catches descendants.
                QString parent = p.parentId;
                int guard = 0;
                while (!parent.isEmpty() && guard++ < 16) {
                    const Todoist::Project up = repo->project(parent);
                    if (up.name.compare(n->text, Qt::CaseInsensitive) == 0) {
                        return true;
                    }
                    parent = up.parentId;
                }
            }
            return false;
        }

        case Node::SectionName: {
            if (!repo || item.sectionId.isEmpty()) {
                return false;
            }
            return repo->section(item.sectionId).name.compare(n->text, Qt::CaseInsensitive) == 0;
        }

        case Node::Assigned:
            return !item.responsibleUid.isEmpty();
        case Node::NoAssignee:
            return item.responsibleUid.isEmpty();
        case Node::Shared: {
            if (!repo) {
                return false;
            }
            return repo->project(item.projectId).isShared;
        }

        case Node::AssignedTo: {
            if (!repo) {
                return false;
            }
            if (n->text.compare(QLatin1String("me"), Qt::CaseInsensitive) == 0) {
                return item.responsibleUid == repo->currentUserId();
            }
            if (n->text.compare(QLatin1String("others"), Qt::CaseInsensitive) == 0) {
                return !item.responsibleUid.isEmpty() && item.responsibleUid != repo->currentUserId();
            }
            if (item.responsibleUid.isEmpty()) {
                return false;
            }
            const Todoist::Collaborator c = repo->collaborator(item.responsibleUid);
            return c.fullName.contains(n->text, Qt::CaseInsensitive) || c.email.compare(n->text, Qt::CaseInsensitive) == 0;
        }

        case Node::AssignedBy: {
            if (!repo) {
                return false;
            }
            if (n->text.compare(QLatin1String("me"), Qt::CaseInsensitive) == 0) {
                return item.assignedByUid == repo->currentUserId();
            }
            if (item.assignedByUid.isEmpty()) {
                return false;
            }
            const Todoist::Collaborator c = repo->collaborator(item.assignedByUid);
            return c.fullName.contains(n->text, Qt::CaseInsensitive) || c.email.compare(n->text, Qt::CaseInsensitive) == 0;
        }

        case Node::SubtaskOf: {
            if (!repo || item.parentId.isEmpty()) {
                return false;
            }
            return repo->item(item.parentId).content.contains(n->text, Qt::CaseInsensitive);
        }

        case Node::TextSearch:
            return item.content.contains(n->text, Qt::CaseInsensitive) || item.description.contains(n->text, Qt::CaseInsensitive);
        }
        return false;
    };

    return eval(m_root.get());
}
