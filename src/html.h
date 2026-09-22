#ifndef RR_HTML_H
#define RR_HTML_H

const String html_index_html = {
#include "html/index.html"
};

const String html_script_js = {
#include "html/script.js"
};

// /setup page + its companion JS for the SoftAP captive portal. Both
// wrapped in `R""""(` / `)""""` so the C preprocessor doesn't trip on
// `2em` (the `e` parses as the start of a hex float) or on embedded
// `)"` sequences inside the markup.
const String html_setup_html = {
#include "html/setup.html"
};

const String html_setup_js = {
#include "html/setup.js"
};

#endif