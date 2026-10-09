#pragma once

#include <functional>
#include <string>
#include <vector>

#include "render/theme.hpp"
#include "ui/context.hpp"

namespace gui {

Rect panel(Context& c, const Rect& bounds, std::string_view title = "",
           bool raised = false);

class Stack {
public:
    Stack(Context& c, Rect area, Axis axis = Axis::Vertical)
        : ctx_(c), area_(area), axis_(axis) {}

    Rect next(f32 extent);
    Rect next();
    void gap(f32 amount = -1.0f);
    Rect remaining() const noexcept { return area_; }
    bool exhausted() const noexcept { return area_.empty(); }

private:
    Context& ctx_;
    Rect     area_;
    Axis     axis_;
};

struct ScrollState {
    f32  offset{0.0f};
    f32  content_extent{0.0f};
    bool dragging{false};
    f32  drag_start{0.0f};
};

Rect begin_scroll(Context& c, const Rect& r, ScrollState& s);
void end_scroll(Context& c, const Rect& r, ScrollState& s, f32 content_extent);

int tabs(Context& c, const Rect& r, const std::vector<std::string>& labels,
         int current);

struct TreeNode {
    std::string id;
    std::string label;
    int         depth{0};
    bool        has_children{false};
    bool        expanded{true};
    bool        selected{false};
    u32         icon{0};
};

struct TreeState {
    std::vector<std::string> collapsed;
    std::string              selected;

    bool is_collapsed(std::string_view id) const;
    void toggle(std::string_view id);
};

struct TreeResult {
    std::string clicked;
    std::string toggled;
    std::size_t rows_drawn{0};
    f32         content_height{0.0f};
};

TreeResult tree(Context& c, const Rect& r, const std::vector<TreeNode>& nodes,
                TreeState& state);

struct TableColumn {
    std::string label;
    f32         width{100.0f};
    TextAlign   align{TextAlign::Left};
};

struct TableState {
    ScrollState scroll{};
    int         sort_column{-1};
    bool        sort_ascending{true};
    int         selected_row{-1};
};

struct TableResult {
    int  clicked_row{-1};
    int  clicked_column{-1};
    bool sort_changed{false};
};

TableResult table(Context& c, const Rect& r,
                  const std::vector<TableColumn>& columns,
                  const std::vector<std::vector<std::string>>& rows,
                  TableState& state);

bool button(Context& c, const Rect& r, std::string_view label,
            bool enabled = true, bool primary = false);

bool toggle(Context& c, const Rect& r, std::string_view label, bool& value,
            bool enabled = true);

bool checkbox(Context& c, const Rect& r, std::string_view label, bool& value);

void label(Context& c, const Rect& r, std::string_view text,
           TextAlign align = TextAlign::Left, bool dim = false);

bool text_field(Context& c, const Rect& r, std::string_view id_label,
                std::string& value, std::string_view placeholder = "");

int  text_area_lines(const Context& c, std::string_view value, f32 size, f32 width);
bool text_area(Context& c, const Rect& r, std::string_view id_label,
               std::string& value, std::string_view placeholder = "");

struct NumericResult {
    bool changed{false};
    bool committed{false};
    std::string text;
};
NumericResult numeric_field(Context& c, const Rect& r, std::string_view id_label,
                            std::string& value, std::string_view suffix = "");

bool slider(Context& c, const Rect& r, std::string_view id_label, f32& value,
            f32 lo, f32 hi);

i32 dropdown(Context& c, const Rect& r, std::string_view id_label,
             const std::vector<std::string>& items, i32& selected);

void separator(Context& c, const Rect& r);
void color_swatch(Context& c, const Rect& r, Color col, bool selected = false);

struct CommandBarState {
    std::string              input;
    std::string              prompt;
    std::vector<std::string> completions;
    std::vector<std::string> keywords;
    bool                     submitted{false};
    bool                     cancelled{false};
    i32                      chosen_keyword{-1};
    i32                      chosen_completion{-1};
    i32                      highlight{0};
};
void command_bar(Context& c, const Rect& r, CommandBarState& st);

bool status_toggle(Context& c, Rect& cursor, std::string_view label, bool& value);
void status_text(Context& c, Rect& cursor, std::string_view text, f32 width);

struct LayerRow {
    std::string name;
    bool on{true}, frozen{false}, locked{false};
    Color color{};
    bool current{false};
};
bool layer_row(Context& c, const Rect& r, LayerRow& row);

struct QuickAction {
    u32         icon{0};
    std::string label;
    int         id{0};
};
int quick_bar(Context& c, Vec2 anchor, const Rect& bounds,
              const std::vector<QuickAction>& actions);

bool tool_button(Context& c, const Rect& r, u32 icon, std::string_view tooltip,
                 bool active);

struct ThemeEditorState {
    std::vector<std::string> collapsed;
    std::string              selected;
    std::string              hex;
    std::string              new_name;
    ScrollState              scroll;

