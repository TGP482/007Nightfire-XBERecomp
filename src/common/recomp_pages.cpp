#include "common/recomp_pages.h"

#include <iterator>

namespace app {

namespace {

using rows::Kind;
using rows::Row;

constexpr char kRestoreDefaults[] = "RESTORE DEFAULTS";
const char* const kCategories[] = {"RECOMP SETTINGS", "MOUSE AND KEYBOARD", "CONTROLLER"};  // rows::Page order
constexpr int kCategoryCount = int(std::size(kCategories));

struct PageRows {
    const Row* list = nullptr;
    size_t count = 0;
};

PageRows RowsOf(int page) {
    PageRows rows;
    rows.list = rows::Page(page, &rows.count);
    return rows;
}

std::vector<int> Categories() {
    std::vector<int> pages;
    for (int page = 0; page < kCategoryCount; page++) {
        if (RowsOf(page).count) pages.push_back(page);
    }
    return pages;
}

// the headings a row follows, by their index in the page
std::vector<size_t> Sections(int page) {
    PageRows rows = RowsOf(page);
    std::vector<size_t> headings;
    for (size_t i = 0; i + 1 < rows.count; i++) {
        bool filled = rows.list[i + 1].kind != Kind::Heading;
        if (rows.list[i].kind == Kind::Heading && filled) headings.push_back(i);
    }
    return headings;
}

// the rows after a heading, up to the next
std::vector<const Row*> SectionRows(int page, size_t heading) {
    PageRows rows = RowsOf(page);
    std::vector<const Row*> out;
    for (size_t i = heading + 1; i < rows.count; i++) {
        if (rows.list[i].kind == Kind::Heading) break;
        out.push_back(&rows.list[i]);
    }
    return out;
}

}  // namespace

void RecompPages::Open() {
    level_ = Level::Categories;
    lines_ = {};
}

void RecompPages::Top() { level_ = Level::Categories; }

std::vector<RecompPages::Line> RecompPages::Lines() const {
    std::vector<Line> lines;
    if (level_ == Level::Categories) {
        for (int page : Categories()) lines.push_back({kCategories[page], {}});
        lines.push_back({kRestoreDefaults, {}});
    } else if (level_ == Level::Sections) {
        PageRows rows = RowsOf(category_);
        for (size_t heading : Sections(category_)) lines.push_back({rows.list[heading].label, {}});
    } else {
        for (const Row* row : SectionRows(category_, section_)) lines.push_back({row->label, rows::Value(*row)});
    }
    return lines;
}

const rows::Row* RecompPages::RowAt(int line) const {
    if (level_ != Level::Rows || line < 0) return nullptr;
    std::vector<const Row*> rows = SectionRows(category_, section_);
    return line < int(rows.size()) ? rows[size_t(line)] : nullptr;
}

RecompPages::Pressed RecompPages::Press(int line) {
    if (level_ == Level::Rows) {
        const Row* row = RowAt(line);
        if (!row) return Pressed::Nothing;
        if (row->kind == Kind::Bind) {
            rows::Bind(*row);
            return Pressed::Binding;
        }
        rows::Value(*row, 1);
        return Pressed::Stepped;
    }
    if (level_ == Level::Sections) {
        std::vector<size_t> headings = Sections(category_);
        if (line < 0 || line >= int(headings.size())) return Pressed::Nothing;
        lines_[size_t(Level::Sections)] = line;
        lines_[size_t(Level::Rows)] = 0;
        section_ = headings[size_t(line)];
        level_ = Level::Rows;
        return Pressed::Opened;
    }
    std::vector<int> pages = Categories();
    if (line < 0 || line > int(pages.size())) return Pressed::Nothing;
    lines_[size_t(Level::Categories)] = line;
    if (line == int(pages.size())) {
        for (int page : pages) rows::Defaults(page);
        return Pressed::Restored;
    }
    lines_[size_t(Level::Sections)] = 0;
    category_ = pages[size_t(line)];
    level_ = Level::Sections;
    return Pressed::Opened;
}

bool RecompPages::Back() {
    if (level_ == Level::Categories) return false;
    level_ = level_ == Level::Rows ? Level::Sections : Level::Categories;
    return true;
}

bool RecompPages::Step(int line, int step) {
    const Row* row = RowAt(line);
    if (!row || row->kind != Kind::Value) return false;
    rows::Value(*row, step);
    return true;
}

}  // namespace app
