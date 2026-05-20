idf_component_register(SRCS "http_server.c" "api_handlers.c"
                       INCLUDE_DIRS "."
                       REQUIRES config core wifi mqtt)
