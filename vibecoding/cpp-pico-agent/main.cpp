// slack_socket_mode.cpp
//
// Slack Socket Mode bot using Boost.Beast + Boost.Asio.
//
// This version uses an Asio strand for the entire WebSocket session.
// All WebSocket reads, writes, timers, and connection state transitions are
// asynchronous and serialized through the same strand.
//
// Build (Ubuntu 24.04 / Boost 1.90):
//   apt-get install libboost-system-dev libssl-dev
//   g++ -std=c++17 -O2 slack_socket_mode.cpp -o slack_socket_mode \
//       -lboost_system -lssl -lcrypto -lpthread
//
// Run:
//   export SLACK_BOT_TOKEN=xoxb-...
//   export SLACK_APP_TOKEN=xapp-...
//   export SLACK_AI_AGENT_TOKEN=...
//   export SLACK_AI_MODEL=moonshotai/kimi-k3
//   # Optional; defaults to OpenAI's Chat Completions endpoint:
//   export SLACK_AI_AGENT_ENDPOINT=https://api.openai.com/v1/chat/completions
//   ./slack_socket_mode

#define APP_VERSION "1.0.2"

#define BOOST_JSON_NO_LIB

#include <boost/json/src.hpp>

#include "https_client.hpp"
#include "websocket_client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace net = boost::asio;

namespace {

constexpr bool APP_DEBUG_MESSAGES = false;
constexpr bool APP_DEBUG_AI = false;

/**
 * Ok model performance
 *
static std::string g_AI_model = "nvidia/nemotron-3.5-lightning-30b-a3b";
*/

// meta muse ai have blazing fast performance at the moment
static std::string g_AI_model = "meta/muse-glimmer-30b";

constexpr std::size_t kMaxSectionTextBytes = 3000;

void log(const std::string &msg) {
  std::cerr << "[slack] " << msg << std::endl;
}

bool is_equals(const std::string &a, const std::string &b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](unsigned char c1, unsigned char c2) {
                      return std::tolower(c1) == std::tolower(c2);
                    });
}