    bool is_collapsed(std::string_view group) const;
    void toggle_group(std::string_view group);
};

struct ThemeEditorResult {
    bool changed{false};
    bool committed{false};
    bool reverted{false};
    bool duplicated{false};
};

ThemeEditorResult theme_editor_panel(Context& c, const Rect& r,
                                     gtheme::ThemeEditor& editor,
                                     ThemeEditorState& st);

enum class PropertyKind : u8 { Text, Number, Bool, Color, Choice, ReadOnly };

struct PropertyRow {
    std::string  group{"General"};
    std::string  name;
    std::string  value;
    PropertyKind kind{PropertyKind::Text};
    bool         enabled{true};
    bool         varies{false};
    std::vector<std::string> choices;
    std::string  suffix;
};

struct PropertyPanelState {
    std::vector<std::string> collapsed;
    ScrollState              scroll;
    bool is_collapsed(std::string_view group) const;
    void toggle_group(std::string_view group);
};

struct PropertyEdit {
    bool        edited{false};
    std::size_t row{0};
    std::string value;
};

PropertyEdit properties_panel(Context& c, const Rect& r,
                              const std::vector<PropertyRow>& rows,
                              PropertyPanelState& st);

struct PaletteCommand {
    std::string name;
    std::string shortcut;
    std::string detail;
    bool        enabled{true};
    u32         id{0};
};

struct CommandPaletteState {
    bool        open{false};
    std::string query;
    i32         highlighted{0};
    ScrollState scroll;
};

bool palette_match(std::string_view command, std::string_view query, i32& score);

std::vector<std::size_t> palette_filter(const std::vector<PaletteCommand>& cmds,
                                        std::string_view query);

i32 command_palette(Context& c, const Rect& r,
                    const std::vector<PaletteCommand>& cmds,
                    CommandPaletteState& st);

struct LiveRuleRow {
    std::string name;
    std::string detail;
    bool        enabled{true};
    bool        applied{false};
    f32         tolerance{0.0f};
};

struct LiveRulesState {
    ScrollState scroll;
    i32         hovered{-1};
};

struct LiveRulesResult {
    i32  toggled{-1};
    i32  hovered{-1};
};

LiveRulesResult live_rules_panel(Context& c, const Rect& r,
                                 std::vector<LiveRuleRow>& rules,
                                 LiveRulesState& st);

struct RingItem {
    u32         icon{0};
    std::string label;
    bool        enabled{true};
    u32         id{0};
};

struct RingState {
    bool open{false};
    Vec2 centre{};
    i32  hovered{-1};
    i32  chosen{-1};

    bool pressing{false};
    f64  press_started{0.0};
    Vec2 press_at{};
};

bool arm_long_press(RingState& s, const InputState& in, f64 hold = 0.35,
                    f32 slop = 6.0f);

i32 ring(Context& c, RingState& s, const std::vector<RingItem>& items,
         f32 radius = 96.0f, f32 dead_zone = 24.0f);

i32 ring_sector(Vec2 from_centre, std::size_t count, f32 dead_zone);

Vec2 ring_item_centre(Vec2 centre, std::size_t index, std::size_t count,
                      f32 radius);

}
