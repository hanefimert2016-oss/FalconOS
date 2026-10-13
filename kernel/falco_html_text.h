/* FalconOS Falco SemanticText/2: bounded inert HTML -> native text.
 * No script execution, no style cascade, no external resource loading. */
#ifndef FALCO_HTML_TEXT_H
#define FALCO_HTML_TEXT_H
#include "falcon.h"
i32 falco_html_extract(const char *html,char *out,u32 cap);
#endif
