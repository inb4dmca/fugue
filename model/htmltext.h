// Text helpers for the chat and browser-output rules of the world model.
// Decode-chat-HTML = URL query-unescape or the raw text; strip-chat-markup = `<[^>]+>` removal then HTML unescape.
// The named-entity table is a subset of HTML5 (see htmltext.cpp).
#pragma once
#include <string>
#include <string_view>

namespace fugue::model {

// URL query-unescape; on error (a '%' not followed by two hex digits) returns `raw` unchanged.
std::string decodeChatHTML(std::string_view raw);
// `<[^>]+>` removed, then HTML unescape.
std::string stripChatMarkup(std::string_view markup);
// HTML escape: & ' < > " -> &amp; &#39; &lt; &gt; &#34;
std::string htmlEscape(std::string_view s);

}  // namespace fugue::model
