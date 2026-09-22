#ifndef RR_HTML_H
#define RR_HTML_H

const String html_index_html = {
#include "html/index.html"
};

const String html_script_js = {
#include "html/script.js"
};

// The /setup page and its JS live as PROGMEM raw-string literals in
// src/network.cpp. We tried embedding them as `const String {...}` via
// the `#include "html/setup.html"` trick that html_index_html uses,
// but the file's CSS values (`2em`) and the JS regex/quotes hit C++
// preprocessor edge cases (the `e` in `2em` gets parsed as the start
// of a hex float). PROGMEM raw-string avoids all of that.

#endif