bool wildcardMatch(const std::string &str, const std::string &pattern) {
  size_t s = 0, p = 0;
  size_t star = std::string::npos, sMark = 0;

  while (s < str.size()) {
    if (p < pattern.size() && pattern[p] == str[s]) {
      ++s;
      ++p;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      sMark = s;
    } else if (star != std::string::npos) {
      p = star + 1;
      s = ++sMark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

std::string removeSlackMentions(const std::string &text) {
  std::string result = std::regex_replace(text, std::regex(R"(<@[^>]+>)"), "");

  result.erase(0, result.find_first_not_of(" \t\r\n"));
  result.erase(result.find_last_not_of(" \t\r\n") + 1);

  return result;
}

std::string removeOnString(const std::string &text,
                           const std::string &expression) {
  std::string result = std::regex_replace(text, std::regex(expression), "");

  result.erase(0, result.find_first_not_of(" \t\r\n"));
  result.erase(result.find_last_not_of(" \t\r\n") + 1);

  return result;
}

bool starts_with(const std::string &str, const std::string &prefix) {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

// -----------------------------------------------------------------------------
// UTF-8
// -----------------------------------------------------------------------------

std::size_t utf8CodePointLength(std::string_view s, std::size_t pos) {
  const auto c = static_cast<unsigned char>(s[pos]);

  if (c < 0x80) {
    return 1;
  }

  if ((c & 0xE0) == 0xC0) {
    return 2;
  }

  if ((c & 0xF0) == 0xE0) {
    return 3;
  }

  if ((c & 0xF8) == 0xF0) {
    return 4;
  }

  return 0;
}

bool isValidUtf8(std::string_view s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto len = utf8CodePointLength(s, i);

    if (len == 0 || i + len > s.size()) {
      return false;
    }

    const auto c0 = static_cast<unsigned char>(s[i]);

    for (std::size_t j = 1; j < len; ++j) {
      const auto c = static_cast<unsigned char>(s[i + j]);

      if ((c & 0xC0) != 0x80) {
        return false;
      }
    }

    // Reject overlong encodings.
    if (len == 2 && c0 < 0xC2) {
      return false;
    }

    if (len == 3) {
      const auto c1 = static_cast<unsigned char>(s[i + 1]);

      if (c0 == 0xE0 && c1 < 0xA0) {
        return false;
      }

      if (c0 == 0xED && c1 >= 0xA0) {
        return false;
      }
    }

    if (len == 4) {
      const auto c1 = static_cast<unsigned char>(s[i + 1]);

      if (c0 == 0xF0 && c1 < 0x90) {
        return false;
      }

      if (c0 == 0xF4 && c1 >= 0x90) {
        return false;
      }

      if (c0 > 0xF4) {
        return false;
      }
    }

    i += len;
  }

  return true;
}

// -----------------------------------------------------------------------------
// Markdown / Slack mrkdwn parsing helpers
// -----------------------------------------------------------------------------

struct MarkdownState {
  bool inlineCode = false;
  bool fencedCode = false;
};

/**
 * Returns true if `text[pos...]` starts a Markdown code fence.
 *
 * Slack mrkdwn commonly uses:
 *
 *     ```
 *     code
 *     ```
 */
bool isCodeFence(std::string_view text, std::size_t pos) {
  return pos + 3 <= text.size() && text.compare(pos, 3, "```") == 0;
}

/**
 * Finds a safe split point while respecting Slack mrkdwn constructs.
 *
 * We strongly prefer:
 *
 *   1. paragraph boundaries
 *   2. line boundaries
 *   3. whitespace
 *
 * We avoid splitting while inside:
 *
 *   - inline code
 *   - fenced code
 *
 * This doesn't attempt to be a complete CommonMark parser because Slack
 * mrkdwn is not CommonMark.
 */
std::size_t findMarkdownSplitPoint(std::string_view text,
                                   std::size_t maxBytes) {
  if (text.size() <= maxBytes) {
    return text.size();
  }

  MarkdownState state;

  std::size_t lastSafe = 0;
  std::size_t lastWhitespace = 0;
  std::size_t lastLineBreak = 0;

  for (std::size_t i = 0; i < text.size();) {
    const auto len = utf8CodePointLength(text, i);

    if (len == 0 || i + len > text.size()) {
      break;
    }

    // ---------------------------------------------------------------------
    // Fenced code
    // ---------------------------------------------------------------------

    if (!state.inlineCode && isCodeFence(text, i)) {
      state.fencedCode = !state.fencedCode;
      i += 3;
      continue;
    }

    // ---------------------------------------------------------------------
    // Inline code
    // ---------------------------------------------------------------------

    if (!state.fencedCode && text[i] == '`') {
      state.inlineCode = !state.inlineCode;
      i += 1;
      continue;
    }

    // ---------------------------------------------------------------------
    // Track safe boundaries
    // ---------------------------------------------------------------------

    if (!state.inlineCode && !state.fencedCode) {
      if (text[i] == '\n') {
        lastLineBreak = i + 1;
        lastSafe = i + 1;
      } else if (text[i] == ' ' || text[i] == '\t') {
        lastWhitespace = i + 1;
        lastSafe = i + 1;
      }
    }

    // Don't consider a code-point that would exceed the limit.
    if (i + len > maxBytes) {
      break;
    }

    i += len;
  }

  // Best option: complete line.
  if (lastLineBreak > 0 && lastLineBreak <= maxBytes) {
    return lastLineBreak;
  }

  // Next best: whitespace.
  if (lastWhitespace > 0 && lastWhitespace <= maxBytes) {
    return lastWhitespace;
  }

  // If we're inside an inline/fenced code section at the limit,
  // find a UTF-8-safe hard boundary.
  std::size_t safe = 0;

  for (std::size_t i = 0; i < text.size();) {
    const auto len = utf8CodePointLength(text, i);

    if (len == 0 || i + len > maxBytes) {
      break;
    }

    safe = i + len;
    i += len;
  }

  if (safe == 0) {
    throw std::runtime_error("Unable to find UTF-8-safe Slack split point");
  }

  return safe;
}

// -----------------------------------------------------------------------------
// Markdown-aware splitting
// -----------------------------------------------------------------------------

std::vector<std::string> splitSlackMrkdwn(std::string_view text) {
  std::vector<std::string> result;

  if (text.empty()) {
    result.emplace_back();
    return result;
  }

  result.reserve(text.size() / kMaxSectionTextBytes + 1);

  while (!text.empty()) {
    const auto split = findMarkdownSplitPoint(text, kMaxSectionTextBytes);

    if (split == 0) {
      throw std::runtime_error("Slack mrkdwn split produced an empty chunk");
    }

    result.emplace_back(text.substr(0, split));

    text.remove_prefix(split);

    // Remove whitespace that became the first character of the
    // next chunk after splitting at a boundary.
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
      text.remove_prefix(1);
    }
  }

  return result;
}

// -----------------------------------------------------------------------------
// Block normalization
// -----------------------------------------------------------------------------

/**
 * Normalizes a Slack Block Kit payload.
 *
 * For section blocks:
 *
 *   text.type == "mrkdwn"
 *
 * oversized text is split into multiple section blocks while attempting
 * to preserve mrkdwn formatting boundaries.
 *
 * `plain_text` is also supported and split safely.
 *
 * The input object is never modified.
 */
json::object normalizeBlocksForSlack(const json::object &payload) {
  json::object output = payload;

  const auto blocksIt = payload.if_contains("blocks");

  if (!blocksIt) {
    return output;
  }

  if (!blocksIt->is_array()) {
    throw std::invalid_argument("'blocks' must be an array");
  }

  const auto &blocks = blocksIt->as_array();

  json::array normalizedBlocks;

  normalizedBlocks.reserve(blocks.size());

  for (const auto &blockValue : blocks) {
    if (!blockValue.is_object()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    const auto &block = blockValue.as_object();

    const auto typeIt = block.if_contains("type");

    if (!typeIt || !typeIt->is_string() || typeIt->as_string() != "section") {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    const auto textIt = block.if_contains("text");

    if (!textIt || !textIt->is_object()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    const auto &textObject = textIt->as_object();

    const auto textTypeIt = textObject.if_contains("type");

    const auto textValueIt = textObject.if_contains("text");

    if (!textTypeIt || !textTypeIt->is_string() || !textValueIt ||
        !textValueIt->is_string()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    const auto textType = textTypeIt->as_string();

    const auto &textValue = textValueIt->as_string();

    const std::string_view text(textValue.data(), textValue.size());

    if (!isValidUtf8(text)) {
      throw std::invalid_argument("Slack section text contains invalid UTF-8");
    }

    // Already valid.
    if (text.size() <= kMaxSectionTextBytes) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    std::vector<std::string> chunks;

    if (textType == "mrkdwn") {
      chunks = splitSlackMrkdwn(text);
    } else if (textType == "plain_text") {
      // No Markdown syntax to preserve.
      chunks = splitSlackMrkdwn(text);
    } else {
      throw std::invalid_argument("Unsupported Slack text type");
    }

    for (auto &chunk : chunks) {
      json::object newText = textObject;

      newText["text"] = std::move(chunk);

      json::object newBlock = block;

      newBlock["text"] = std::move(newText);

      normalizedBlocks.push_back(std::move(newBlock));
    }
  }

  output["blocks"] = std::move(normalizedBlocks);

  return output;
}

const json::object kEmptyObject;

std::string getStr(const json::object &obj, std::string_view key,
                   std::string def = "") {
  if (auto *p = obj.if_contains(key); p && p->is_string()) {
    return std::string(p->as_string());
  }
  return def;
}

bool getBool(const json::object &obj, std::string_view key, bool def = false) {
  if (auto *p = obj.if_contains(key); p && p->is_bool()) {
    return p->as_bool();
  }
  return def;
}

bool has(const json::object &obj, std::string_view key) {
  return obj.if_contains(key) != nullptr;
}

const json::object &getObj(const json::object &obj, std::string_view key) {
  if (auto *p = obj.if_contains(key); p && p->is_object()) {
    return p->as_object();
  }
  return kEmptyObject;
}

struct HttpsUrl {
  std::string host;
  std::string target;
};

HttpsUrl parseHttpsUrl(const std::string &url) {
  const std::string prefix = "https://";
  if (url.rfind(prefix, 0) != 0) {
    throw std::runtime_error("AI agent endpoint must use https://: " + url);
  }

  const std::string rest = url.substr(prefix.size());
  const auto slashPos = rest.find('/');

  HttpsUrl out;
  if (slashPos == std::string::npos) {
    out.host = rest;
    out.target = "/";
  } else {
    out.host = rest.substr(0, slashPos);
    out.target = rest.substr(slashPos);
  }

  if (out.host.empty()) {
    throw std::runtime_error("AI agent endpoint has an empty host.");
  }
  return out;
}

std::string extractAiAgentReply(const json::value &res) {
  if (!res.is_object()) {
    throw std::runtime_error("AI agent returned a non-object JSON response.");
  }

  const auto &obj = res.as_object();

  // OpenAI Chat Completions compatible response:
  // {"choices":[{"message":{"role":"assistant","content":"..."}}]}
  if (auto *choices = obj.if_contains("choices");
      choices && choices->is_array() && !choices->as_array().empty()) {
    const auto &choice = choices->as_array().front();
    if (choice.is_object()) {
      const auto &choiceObj = choice.as_object();
      if (auto *message = choiceObj.if_contains("message");
          message && message->is_object()) {
        const auto &messageObj = message->as_object();
        if (auto *content = messageObj.if_contains("content");
            content && content->is_string()) {
          return std::string(content->as_string());
        }
      }
    }
  }

  // Some OpenAI-compatible gateways return {"output":"..."}.
  if (auto *output = obj.if_contains("output"); output && output->is_string()) {
    return std::string(output->as_string());
  }

  throw std::runtime_error("AI agent response did not contain an assistant "
                           "message content.");
}

// This is intentionally synchronous because it is used from the HTTP worker
// pool, never from the WebSocket strand. Each call owns a short-lived
// io_context so https_client can run to completion on the worker thread.
json::value httpsPostJson(const std::string &host, const std::string &target,
                          const std::string &bearerToken,
                          const json::value &body) {
  net::io_context ioc;
  https_client client(ioc);

  json::value parsed;
  std::exception_ptr eptr;

  http_request req;
  req.method = http_method::POST;
  req.host = host;
  req.target = target;
  req.headers["Authorization"] = "Bearer " + bearerToken;
  req.headers["Content-Type"] = "application/json; charset=utf-8";
  req.body = json::serialize(body);

  client.async_request(
      std::move(req), [&](error_code ec, http_response resp) {
        if (ec) {
          eptr = std::make_exception_ptr(
              std::runtime_error("HTTPS POST failed: " + ec.message()));
          return;
        }
        try {
          parsed = json::parse(resp.body);
        } catch (...) {
          eptr = std::current_exception();
        }
      });

  ioc.run();

  if (eptr) {
    std::rethrow_exception(eptr);
  }

  return parsed;
}

// Ask Slack for a fresh Socket Mode WebSocket URL.
std::string openSocketModeUrl(const std::string &appToken) {
  json::value res = httpsPostJson("slack.com", "/api/apps.connections.open",
                                  appToken, json::value(json::object{}));

  const json::object &obj = res.as_object();
  if (!getBool(obj, "ok")) {
    throw std::runtime_error("apps.connections.open failed: " +
                             json::serialize(res));
  }

  return getStr(obj, "url");
}

class AiAgentClient : public std::enable_shared_from_this<AiAgentClient> {
public:
  AiAgentClient(net::io_context &ioc, std::string endpoint, std::string token)
      : https_(ioc), endpoint_(parseHttpsUrl(endpoint)),
        token_(std::move(token)) {}

  template <typename Handler> void query(std::string prompt, Handler handler) {
    start(std::move(prompt), std::move(handler));
  }

private:
  https_client https_;
  HttpsUrl endpoint_;
  std::string token_;

  template <typename Handler> void start(std::string prompt, Handler handler) {
    if (token_.empty()) {
      handler(std::make_exception_ptr(
                  std::runtime_error("SLACK_AI_AGENT_TOKEN is empty.")),
              std::string{});
      return;
    }

    // std::string aiModel = "moonshotai/kimi-k3";
    // std::string aiModel = "nvidia/nemotron-3.5-lightning-30b-a3b";

    // Standard OpenAI Chat Completions request. Streaming is deliberately
    // not enabled: no "stream": true field is sent.
    json::object message;
    message["role"] = "user";
    message["content"] = std::move(prompt);

    json::object systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] =
        "You are responding in Slack.\n"
        "\n"
        "Your output MUST use Slack `mrkdwn` formatting, NOT GitHub Markdown, "
        "CommonMark, or Markdown extensions that Slack does not support.\n"
        "\n"
        "CORE FORMATTING RULES\n"
        "\n"
        "Use only Slack-compatible `mrkdwn` syntax.\n"
        "\n"
        "Supported inline formatting:\n"
        "- Bold: `*bold text*`\n"
        "- Italic: `_italic text_`\n"
        "- Strikethrough: `~strikethrough text~`\n"
        "- Inline code: `` `code` ``\n"
        "- Links: `<https://example.com|link text>`\n"
        "- Bare URLs may be written as `https://example.com` when link text is "
        "unnecessary.\n"
        "\n"
        "Do NOT use Markdown headings:\n"
        "- Never use `# Heading`\n"
        "- Never use `## Heading`\n"
        "- Never use `### Heading`\n"
        "- Never use any other Markdown heading syntax.\n"
        "\n"
        "Instead, use bold text as section labels, for example:\n"
        "`*Summary*`\n"
        "\n"
        "Do not use Markdown horizontal rules such as `---`, `***`, or `___` "
        "as separators.\n"
        "\n"
        "PARAGRAPHS AND SPACING\n"
        "\n"
        "Keep responses readable in Slack.\n"
        "- Use short paragraphs.\n"
        "- Separate logical sections with blank lines.\n"
        "- Prefer concise sections over large walls of text.\n"
        "- Do not rely on indentation for visual structure.\n"
        "\n"
        "LISTS\n"
        "\n"
        "For unordered lists, use simple bullet characters such as `•` or "
        "`-`.\n"
        "\n"
        "Preferred:\n"
        "• First item\n"
        "• Second item\n"
        "• Third item\n"
        "\n"
        "Numbered lists may use ordinary numbers:\n"
        "1. First step\n"
        "2. Second step\n"
        "3. Third step\n"
        "\n"
        "Keep list formatting simple and Slack-compatible.\n"
        "\n"
        "Do not use GitHub-style task-list syntax such as:\n"
        "`- [ ] Task`\n"
        "`- [x] Completed task`\n"
        "unless the text is intentionally meant to remain literal.\n"
        "\n"
        "Do not depend on nested Markdown list indentation for complex "
        "hierarchy. When hierarchy matters, prefer short labeled sections or "
        "simple bullets.\n"
        "\n"
        "QUOTES\n"
        "\n"
        "Use Slack block quotes with `>`.\n"
        "\n"
        "Example:\n"
        "> This is a quoted statement.\n"
        "\n"
        "For multiple quoted lines, prefix each line with `>`.\n"
        "\n"
        "CODE\n"
        "\n"
        "Use single backticks for short inline code:\n"
        "`npm install`\n"
        "`systemMessage`\n"
        "`boost::json::object`\n"
        "\n"
        "Use triple backticks for multiline code blocks:\n"
        "```\n"
        "example code\n"
        "multiple lines\n"
        "```\n"
        "\n"
        "Prefer plain triple-backtick code blocks unless language annotation "
        "is explicitly useful and known to be supported by the target Slack "
        "surface.\n"
        "\n"
        "Never apply bold, italic, strikethrough, or other formatting inside a "
        "code span or code block when the intent is to display literal code.\n"
        "\n"
        "Preserve code exactly inside code formatting.\n"
        "\n"
        "LINKS\n"
        "\n"
        "Use Slack's link syntax when custom link text is needed:\n"
        "`<https://example.com|Example>`\n"
        "\n"
        "For a URL without custom text, use:\n"
        "`<https://example.com>`\n"
        "\n"
        "Do not use GitHub/Markdown links such as:\n"
        "`[Example](https://example.com)`\n"
        "\n"
        "Do not invent URLs.\n"
        "\n"
        "When displaying a URL as literal text, use code formatting if that is "
        "clearer:\n"
        "`https://example.com`\n"
        "\n"
        "Do not add tracking parameters or modify URLs unless explicitly "
        "requested.\n"
        "\n"
        "SLACK MENTIONS AND SPECIAL REFERENCES\n"
        "\n"
        "When the required Slack identifier is known, use Slack's native "
        "reference syntax rather than Markdown.\n"
        "\n"
        "User mention:\n"
        "`<@U12345678>`\n"
        "\n"
        "Channel mention:\n"
        "`<#C12345678>`\n"
        "\n"
        "Special mentions, when explicitly intended:\n"
        "`<!channel>`\n"
        "`<!here>`\n"
        "`<!everyone>`\n"
        "\n"
        "Do not fabricate Slack user IDs, channel IDs, or other Slack "
        "identifiers.\n"
        "\n"
        "If the actual Slack identifier is unknown, write the person's or "
        "channel's name as ordinary text rather than inventing a mention.\n"
        "\n"
        "Use special mentions sparingly because they can notify large groups.\n"
        "\n"
        "EMOJI\n"
        "\n"
        "Slack emoji may be represented using colon syntax:\n"
        "`:thumbsup:`\n"
        "`:rocket:`\n"
        "`:warning:`\n"
        "\n"
        "Use emoji only when they improve readability or tone.\n"
        "\n"
        "Do not use emoji as a replacement for required information.\n"
        "\n"
        "ESCAPING SPECIAL CHARACTERS\n"
        "\n"
        "Slack interprets certain characters specially in `mrkdwn`.\n"
        "\n"
        "When literal special characters are required, escape them "
        "appropriately.\n"
        "\n"
        "In particular:\n"
        "- `&` should be represented as `&amp;` when it needs to be displayed "
        "literally.\n"
        "- `<` should be represented as `&lt;` when it should not begin Slack "
        "link or mention syntax.\n"
        "- `>` should be represented as `&gt;` when it should not be "
        "interpreted as a block quote.\n"
        "\n"
        "Do not HTML-escape ordinary text unnecessarily.\n"
        "\n"
        "Be especially careful when outputting source code, JSON, XML, HTML, "
        "shell commands, or comparison expressions containing `<`, `>`, or "
        "`&`.\n"
        "\n"
        "Inside code spans and code blocks, preserve literal code characters "
        "rather than applying mrkdwn formatting to them.\n"
        "\n"
        "TABLES\n"
        "\n"
        "Slack `mrkdwn` does not provide reliable native Markdown table "
        "formatting.\n"
        "\n"
        "Do NOT generate GitHub Markdown tables such as:\n"
        "`| Column | Column |`\n"
        "`|---|---|`\n"
        "`| Value | Value |`\n"
        "\n"
        "Instead, use bullets or a code block when tabular alignment is "
        "important.\n"
        "\n"
        "For example:\n"
        "*Comparison*\n"
        "• *Option A* — Fast, simple, inexpensive\n"
        "• *Option B* — More flexible, more configuration\n"
        "\n"
        "For genuinely columnar data where exact alignment matters, a code "
        "block may be used.\n"
        "\n"
        "EMPHASIS AND NESTING\n"
        "\n"
        "Use formatting conservatively.\n"
        "\n"
        "Do not assume that arbitrary combinations of Markdown formatting will "
        "render correctly in Slack.\n"
        "\n"
        "Prefer simple, unambiguous constructs such as:\n"
        "`*bold*`\n"
        "`_italic_`\n"
        "`~strike~`\n"
        "`` `code` ``\n"
        "\n"
        "Avoid unnecessarily complicated nested formatting.\n"
        "\n"
        "Never use Markdown `**bold**`; Slack bold is `*bold*`.\n"
        "Never use Markdown `__italic__`; Slack italic is `_italic_`.\n"
        "Never use Markdown `~~strikethrough~~`; Slack strikethrough is "
        "`~strikethrough~`.\n"
        "\n"
        "TECHNICAL CONTENT\n"
        "\n"
        "When discussing APIs, code, commands, variables, filenames, "
        "configuration keys, class names, function names, database fields, or "
        "other technical identifiers, use inline code formatting where "
        "helpful.\n"
        "\n"
        "Examples:\n"
        "`Content-Type`\n"
        "`systemMessage[\"content\"]`\n"
        "`POST /api/messages`\n"
        "`npm run build`\n"
        "\n"
        "Use code blocks for multiline commands, configuration, JSON, logs, "
        "stack traces, source code, SQL, or other content where preserving "
        "whitespace matters.\n"
        "\n"
        "Do not apply Slack emphasis to technical syntax unless the "
        "surrounding prose needs emphasis.\n"
        "\n"
        "ERROR MESSAGES, LOGS, JSON, AND STRUCTURED DATA\n"
        "\n"
        "Put multiline logs, JSON, stack traces, configuration, and structured "
        "machine-readable output inside triple-backtick code blocks.\n"
        "\n"
        "Do not modify machine-readable content merely to make it visually "
        "prettier.\n"
        "\n"
        "Do not introduce Slack mentions, links, or formatting inside content "
        "that is supposed to be copied verbatim.\n"
        "\n"
        "For JSON and similar formats, preserve valid syntax.\n"
        "\n"
        "SECTION STRUCTURE\n"
        "\n"
        "For substantial responses, organize content using bold labels.\n"
        "\n"
        "Example:\n"
        "*Answer*\n"
        "\n"
        "The issue is caused by the request being sent before authentication "
        "completes.\n"
        "\n"
        "*Why*\n"
        "\n"
        "The initialization sequence creates a race condition.\n"
        "\n"
        "*Fix*\n"
        "\n"
        "1. Initialize authentication first.\n"
        "2. Wait for the initialization promise.\n"
        "3. Send the request only after authentication succeeds.\n"
        "\n"
        "*Example*\n"
        "\n"
        "```\n"
        "initializeAuth();\n"
        "await authReady();\n"
        "sendRequest();\n"
        "```\n"
        "\n"
        "PUNCTUATION AND READABILITY\n"
        "\n"
        "Use normal punctuation and natural language.\n"
        "\n"
        "Do not add excessive formatting to every sentence.\n"
        "\n"
        "Do not surround every phrase with bold or italics.\n"
        "\n"
        "Use formatting to communicate hierarchy and emphasis, not "
        "decoration.\n"
        "\n"
        "Prefer:\n"
        "`*Status:* Complete`\n"
        "\n"
        "over excessive formatting such as:\n"
        "`*STATUS:* *COMPLETE!!!*`\n"
        "\n"
        "COPY/PASTE SAFETY\n"
        "\n"
        "The response should remain useful when copied from Slack into another "
        "plain-text environment.\n"
        "\n"
        "Do not rely exclusively on visual formatting to convey essential "
        "meaning.\n"
        "\n"
        "When providing commands or code that the user may copy, place them in "
        "code formatting.\n"
        "\n"
        "When providing URLs that the user may copy, ensure the underlying URL "
        "is complete and accurate.\n"
        "\n"
        "SLACK BLOCK KIT COMPATIBILITY\n"
        "\n"
        "The output may be inserted into a Slack Block Kit `mrkdwn` text "
        "object.\n"
        "\n"
        "Therefore:\n"
        "- Use Slack `mrkdwn`, not GitHub Markdown.\n"
        "- Do not use Markdown headings.\n"
        "- Do not use GitHub Markdown tables.\n"
        "- Do not use GitHub-style links.\n"
        "- Do not use unsupported Markdown extensions.\n"
        "- Keep formatting compatible with Slack's `mrkdwn` parser.\n"
        "- Do not assume arbitrary HTML is supported.\n"
        "- Do not use HTML tags for styling.\n"
        "- Do not use CSS, HTML headings, `<strong>`, `<em>`, or similar "
        "markup.\n"
        "- Do not depend on browser-specific Markdown behavior.\n"
        "- Keep individual sections reasonably concise.\n"
        "- Preserve intentional line breaks.\n"
        "\n"
        "WHAT NOT TO DO\n"
        "\n"
        "Never output:\n"
        "`# Heading`\n"
        "`## Heading`\n"
        "`**bold**`\n"
        "`__italic__`\n"
        "`~~strikethrough~~`\n"
        "`[link text](https://example.com)`\n"
        "`| A | B |`\n"
        "`|---|---|`\n"
        "`| 1 | 2 |`\n"
        "\n"
        "Do not use HTML for styling:\n"
        "`<b>bold</b>`\n"
        "`<i>italic</i>`\n"
        "`<strong>bold</strong>`\n"
        "\n"
        "Do not invent Slack IDs:\n"
        "`<@made-up-user-id>`\n"
        "\n"
        "Do not invent channel IDs:\n"
        "`<#made-up-channel-id>`\n"
        "\n"
        "Do not use unsupported formatting merely because it works in GitHub, "
        "Discord, HTML, or another chat application.\n"
        "\n"
        "FINAL FORMATTING CHECK\n"
        "\n"
        "Before producing the response, mentally validate it against these "
        "rules:\n"
        "1. Is all formatting Slack `mrkdwn` rather than GitHub Markdown?\n"
        "2. Are bold, italic, and strikethrough written as `*...*`, `_..._`, "
        "and `~...~`?\n"
        "3. Are Markdown links replaced with Slack links such as "
        "`<https://example.com|text>`?\n"
        "4. Are there no `#` Markdown headings?\n"
        "5. Are there no GitHub Markdown tables?\n"
        "6. Are multiline code, logs, JSON, and configuration inside triple "
        "backticks?\n"
        "7. Are inline technical identifiers inside single backticks where "
        "appropriate?\n"
        "8. Are quotes written using `>`?\n"
        "9. Are Slack mentions used only when actual Slack identifiers are "
        "known?\n"
        "10. Are special characters such as literal `<`, `>`, and `&` escaped "
        "when necessary?\n"
        "11. Is the response readable in Slack without excessive formatting?\n"
        "12. Is the resulting text suitable for insertion into a Slack Block "
        "Kit `mrkdwn` field?\n"
        "\n"
        "Always prioritize correct Slack rendering, readability, and "
        "copy/paste usefulness over decorative formatting.";

    json::array messages;
    messages.push_back(std::move(systemMessage));
    messages.push_back(std::move(message));

    json::object body;
    body["model"] = g_AI_model;
    body["messages"] = std::move(messages);
    body["stream"] = false;
    body["max_tokens"] = 16384;
    body["temperature"] = 1;

    http_request req;
    req.method = http_method::POST;
    req.host = endpoint_.host;
    req.target = endpoint_.target;
    req.headers["Authorization"] = "Bearer " + token_;
    req.headers["Content-Type"] = "application/json; charset=utf-8";
    req.body = json::serialize(body);
    if (APP_DEBUG_AI) {
      log("ai payload: " + req.body);
    }

    https_.async_request(
        std::move(req),
        [self = shared_from_this(), handler = std::move(handler)](
            error_code ec, http_response resp) mutable {
          (void)self;
          if (ec) {
            handler(std::make_exception_ptr(std::runtime_error(
                        "AI agent request: " + ec.message())),
                    std::string{});
            return;
          }

          if (resp.status_code < 200 || resp.status_code >= 300) {
            handler(std::make_exception_ptr(std::runtime_error(
                        "AI agent HTTP status " +
                        std::to_string(resp.status_code) + ": " + resp.body)),
                    std::string{});
            return;
          }

          try {
            handler(nullptr,
                    extractAiAgentReply(json::parse(resp.body)));
          } catch (const std::exception &e) {
            handler(std::make_exception_ptr(std::runtime_error(
                        std::string("AI agent JSON response: ") + e.what())),
                    std::string{});
          }
        });
  }
};

class SocketModeSession
    : public std::enable_shared_from_this<SocketModeSession> {
  using strand_type = net::strand<net::io_context::executor_type>;
  using websocket_type = websocket::stream<ssl::stream<tcp::socket>>;

  struct PendingWrite {
    std::string data;
  };

public:
  SocketModeSession(net::io_context &ioc, ssl::context &sslCtx,
                    std::string botToken, std::string wsUrl,
                    net::thread_pool &httpPool, net::io_context &aiIoc,
                    std::string aiEndpoint, std::string aiToken)
      : strand_(net::make_strand(ioc)), resolver_(strand_),
        ws_(strand_, sslCtx), pingTimer_(strand_),
        botToken_(std::move(botToken)), wsUrl_(std::move(wsUrl)),
        httpPool_(httpPool), aiIoc_(aiIoc), sslCtx_(sslCtx),
        aiEndpoint_(std::move(aiEndpoint)), aiToken_(std::move(aiToken)) {
    parsed_ = parseWssUrl(wsUrl_);
  }

  void start() {
    net::dispatch(strand_,
                  [self = shared_from_this()] { self->startOnStrand(); });
  }

private:
  strand_type strand_;
  tcp::resolver resolver_;
  websocket_type ws_;
  net::steady_timer pingTimer_;

  std::string botToken_;
  std::string wsUrl_;
  WssUrl parsed_;
  net::thread_pool &httpPool_;
  net::io_context &aiIoc_;
  ssl::context &sslCtx_;
  std::string aiEndpoint_;
  std::string aiToken_;

  beast::flat_buffer readBuffer_;
  std::deque<PendingWrite> writeQueue_;
  bool writeInProgress_ = false;
  bool reconnect_ = false;
  bool stopped_ = false;
  std::chrono::steady_clock::time_point lastPong_ =
      std::chrono::steady_clock::now();

  static constexpr auto kPingInterval = std::chrono::seconds(30);
  static constexpr auto kPongTimeout = std::chrono::seconds(60);

  void startOnStrand() {
    // Beast invokes this callback while the read operation is active.
    ws_.control_callback([self = shared_from_this()](websocket::frame_type kind,
                                                     beast::string_view) {
      // control_callback is invoked by Beast as part of the
      // WebSocket operation. Marshal state changes explicitly onto
      // the same strand so they remain serialized with timers and
      // writes.
      net::post(self->strand_, [self, kind] {
        if (kind == websocket::frame_type::pong) {
          self->lastPong_ = std::chrono::steady_clock::now();
        }
      });
    });

    resolver_.async_resolve(
        parsed_.host, "443",
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec,
                                        tcp::resolver::results_type results) {
          self->onResolve(ec, std::move(results));
        }));
  }

  void onResolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("DNS resolve", ec);
      return;
    }

    net::async_connect(ws_.next_layer().next_layer(), results,
                       net::bind_executor(strand_, [self = shared_from_this()](
                                                       beast::error_code ec,
                                                       const tcp::endpoint &) {
                         self->onConnect(ec);
                       }));
  }

  void onConnect(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("TCP connect", ec);
      return;
    }

    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                                  parsed_.host.c_str())) {
      beast::error_code sslEc(static_cast<int>(::ERR_get_error()),
                              net::error::get_ssl_category());
      fail("SNI", sslEc);
      return;
    }

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        net::bind_executor(strand_,
                           [self = shared_from_this()](beast::error_code ec) {
                             self->onSslHandshake(ec);
                           }));
  }

  void onSslHandshake(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("TLS handshake", ec);
      return;
    }

    ws_.set_option(
        websocket::stream_base::decorator([](websocket::request_type &req) {
          req.set(http::field::user_agent, "boost-beast-slack-bot/1.0");
        }));

    ws_.async_handshake(parsed_.host, parsed_.target,
                        net::bind_executor(strand_, [self = shared_from_this()](
                                                        beast::error_code ec) {
                          self->onWebSocketHandshake(ec);
                        }));
  }

  void onWebSocketHandshake(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("WebSocket handshake", ec);
      return;
    }

    log("WebSocket handshake complete.");

    // One-off boot notification before entering the read loop.
    sendBootNotification();

    lastPong_ = std::chrono::steady_clock::now();
    schedulePing();
    readNext();
  }

  void sendBootNotification() {
    const char *channelEnv = std::getenv("SLACK_BOOT_CHANNEL");
    if (!channelEnv || !*channelEnv) {
      log("SLACK_BOOT_CHANNEL is not set; skipping boot notification.");
      return;
    }

    const std::string channel = channelEnv;
    const std::string bootId =
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());

    json::object message;
    message["channel"] = channel;
    message["text"] = "Boot Notification - Boot ID: " + bootId + " - Ready!!!";

    // Use Block Kit for the title/layout and mrkdwn for inline formatting.
    json::array blocks;

    json::object headerText;
    headerText["type"] = "plain_text";
    headerText["text"] = "Boot Notification";
    headerText["emoji"] = true;

    json::object header;
    header["type"] = "header";
    header["text"] = std::move(headerText);
    blocks.push_back(std::move(header));

    json::object sectionText;
    sectionText["type"] = "mrkdwn";
    sectionText["text"] = "🚀 Ready";

    json::object section;
    section["type"] = "section";
    section["text"] = std::move(sectionText);

    json::array fields;

    json::object bootField;
    bootField["type"] = "mrkdwn";
    bootField["text"] = "ℹ️ Boot ID: `" + bootId + "`";
    fields.push_back(std::move(bootField));

    json::object versionField;
    versionField["type"] = "mrkdwn";
    versionField["text"] = "ℹ️ Version: `" APP_VERSION "`";
    fields.push_back(std::move(versionField));

    json::object systemField;
    systemField["type"] = "mrkdwn";
    systemField["text"] = "🖥️ System: `Linux`";
    fields.push_back(std::move(systemField));

    section["fields"] = std::move(fields);

    blocks.push_back(std::move(section));

    json::object divider;
    divider["type"] = "divider";
    blocks.push_back(std::move(divider));

    message["blocks"] = std::move(blocks);

    const std::string botToken = botToken_;
    auto weakSelf = weak_from_this();

    // chat.postMessage is blocking, so keep it off the WebSocket strand.
    net::post(httpPool_, [weakSelf, botToken,
                          message = std::move(message)]() mutable {
      try {
        json::value res =
            httpsPostJson("slack.com", "/api/chat.postMessage", botToken,
                          json::value(std::move(message)));

        if (auto self = weakSelf.lock()) {
          net::post(self->strand_, [self, res = std::move(res)]() mutable {
            if (!res.is_object() || !getBool(res.as_object(), "ok")) {
              log("Boot notification failed: " + json::serialize(res));
            }
          });
        }
      } catch (const std::exception &e) {
        log(std::string("Boot notification error: ") + e.what());
      }
    });
  }

  void readNext() {
    if (stopped_) {
      return;
    }

    readBuffer_.clear();

    ws_.async_read(
        readBuffer_,
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec, std::size_t) {
          self->onRead(ec);
        }));
  }

  void onRead(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec == websocket::error::closed) {
      log("WebSocket closed by peer.");
      stop();
      return;
    }

    if (ec) {
      fail("WebSocket read", ec);
      return;
    }

    const std::string raw = beast::buffers_to_string(readBuffer_.data());

    json::value envelope;
    try {
      envelope = json::parse(raw);
    } catch (const std::exception &e) {
      log(std::string("Failed to parse envelope JSON: ") + e.what());
      readNext();
      return;
    }

    if (!envelope.is_object()) {
      readNext();
      return;
    }

    handleEnvelope(envelope.as_object());
    readNext();
  }

  void handleEnvelope(const json::object &envelope) {
    const std::string type = getStr(envelope, "type");

    if (type == "hello") {
      log("Socket Mode connection established (hello received).");
      return;
    }

    if (type == "disconnect") {
      log("Received disconnect envelope (reason: " +
          getStr(envelope, "reason", "unknown") + "); reconnecting...");
      reconnect_ = true;
      stop();
      return;
    }

    if (has(envelope, "envelope_id")) {
      json::object ack;
      ack["envelope_id"] = getStr(envelope, "envelope_id");
      queueWrite(json::serialize(json::value(ack)));
    }

    if (type != "events_api") {
      return;
    }

    const json::object &payload = getObj(envelope, "payload");
    const json::object &event = getObj(payload, "event");

    const std::string eventType = getStr(event, "type");
    if (eventType == "message" || eventType == "app_mention") {
      handleMessageEvent(event);
    }
  }

  void handleMessageEvent(const json::object &event) {
    if (has(event, "subtype") || has(event, "bot_id")) {
      return;
    }

    const std::string eventType = getStr(event, "type");

    if (eventType != "app_mention" && getStr(event, "channel_type") != "im") {
      return;
    }

    const std::string text = getStr(event, "text");
    const std::string channel = getStr(event, "channel");
    const std::string ts = getStr(event, "ts");
    const std::string clientMsgId = getStr(event, "client_msg_id");

    if (text.empty() || channel.empty()) {
      log("Ignoring message with empty text or channel.");
      return;
    }

    log("Received message: " + clientMsgId);

    if (APP_DEBUG_MESSAGES) {
      log(boost::json::serialize(event));
    }

    // Query the AI agent asynchronously on its own io_context. The Slack
    // WebSocket strand is never blocked waiting for the model response.
    const std::string botToken = botToken_;
    const std::string prompt = removeSlackMentions(text);
    const std::string replyChannel = channel;
    const std::string replyThread = ts;
    bool replyOnThread = false;
    auto weakSelf = weak_from_this();

    bool isBotTask = false;
    const std::string pattern = "<@U****> ping";

    json::object reply;

    if (is_equals(prompt, "ping")) {
      reply["text"] = "*pong*";
      isBotTask = true;
    } else if (is_equals(prompt, "/version")) {
      reply["text"] = std::string(APP_VERSION);
      isBotTask = true;
    } else if (starts_with(prompt, "/use ")) {
      std::string model = removeOnString(prompt, "/use ");
      std::string prevModel = std::string(g_AI_model);
      g_AI_model = model;
      reply["text"] = "switching from `" + prevModel + "` to `" + g_AI_model +
                      "` AI model. **Ready** 👍";
      isBotTask = true;
    } else if (is_equals(prompt, "/model")) {
      reply["text"] = g_AI_model;
      isBotTask = true;
    } else {
      reply["text"] = "no suitable response at the moment.";
    }

    if (isBotTask) {
      reply["channel"] = replyChannel;

      if (replyOnThread && !replyThread.empty()) {
        reply["thread_ts"] = replyThread;
      }

      // Slack chat.postMessage is still blocking, so keep it off
      // the WebSocket strand on the existing HTTP worker pool.
      net::post(httpPool_, [weakSelf, botToken,
                            reply = std::move(reply)]() mutable {
        try {
          json::value res =
              httpsPostJson("slack.com", "/api/chat.postMessage", botToken,
                            json::value(std::move(reply)));

          if (auto self2 = weakSelf.lock()) {
            net::post(self2->strand_, [self2, res = std::move(res)]() mutable {
              if (!res.is_object() || !getBool(res.as_object(), "ok")) {
                log("chat.postMessage failed: " + json::serialize(res));
              }
            });
          }
        } catch (const std::exception &e) {
          if (auto self2 = weakSelf.lock()) {
            const std::string errorText = e.what();
            net::post(self2->strand_, [self2, errorText] {
              (void)self2;
              log("chat.postMessage error: " + errorText);
            });
          }
        }
      });
    } else {
      auto aiClient = std::make_shared<AiAgentClient>(aiIoc_, sslCtx_,
                                                      aiEndpoint_, aiToken_);

      if (APP_DEBUG_AI) {
        log("doing ai...");
      }
      aiClient->query(prompt, [weakSelf, aiClient, botToken, replyChannel,
                               replyThread,
                               replyOnThread](std::exception_ptr error,
                                              std::string aiReply) mutable {
        if (auto self = weakSelf.lock()) {
          net::post(self->strand_, [self, weakSelf, botToken, replyChannel,
                                    replyThread, replyOnThread,
                                    error = std::move(error),
                                    aiReply = std::move(aiReply)]() mutable {
            if (APP_DEBUG_AI) {
              log("ai responded...");
            }
            if (error) {
              try {
                std::rethrow_exception(error);
              } catch (const std::exception &e) {
                log(std::string("AI agent query failed: ") + e.what());
                json::object message;
                message["channel"] = replyChannel;
                message["text"] = std::string("⚠️ AI error: ") + e.what();

                net::post(self->httpPool_, [weakSelf, botToken,
                                            reply =
                                                std::move(message)]() mutable {
                  try {
                    json::value res =
                        httpsPostJson("slack.com", "/api/chat.postMessage",
                                      botToken, json::value(std::move(reply)));

                    if (auto self2 = weakSelf.lock()) {
                      net::post(self2->strand_,
                                [self2, res = std::move(res)]() mutable {
                                  if (!res.is_object() ||
                                      !getBool(res.as_object(), "ok")) {
                                    log("chat.postMessage failed: " +
                                        json::serialize(res));
                                  }
                                });
                    }
                  } catch (const std::exception &e) {
                    if (auto self2 = weakSelf.lock()) {
                      const std::string errorText = e.what();
                      net::post(self2->strand_, [self2, errorText] {
                        (void)self2;
                        log("chat.postMessage error: " + errorText);
                      });
                    }
                  }
                });
              }
              return;
            }

            if (aiReply.empty()) {
              log("AI agent returned an empty reply.");
              return;
            }

            json::object message;
            message["channel"] = replyChannel;
            message["text"] = "AI response";

            json::array blocks;

            json::object headerText;
            headerText["type"] = "plain_text";
            headerText["text"] = "Here is your answer";
            headerText["emoji"] = true;

            json::object header;
            header["type"] = "header";
            header["text"] = std::move(headerText);
            blocks.push_back(std::move(header));

            json::object sectionText;
            sectionText["type"] = "mrkdwn";
            sectionText["text"] = aiReply;

            json::object section;
            section["type"] = "section";
            section["text"] = std::move(sectionText);
            blocks.push_back(std::move(section));

            json::object divider;
            divider["type"] = "divider";
            blocks.push_back(std::move(divider));

            message["blocks"] = std::move(blocks);

            if (replyOnThread && !replyThread.empty()) {
              message["thread_ts"] = replyThread;
            }

            auto normalizedMessage = normalizeBlocksForSlack(message);

            // Slack chat.postMessage is still blocking, so keep it off
            // the WebSocket strand on the existing HTTP worker pool.
            net::post(self->httpPool_, [weakSelf, botToken,
                                        reply = std::move(
                                            normalizedMessage)]() mutable {
              try {
                json::value res =
                    httpsPostJson("slack.com", "/api/chat.postMessage",
                                  botToken, json::value(std::move(reply)));

                if (auto self2 = weakSelf.lock()) {
                  net::post(self2->strand_, [self2,
                                             res = std::move(res)]() mutable {
                    if (!res.is_object() || !getBool(res.as_object(), "ok")) {
                      log("chat.postMessage failed: " + json::serialize(res));
                    }
                  });
                }
              } catch (const std::exception &e) {
                if (auto self2 = weakSelf.lock()) {
                  const std::string errorText = e.what();
                  net::post(self2->strand_, [self2, errorText] {
                    (void)self2;
                    log("chat.postMessage error: " + errorText);
                  });
                }
              }
            });
          });
        }
      });
    }
  }

  void queueWrite(std::string data) {
    // This function is only called on strand_.
    writeQueue_.push_back(PendingWrite{std::move(data)});
    if (!writeInProgress_) {
      writeNext();
    }
  }

  void writeNext() {
    // This function is only called on strand_.
    if (writeQueue_.empty()) {
      writeInProgress_ = false;
      return;
    }

    writeInProgress_ = true;

    auto &item = writeQueue_.front();

    ws_.async_write(
        net::buffer(item.data),
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec, std::size_t) {
          self->onWrite(ec);
        }));
  }

  void onWrite(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("WebSocket write", ec);
      return;
    }

    writeQueue_.pop_front();
    writeNext();
  }

  void schedulePing() {
    if (stopped_) {
      return;
    }

    pingTimer_.expires_after(std::chrono::seconds(1));
    pingTimer_.async_wait(net::bind_executor(
        strand_, [self = shared_from_this()](beast::error_code ec) {
          self->onPingTimer(ec);
        }));
  }

  void onPingTimer(beast::error_code ec) {
    if (stopped_ || ec == net::error::operation_aborted) {
      return;
    }

    if (ec) {
      fail("ping timer", ec);
      return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (now - lastPong_ > kPongTimeout) {
      log("No pong received within timeout; closing socket to force "
          "reconnect.");
      stop();
      return;
    }

    if (now - lastPingAt_ >= kPingInterval) {
      lastPingAt_ = now;

      // A Beast WebSocket permits a control operation such as ping
      // while an async_read is outstanding. The strand makes the
      // control callback and session state changes serialized.
      ws_.async_ping(
          {}, net::bind_executor(
                  strand_, [self = shared_from_this()](beast::error_code ec) {
                    if (self->stopped_) {
                      return;
                    }
                    if (ec) {
                      self->fail("WebSocket ping", ec);
                    }
                  }));
    }

    schedulePing();
  }

  void fail(const char *where, beast::error_code ec) {
    log(std::string(where) + ": " + ec.message());
    stop();
  }

  void stop() {
    if (stopped_) {
      return;
    }

    stopped_ = true;
    pingTimer_.cancel();
    resolver_.cancel();

    beast::error_code ec;
    beast::get_lowest_layer(ws_).cancel(ec);

    if (ws_.is_open()) {
      ws_.async_close(
          websocket::close_code::normal,
          net::bind_executor(
              strand_, [self = shared_from_this()](beast::error_code closeEc) {
                if (closeEc && closeEc != net::error::operation_aborted) {
                  log("WebSocket close: " + closeEc.message());
                }
              }));
    }
  }

  std::chrono::steady_clock::time_point lastPingAt_ =
      std::chrono::steady_clock::now();
};

} // namespace

