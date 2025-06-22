#pragma once

#ifndef BLOG_POST_H
#define BLOG_POST_H

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

#include <iostream>
#include <string>

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

class Post {
public:
  // Constructor taking a shared_ptr to ConnectionPool
  explicit Post(std::shared_ptr<mongocxx::pool> dbPool);

  // Default destructor
  ~Post() = default;

  std::string timestamp(const std::string &timeStamp);
  std::string titlePlaceholder(std::string content, const char *newTitle) const;
  std::string getPost(int postId);

private:
  std::shared_ptr<mongocxx::pool> pool;
};

Post::Post(std::shared_ptr<mongocxx::pool> dbPool) : pool(dbPool) {
  if (!pool) {
    throw std::invalid_argument("Invalid or null mongodb pool");
  }
}

std::string Post::timestamp(const std::string &inputTimestamp) {
  try {
    // Split timestamp into date and time parts.
    std::string datePart;
    std::string timePart;
    std::stringstream inputStream(inputTimestamp);
    std::getline(inputStream, datePart, ' ');
    std::getline(inputStream, timePart);

    // Parse date (YYYY-MM-DD).
    gr::date date = gr::from_simple_string(datePart);

    // Parse time (HH:MM:SS.ffffff).
    pt::time_duration time;
    std::istringstream timeStream(timePart);
    timeStream >> time;

    // Combine into ptime.
    pt::ptime utcTime(date, time);

    // Validate parsed time.
    if (utcTime.is_not_a_date_time()) {
      // Invalid timestamp: parsing failed
      return inputTimestamp;
    }

    // Format output.
    pt::time_facet *outputFacet = new pt::time_facet();
    outputFacet->format("%a, %B %d, %Y at %I:%M %p UTC");
    std::stringstream outputStream;
    outputStream.imbue(std::locale(outputStream.getloc(), outputFacet));
    outputStream << utcTime;

    return outputStream.str();
  } catch (const std::exception &e) {
    // error
    return inputTimestamp;
  }
  return inputTimestamp;
}

std::string Post::titlePlaceholder(std::string content,
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

std::string Post::getPost(int postId) {
  std::string post;
  try {
    /*
    auto conn = pool->getConnection();
    pqxx::work txn(*conn);
    auto result = txn.exec(queryPost, postId);

    if (result.size() > 0) {
      int modeId = MODE_MARKDOWN;
      const char *markdown;
      for (const auto &row : result) {
        modeId = row.at("mode_id").as<int>();
        post.append(row.at("header").c_str());
        post.append("<h1>");
        post.append(row.at("title").c_str());
        post.append("</h1>");
        post.append("<h4>");
        post.append(timestamp(row.at("updated_at").as<std::string>()));
        post.append("<h4>");
        if (modeId == MODE_MARKDOWN) {
          markdown = row.at("content").c_str();
          auto html = std::unique_ptr<char, void (*)(void *)>(
              cmark_markdown_to_html(markdown, strlen(markdown),
                                     CMARK_OPT_DEFAULT),
              std::free);
          post.append(html.get());
        } else if (modeId == MODE_HTML) {
          post.append(row.at("content").c_str());
        }
        post.append(row.at("footer").c_str());
        post = titlePlaceholder(post, row.at("title").c_str());
        break;
      }
    }
    */
  } catch (const std::exception &e) {
    spdlog::error("Post::getPost exception: {}", e.what());
  }

  return post;
}

} // namespace blog

#endif // BLOG_POST_H
