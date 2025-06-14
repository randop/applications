#pragma once

#ifndef BLOG_PAGE_H
#define BLOG_PAGE_H

/***
###############################################################################
# Includes
###############################################################################
***/

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cmark.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

/***
###############################################################################
# Constants
###############################################################################
***/
#include "include/constants.h"

namespace pt = boost::posix_time;
namespace gr = boost::gregorian;

namespace blog {
class Page {
public:
  // Constructor taking a shared_ptr to ConnectionPool
  explicit Page(std::shared_ptr<db::ConnectionPool> sharedPool);

  // Default destructor
  ~Page() = default;

  std::string titlePlaceholder(std::string content, const char *newTitle) const;
  std::string getPage(const std::string &pageId);

private:
  std::shared_ptr<db::ConnectionPool> pool;
};

Page::Page(std::shared_ptr<db::ConnectionPool> sharedPool) : pool(sharedPool) {
  if (!pool) {
    throw std::invalid_argument("Connection pool cannot be null");
  }
}

std::string Page::titlePlaceholder(std::string content,
                                   const char *newTitle) const {
  const std::string placeholder = "<title>%REPLACE_WITH_TITLE_ID%</title>";
  std::string::size_type pos = content.find(placeholder);
  std::string title = "<title>";
  title.append(newTitle);
  title.append("</title>");
  if (pos != std::string::npos) {
    content.replace(pos, placeholder.length(), title);
  }
  return content;
}

std::string Page::getPage(const std::string &pageId) {
  std::string page;
  try {
    auto conn = pool->getConnection();
    pqxx::work txn(*conn);
    auto result = txn.exec_params(queryPage, pageId);

    if (result.size() > 0) {
      int modeId = MODE_HTML;
      const char *markdown;
      for (const auto &row : result) {
        modeId = row.at("mode_id").as<int>();
        if (modeId == MODE_MARKDOWN) {
          markdown = row.at("content").c_str();
          auto html = std::unique_ptr<char, void (*)(void *)>(
              cmark_markdown_to_html(markdown, strlen(markdown),
                                     CMARK_OPT_DEFAULT),
              std::free);
          page.append(row.at("header").c_str());
          page.append(html.get());
          page.append(row.at("footer").c_str());
        } else if (modeId == MODE_HTML) {
          page.append(row.at("header").c_str());
          page.append(row.at("content").c_str());
          page.append(row.at("footer").c_str());
        }

        page = titlePlaceholder(page, row.at("title").c_str());
        break;
      }
    }
    txn.commit();
    pool->releaseConnection(conn);
  } catch (const std::exception &e) {
    spdlog::error("get page failure: {}", e.what());
    pool->releaseAll();
    spdlog::error("connections release all");
  }

  return page;
}

} // namespace blog

#endif // BLOG_PAGE_H
