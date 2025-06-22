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
#include <mongocxx/collection.hpp>
#include <mongocxx/pipeline.hpp>
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

constexpr std::string kIdField{"id"};
constexpr std::string kFromField{"from"};
constexpr std::string kLocalField{"localField"};
constexpr std::string kForeignField{"foreignField"};
constexpr std::string kModesCollection{"modes"};
constexpr std::string kModeIdField{"modeId"};
constexpr std::string kLayoutsCollection{"layouts"};
constexpr std::string kLayoutIdField{"layoutId"};
constexpr std::string kPagesCollection{"pages"};
constexpr std::string kTitleField{"title"};
constexpr std::string kContentField{"content"};
constexpr std::string kHeaderField{"header"};
constexpr std::string kFooterField{"footer"};
constexpr std::string kLayoutField{"layout"};

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
    auto collection = db[kPagesCollection];

    mongocxx::pipeline pageByIdPipeline;
    pageByIdPipeline.lookup(make_document(
        kvp(kFromField, kModesCollection), kvp(kLocalField, kModeIdField),
        kvp(kForeignField, kIdField), kvp("as", "mode")));
    pageByIdPipeline.unwind("$mode");

    pageByIdPipeline.lookup(make_document(
        kvp(kFromField, kLayoutsCollection), kvp(kLocalField, kLayoutIdField),
        kvp(kForeignField, kIdField), kvp("as", "layout")));
    pageByIdPipeline.unwind("$layout");

    pageByIdPipeline.match(make_document(kvp("id", pageId)));
    auto cursor = collection.aggregate(pageByIdPipeline);

    int modeId = MODE_HTML;
    bool found = false;
    for (const auto &doc : cursor) {
      found = true;
      if (doc[kModeIdField]) {
        modeId = doc[kModeIdField].type() == bsoncxx::type::k_int32
                     ? doc[kModeIdField].get_int32().value
                     : static_cast<int>(doc[kModeIdField].get_int64().value);
      }

      auto titleField = doc[kTitleField].get_string().value;

      auto headerValue = doc[kLayoutField][kHeaderField].get_string().value;
      page.append(headerValue.data(), headerValue.size());

      auto contentValue = doc[kContentField].get_string().value;

      if (modeId == MODE_MARKDOWN) {
        auto html = std::unique_ptr<char, void (*)(void *)>(
            cmark_markdown_to_html(contentValue.data(), contentValue.size(),
                                   CMARK_OPT_DEFAULT),
            std::free);
        page.append("<h1>");
        page.append(titleField.data(), titleField.size());
        page.append("</h1>");
        page.append(html.get());
      } else if (modeId == MODE_HTML) {
        page.append(contentValue.data(), contentValue.size());
      }

      auto footerValue = doc[kLayoutField][kFooterField].get_string().value;
      page.append(footerValue.data(), footerValue.size());

      /***
       * //DEBUG
       * std::cout << bsoncxx::to_json(doc) << std::endl;
       ***/
      break;
    }

    if (!found) {
      spdlog::warn("No result for Page::getPage => {}", pageId);
    }
  } catch (const std::exception &e) {
    spdlog::error("get page failure: {}", e.what());
  }

  return page;
}

} // namespace blog

#endif // BLOG_PAGE_H