int main() {
  const char *botTokenEnv = std::getenv("SLACK_BOT_TOKEN");
  const char *appTokenEnv = std::getenv("SLACK_APP_TOKEN");
  const char *aiTokenEnv = std::getenv("SLACK_AI_AGENT_TOKEN");

  if (!botTokenEnv || !appTokenEnv || !aiTokenEnv || !*aiTokenEnv) {
    std::cerr << "Set SLACK_BOT_TOKEN, SLACK_APP_TOKEN, and "
                 "SLACK_AI_AGENT_TOKEN environment variables."
              << std::endl;
    return 1;
  }

  const std::string botToken = botTokenEnv;
  const std::string appToken = appTokenEnv;
  const std::string aiToken = aiTokenEnv;

  // OpenAI-compatible Chat Completions endpoint. Override this for a
  // self-hosted/proxy endpoint while keeping the same API contract.
  const char *aiEndpointEnv = std::getenv("SLACK_AI_AGENT_ENDPOINT");
  const std::string aiEndpoint =
      (aiEndpointEnv && *aiEndpointEnv)
          ? aiEndpointEnv
          : "https://api.openai.com/v1/chat/completions";

  net::io_context ioc;
  net::io_context aiIoc;
  net::thread_pool httpPool(4);

  ssl::context ctx{ssl::context::tlsv12_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  std::cout << "ArphaXAD is running in socket mode..." << std::endl;
  std::cout << "AI agent endpoint: " << aiEndpoint << std::endl;

  // The AI agent has its own Asio io_context so its DNS/TLS/HTTP work is
  // isolated from the Slack WebSocket event loop.
  // Keep it alive even when there is temporarily no queued work; otherwise
  // io_context::run() returns immediately and later query() calls never run.
  auto aiWorkGuard = net::make_work_guard(aiIoc);
  std::thread aiIoThread([&aiIoc] { aiIoc.run(); });

  while (true) {
    try {
      // This is done outside the WebSocket strand. It is a short,
      // one-shot connection used only to obtain the Socket Mode URL.
      const std::string wsUrl = openSocketModeUrl(appToken);

      auto session = std::make_shared<SocketModeSession>(
          ioc, ctx, botToken, wsUrl, httpPool, aiIoc, aiEndpoint, aiToken);

      session->start();

      // All WebSocket work is asynchronous. Multiple threads may run
      // this io_context; the session's strand still guarantees that
      // its handlers never execute concurrently.
      ioc.run();
      ioc.restart();
    } catch (const std::exception &e) {
      log(std::string("Session error: ") + e.what());
    }

    log("Reconnecting in 1s...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  aiIoc.stop();
  if (aiIoThread.joinable()) {
    aiIoThread.join();
  }
  httpPool.join();
}
