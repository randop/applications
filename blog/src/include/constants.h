#pragma once

#ifndef BLOG_CONSTANTS_H
#define BLOG_CONSTANTS_H

#define CONTENT_TYPE_HTML "text/html"
#define CONTENT_TYPE_JSON "application/json"

const char *ANY_IPV4_HOST = "0.0.0.0";
const int MAX_DB_CONNECTION = 10;
const int MODE_MARKDOWN = 1;
const int MODE_HTML = 2;
const int DEFAULT_PORT = 10000;
const int NONE_POST_ID = 0;

const char *queryPost = "SELECT "
                        "p.id, "
                        "p.created_at, "
                        "p.updated_at, "
                        "p.title, "
                        "p.content, "
                        "p.mode_id, "
                        "l.header, "
                        "l.footer "
                        "FROM posts p "
                        "INNER JOIN layouts l ON p.layout_id = l.id "
                        "WHERE p.id = $1 "
                        "LIMIT 1";

const char *queryPage = "SELECT "
                        "p.id, "
                        "p.created_at, "
                        "p.updated_at, "
                        "p.title, "
                        "p.content, "
                        "p.mode_id, "
                        "l.header, "
                        "l.footer "
                        "FROM pages p "
                        "INNER JOIN layouts l ON p.layout_id = l.id "
                        "WHERE p.id = $1 "
                        "LIMIT 1";
#endif // BLOG_CONSTANTS_H
