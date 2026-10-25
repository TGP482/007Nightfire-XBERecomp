#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "common/menu.h"

// The RECOMP pages a pause menu shows: the categories with rows, then a category's sections, then a section's rows,
// with the F10 panel's rows, values and wording. Each image draws them and feeds them its keys.
namespace app {

class RecompPages {
public:
    enum class Level { Categories, Sections, Rows };
    enum class Pressed { Nothing, Opened, Stepped, Binding, Restored };
    struct Line {
        std::string label;
        std::string value;
    };

    void Open();  // the pause opened: the categories, no line remembered
    void Top();   // the categories, at the line they were left on
    Level level() const { return level_; }
    int line() const { return lines_[size_t(level_)]; }  // the line to select as the level shows
    std::vector<Line> Lines() const;
    const rows::Row* RowAt(int line) const;  // null off the rows level
    // A on a line: opens a category or section, restores the defaults, waits for a key or steps a value on
    Pressed Press(int line);
    bool Back();  // a level up; false on the categories
    bool Step(int line, int step);  // false when the line has no value

private:
    Level level_ = Level::Categories;
    int category_ = 0;    // rows::Page
    size_t section_ = 0;  // its heading's index in the page
    std::array<int, 3> lines_{};  // by level
};

}  // namespace app
