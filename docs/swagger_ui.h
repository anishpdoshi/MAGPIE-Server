#ifndef SWAGGER_UI_H
#define SWAGGER_UI_H

static const char *swagger_ui_html =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<title>MAGPIE API Docs</title>"
    "<link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui.css\">"
    "<style>body { margin: 0; } #swagger-ui { max-width: 1200px; margin: 0 auto; }</style>"
    "</head>"
    "<body>"
    "<div id=\"swagger-ui\"></div>"
    "<script src=\"https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js\"></script>"
    "<script>"
    "SwaggerUIBundle({"
    "  url: '/api/docs/openapi.yaml',"
    "  dom_id: '#swagger-ui',"
    "  presets: [SwaggerUIBundle.presets.apis, SwaggerUIBundle.SwaggerUIStandalonePreset],"
    "  layout: 'BaseLayout'"
    "})"
    "</script>"
    "</body>"
    "</html>";

#endif
