#pragma once

#ifndef BLOG_CONSTANTS_H
#define BLOG_CONSTANTS_H

const int MAX_DB_CONNECTION = 10;
const int MODE_MARKDOWN = 1;
const int MODE_HTML = 1;
const int DEFAULT_PORT = 8181;
const int NONE_POST_ID = 0;

const char *queryPost = "SELECT id, created_at, updated_at, title, content, "
                        "mode_id FROM posts WHERE id = $1 LIMIT 1";

#endif // BLOG_CONSTANTS_H
