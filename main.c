#include <linux/limits.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#define DS_SB_IMPLEMENTATION
#define DS_SS_IMPLEMENTATION
#define DS_IO_IMPLEMENTATION
#define DS_AP_IMPLEMENTATION
#include "ds.h"

#define MAX_LEN 1024
#define MAX_LISTEN 10

typedef enum request_kind {
    GET
} request_kind;

typedef struct request {
    request_kind kind;
    char *path;
    char *protocol;
} request_t;

int request_parse(char *buffer, unsigned int buffer_len, request_t *request) {
    ds_string_slice buffer_slice, token;
    char *verb = NULL;
    char *path = NULL;
    char *protocol = NULL;
    int result = 0;

    ds_string_slice_init(&buffer_slice, buffer, buffer_len);

    if (ds_string_slice_tokenize(&buffer_slice, ' ', &token) != 0) {
        DS_LOG_ERROR("expected HTTP verb");
        return_defer(-1);
    }
    if (ds_string_slice_to_owned(&token, &verb) != 0) {
        DS_LOG_ERROR("buy more ram!");
        return_defer(-1);
    }

    if (strcmp(verb, "GET") == 0) {
        request->kind = GET;
    } else {
        DS_LOG_ERROR("not a get request");
        return_defer(-1);
    }

    if (ds_string_slice_tokenize(&buffer_slice, ' ', &token) != 0) {
        DS_LOG_ERROR("expected HTTP path");
        return_defer(-1);
    }
    if (ds_string_slice_to_owned(&token, &path) != 0) {
        DS_LOG_ERROR("buy more ram!");
        return_defer(-1);
    }
    request->path = path;

    if (ds_string_slice_tokenize(&buffer_slice, '\n', &token) != 0) {
        DS_LOG_ERROR("expected HTTP protocol");
        return_defer(-1);
    }
    if (ds_string_slice_to_owned(&token, &protocol) != 0) {
        DS_LOG_ERROR("buy more ram!");
        return_defer(-1);
    }
    request->protocol = protocol;

defer:
    return result;
}

int read_path(char* prefix, char *path, char **content) {
    int result = 0;

    ds_string_builder path_builder;
    ds_string_builder_init(&path_builder);
    if (ds_string_builder_append(&path_builder, "%s%s", prefix, path) != 0) {
        DS_LOG_ERROR("could not create path string");
        return_defer(-1);
    }

    char *full_path = NULL;
    if (ds_string_builder_build(&path_builder, &full_path) != 0) {
        DS_LOG_ERROR("buy more ram!");
        return_defer(-1);
    }

    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        DS_LOG_ERROR("stat: %s", strerror(errno));
        return_defer(-1);
    }

    int content_len = 0;
    if (S_ISREG(path_stat.st_mode)) {
        content_len = ds_io_read_file(full_path, content);
    } else if (S_ISDIR(path_stat.st_mode)) {
        ds_string_builder directory_builder;
        ds_string_builder_init(&directory_builder);
        if (ds_string_builder_append(
            &directory_builder,
            "<!DOCTYPE HTML>\n<html lang=\"en\">\n<head>\n<meta "
            "charset=\"utf-8\">\n<title>Directory listing for "
            "%s</title>\n</head>\n"
            "<body>\n<h1>Directory listing for %s</h1>\n<hr>\n<ul>\n",
            path, path) != 0) {
            DS_LOG_ERROR("could not append to response string");
            return_defer(-1);
        }

        DIR *directory = opendir(full_path);
        struct dirent *dir;
        if (directory == NULL) {
            DS_LOG_ERROR("opendir: %s", strerror(errno));
            return_defer(-1);
        }

        while ((dir = readdir(directory)) != NULL) {
            if (ds_string_builder_append(&directory_builder, "<li><a href=\"%s%s\">%s</a></li>\n", path, dir->d_name, dir->d_name) != 0) {
                DS_LOG_ERROR("could not append to response string");
                continue;
            }
        }

        if (closedir(directory) != 0) {
            DS_LOG_ERROR("closedir: %s", strerror(errno));
            return_defer(-1);
        }

        ds_string_builder_append(&directory_builder, "</ul>\n<hr>\n</body>\n</html>\n");
        if (ds_string_builder_build(&directory_builder, content) != 0) {
            DS_LOG_ERROR("buy more ram!");
            return_defer(-1);
        }

        content_len = strlen(*content);
    } else {
        DS_LOG_ERROR("mode not supported yet");
        return_defer(-1);
    }

    result = content_len;

