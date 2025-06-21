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
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>
#include <cmark.h>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

/***
###############################################################################
# Constants
###############################################################################
***/
#include "include/constants.h"

namespace blog {

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;

namespace pt = boost::posix_time;
namespace gr = boost::gregorian;

class Page {
public:
  // Constructor taking a shared_ptr to mongodb uri
  explicit Page(std::shared_ptr<mongocxx::pool> dbPool);

  // Default destructor
  ~Page() = default;

  std::string titlePlaceholder(std::string content, const char *newTitle) const;
  std::string getPage(const std::string &pageId);

private:
  std::shared_ptr<mongocxx::pool> pool;
};

Page::Page(std::shared_ptr<mongocxx::pool> dbPool) : pool(dbPool) {
  if (!pool) {
    throw std::invalid_argument("Invalid or null mongodb pool");
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
    auto client = pool->acquire();
    auto db = client["localhost"];
    auto collection = db["pages"];

    mongocxx::pipeline pages;
    pages.lookup(
        make_document(kvp("from", "modes"), kvp("localField", "mode_id"),
                      kvp("foreignField", "id"), kvp("as", "mode_info")));
    pages.unwind("$mode_info");
    pages.match(make_document(kvp("id", pageId)));

    auto cursor = collection.aggregate(pages);

    int modeId = MODE_HTML;
    const char *markdown;
    for (const auto &doc : cursor) {
      if (doc["mode_id"]) {
        modeId = doc["mode_id"].type() == bsoncxx::type::k_int32
                     ? doc["mode_id"].get_int32().value
                     : static_cast<int>(doc["mode_id"].get_int64().value);
      }
      spdlog::info("mode: {}", modeId);
      std::cout << bsoncxx::to_json(doc) << std::endl;
      break;
    }

    spdlog::warn("No result for Page::getPage => {}", pageId);

    /*
    if (result.size() > 0) {

      for (const auto &row : result) {
        modeId = row.at("mode_id").as<int>();
        page.append(row.at("header").c_str());
        if (modeId == MODE_MARKDOWN) {
          markdown = row.at("content").c_str();
          auto html = std::unique_ptr<char, void (*)(void *)>(
              cmark_markdown_to_html(markdown, strlen(markdown),
                                     CMARK_OPT_DEFAULT),
              std::free);

          page.append(html.get());
        } else if (modeId == MODE_HTML) {
          page.append(row.at("content").c_str());
        }
        page.append(row.at("footer").c_str());
        page = titlePlaceholder(page, row.at("title").c_str());
        break;
      }
    }
    */

  } catch (const std::exception &e) {
    spdlog::error("get page failure: {}", e.what());
  }

  return page;
}

} // namespace blog

#endif // BLOG_PAGE_H
