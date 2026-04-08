#include "text_editor.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/component/animation.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>

TextEditor::TextEditor() {
  lines_.push_back("");
}

ftxui::Element TextEditor::OnRender() {
  using namespace ftxui;
  auto [term_width, term_height] = Terminal::Size();
  max_visible_lines_ = 10000; // large number for scrolling
  int text_width = term_width - 6; // approx for line num and separator
  adjust_scroll();
  Elements elements;
  int display_count = 0;
  for (size_t i = scroll_y_; i < lines_.size(); ++i) {
    auto wrapped = wrap_text(lines_[i], text_width);
    if (display_count + static_cast<int>(wrapped.size()) > max_visible_lines_ && i > scroll_y_) break;
    for (size_t j = 0; j < wrapped.size(); ++j) {
      if (display_count >= max_visible_lines_) break;
      std::string num_str = (j == 0) ? std::to_string(i + 1) : "";
      std::stringstream ss;
      ss << std::setw(4) << num_str << " ";
      auto line_num = ss.str();
      auto chunk = wrapped[j];
      ftxui::Element line_element;
      if (j == 0 && static_cast<int>(i) == cursor_line_) {
        int cursor_pos = std::min(cursor_col_, static_cast<int>(chunk.size()));
        auto before = chunk.substr(0, cursor_pos);
        std::string cursor_char = (cursor_pos < chunk.size()) ? std::string(1, chunk[cursor_pos]) : " ";
        auto after = (cursor_pos < chunk.size()) ? chunk.substr(cursor_pos + 1) : "";
        ftxui::Element cursor_elem = ftxui::text(cursor_char);
        if (cursor_visible_) {
          cursor_elem = ftxui::inverted(cursor_elem);
        }
        line_element = ftxui::hbox(ftxui::text(before), cursor_elem, ftxui::text(after));
      } else {
        line_element = ftxui::text(chunk);
      }
      auto row = hbox(text(line_num), separator(), line_element);
      if (static_cast<int>(i) == cursor_line_) {
        row = row | bgcolor(Color::GrayLight) | color(Color::Black);
      }
      elements.push_back(row);
      display_count++;
    }
  }
  return vbox(elements) | border;
}

bool TextEditor::OnEvent(ftxui::Event event) {
  if (event.is_character()) {
    lines_[cursor_line_].insert(cursor_col_, event.character());
    cursor_col_++;
    return true;
  }
  if (event == ftxui::Event::Backspace) {
    if (cursor_col_ > 0) {
      lines_[cursor_line_].erase(cursor_col_ - 1, 1);
      cursor_col_--;
    } else if (cursor_line_ > 0) {
      cursor_col_ = lines_[cursor_line_ - 1].size();
      lines_[cursor_line_ - 1] += lines_[cursor_line_];
      lines_.erase(lines_.begin() + cursor_line_);
      cursor_line_--;
      adjust_scroll();
    }
    return true;
  }
  if (event == ftxui::Event::Delete) {
    if (cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) {
      lines_[cursor_line_].erase(cursor_col_, 1);
    } else if (cursor_line_ < static_cast<int>(lines_.size()) - 1) {
      lines_[cursor_line_] += lines_[cursor_line_ + 1];
      lines_.erase(lines_.begin() + cursor_line_ + 1);
    }
    return true;
  }
  if (event == ftxui::Event::Return) {
    std::string rest = lines_[cursor_line_].substr(cursor_col_);
    lines_[cursor_line_] = lines_[cursor_line_].substr(0, cursor_col_);
    lines_.insert(lines_.begin() + cursor_line_ + 1, rest);
    cursor_line_++;
    cursor_col_ = 0;
    adjust_scroll();
    return true;
  }
  if (event == ftxui::Event::ArrowUp) {
    if (cursor_line_ > 0) cursor_line_--;
    cursor_col_ = std::min(cursor_col_, static_cast<int>(lines_[cursor_line_].size()));
    adjust_scroll();
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    if (cursor_line_ < static_cast<int>(lines_.size()) - 1) cursor_line_++;
    cursor_col_ = std::min(cursor_col_, static_cast<int>(lines_[cursor_line_].size()));
    adjust_scroll();
    return true;
  }
  if (event == ftxui::Event::ArrowLeft) {
    if (cursor_col_ > 0) cursor_col_--;
    return true;
  }
  if (event == ftxui::Event::ArrowRight) {
    if (cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) cursor_col_++;
    return true;
  }
  return false;
}

void TextEditor::adjust_scroll() {
  if (cursor_line_ < scroll_y_) {
    scroll_y_ = cursor_line_;
  } else if (cursor_line_ >= scroll_y_ + max_visible_lines_) {
    scroll_y_ = cursor_line_ - max_visible_lines_ + 1;
  }
}

std::vector<std::string> TextEditor::wrap_text(const std::string& text, int width) {
  std::vector<std::string> result;
  size_t pos = 0;
  while (pos < text.size()) {
    if (static_cast<int>(text.size() - pos) <= width) {
      result.push_back(text.substr(pos));
      break;
    }
    size_t end = pos + width;
    size_t last_space = text.rfind(' ', end);
    if (last_space != std::string::npos && last_space > pos) {
      result.push_back(text.substr(pos, last_space - pos));
      pos = last_space + 1;
    } else {
      result.push_back(text.substr(pos, width));
      pos += width;
    }
  }
  if (result.empty()) result.push_back("");
  return result;
}

void TextEditor::OnAnimation(ftxui::animation::Params& params) {
  animation_time_ += params.duration().count();
  if (animation_time_ > 0.3f) {
    cursor_visible_ = !cursor_visible_;
    animation_time_ = 0.0f;
    ftxui::animation::RequestAnimationFrame();
  }
}