defer:
    return result;
}

int handle_request(int cfd, char* prefix_directory) {
    int result = 0;
    unsigned int buffer_len = 0;
    char buffer[MAX_LEN] = {0};
    request_t request = {0};
    int content_len;
    char *content = NULL;

    result = read(cfd, buffer, MAX_LEN);
    if (result == -1) {
        DS_LOG_ERROR("read: %s", strerror(errno));
        return_defer(-1);
    }
    buffer_len = result;

    if (request_parse(buffer, buffer_len, &request) == -1) {
        DS_LOG_ERROR("request parse");
        // TODO: respond with 400
        return_defer(-1);
    }

    result = read_path(prefix_directory, request.path, &content);
    if (result == -1) {
        DS_LOG_ERROR("read path");
        return_defer(-1);
    }
    content_len = result;

    ds_string_builder response_builder;
    ds_string_builder_init(&response_builder);
    ds_string_builder_append(&response_builder, "HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: %d\n\n%s", content_len, content);
    char *response = NULL;
    ds_string_builder_build(&response_builder, &response);
    int response_len = strlen(response);

    write(cfd, response, response_len);

defer:
    return result;
}

int main(int argc, char *argv[]) {
    int sfd, cfd, result, port;
    char *prefix_directory = NULL;
    struct sockaddr_in server_addr;
    ds_argparse_parser parser;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        DS_PANIC("getcwd: %s", strerror(errno));
    }
    ds_string_builder directory_builder;
    ds_string_builder_init(&directory_builder);
    ds_string_builder_append(&directory_builder, "%s", cwd);

    ds_argparse_parser_init(&parser, "http.server", "A clone of http.server in C", "1.0");
    ds_argparse_add_argument(
        &parser, (ds_argparse_options){.short_name = 'p',
                                       .long_name = "port",
                                       .description =
                                           "bind to this port (default: 8000)",
                                       .type = ARGUMENT_TYPE_POSITIONAL,
                                       .required = 0});
    ds_argparse_add_argument(
        &parser, (ds_argparse_options){.short_name = 'd',
                                       .long_name = "directory",
                                       .description =
                                           "serve this directory (default: current directory)",
                                       .type = ARGUMENT_TYPE_VALUE,
                                       .required = 0});

    ds_argparse_parse(&parser, argc, argv);
    char *port_value = ds_argparse_get_value(&parser, "port");
    port = (port_value == NULL) ? 8000 : atoi(port_value);

    char *directory_value = ds_argparse_get_value(&parser, "directory");
    if (directory_value != NULL) {
        ds_string_builder_append(&directory_builder, "/%s", directory_value);
    }
    ds_string_builder_build(&directory_builder, &prefix_directory);

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        DS_PANIC("socket: %s", strerror(errno));
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);
    if (bind(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        DS_PANIC("bind: %s", strerror(errno));
    }

    if (listen(sfd, MAX_LISTEN) == -1) {
        DS_PANIC("listen: %s", strerror(errno));
    }
    DS_LOG_INFO("listening on port %d serving from %s", port, prefix_directory);

    while (1) {
        int result;
        char buffer[MAX_LEN] = {0};
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(client_addr);

        cfd = accept(sfd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (cfd == -1) {
             DS_LOG_ERROR("accept: %s", strerror(errno));
             continue;
        }

        handle_request(cfd, prefix_directory);
    }

    result = close(sfd);
    if (result == -1) {
        DS_PANIC("close");
    }

    return 0;
}
