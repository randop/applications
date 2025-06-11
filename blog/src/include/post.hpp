#pragma once

#ifndef BLOG_POST_H
#define BLOG_POST_H

/***
###############################################################################
# Includes
###############################################################################
***/

#include <cmark.h>
#include <spdlog/spdlog.h>

#include <iostream>

/***
###############################################################################
# Constants
###############################################################################
***/
#include "include/constants.h"

const char *queryPost = "SELECT id, created_at, updated_at, title, content, "
                        "mode_id FROM posts WHERE id = $1 LIMIT 1";

namespace blog {
class Post {
public:
  // Constructor taking a shared_ptr to ConnectionPool
  explicit Post(std::shared_ptr<db::ConnectionPool> sharedPool);

  // Default destructor
  ~Post() = default;

  std::string getPost(int postId);

private:
  std::shared_ptr<db::ConnectionPool> pool;
};

Post::Post(std::shared_ptr<db::ConnectionPool> sharedPool)
    : pool(std::move(sharedPool)) {
  if (!pool) {
    throw std::invalid_argument("Connection pool cannot be null");
  }
}

std::string Post::getPost(int postId) {
  std::string post;
  try {
    auto conn = pool->getConnection();
    pqxx::work txn(*conn);
    auto result = txn.exec(queryPost, postId);

    if (result.size() > 0) {
      int modeId = MODE_MARKDOWN;
      const char *markdown;
      for (const auto &row : result) {
        post.append("<h1>");
        post.append(row.at("title").c_str());
        post.append("</h1>");
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
      }
    }
    txn.commit();
    pool->releaseConnection(conn);
  } catch (const std::exception &e) {
    spdlog::error("get post failure: {}", e.what());
  }

  return post;
}

} // namespace blog

#endif // BLOG_POST_H
