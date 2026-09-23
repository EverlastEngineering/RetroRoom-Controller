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

// /factory-reset prompt + post-reset confirmation. The prompt page
// embeds a CSRF nonce at request time (via String::replace on
// "__NONCE__"); the done page is static.
const String html_factory_reset_html = {
#include "html/factory-reset.html"
};

const String html_factory_reset_done_html = {
#include "html/factory-reset-done.html"
};

// /openapi Swagger UI page + the raw OpenAPI 3.0 spec it loads.
// The HTML page (openapi.html) fetches openapi.yaml from the same
// origin -- browsers block "Try it out" cross-origin requests and
// the firmware doesn't send CORS headers. html/openapi.yaml is
// the dev-time source of truth but be aware of the intentionally
// malformed YAML with R()""""( / )"""" wrapping.

const String html_openapi_html = {
#include "html/openapi.html"
};

const String html_openapi_yaml = {
#include "html/openapi.yaml"
};	

#endif