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
#include "ds.h"

#define MAX_LEN 1024

int main() {
    int sfd, cfd, result;
    socklen_t client_addr_size;
    struct sockaddr_in addr, client_addr;

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        DS_PANIC("socket");
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);
    result = bind(sfd, (struct sockaddr *) &addr, sizeof(addr));
    if (result == -1) {
        DS_PANIC("bind");
    }

    result = listen(sfd, 10);
    if (result == -1) {
        DS_PANIC("listen");
    }

    while (1) {
        client_addr_size = sizeof(client_addr);
        cfd = accept(sfd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (cfd == -1) {
             DS_PANIC("accept");
        }

        char buffer[MAX_LEN] = {0};
        int result = read(cfd, buffer, MAX_LEN);
        if (result == -1) {
            DS_LOG_ERROR("read");
            continue;
        }
        unsigned int buffer_len = result;

        ds_string_slice request, token;
        ds_string_slice_init(&request, buffer, buffer_len);

        ds_string_slice_tokenize(&request, ' ', &token);
        char *verb = NULL;
        ds_string_slice_to_owned(&token, &verb);
        if (strcmp(verb, "GET") != 0) {
            DS_LOG_ERROR("not a get request");
            // TODO: respond with 400
            continue;
        }

        ds_string_slice_tokenize(&request, ' ', &token);
        char *path = NULL;
        ds_string_slice_to_owned(&token, &path);

        // TODO: if file does not exists return 404
        struct stat path_stat;
        if (stat(path + 1, &path_stat) != 0) {
            DS_LOG_ERROR("stat");
            continue;
        }

        char *content = NULL;
        int content_len = 0;
        if (S_ISREG(path_stat.st_mode)) {
            content_len = ds_io_read_file(path + 1, &content);
        } else if (S_ISDIR(path_stat.st_mode)) {
            ds_string_builder directory_builder;
            ds_string_builder_init(&directory_builder);
            ds_string_builder_append(
                &directory_builder,
                "<!DOCTYPE HTML>\n<html lang=\"en\">\n<head>\n<meta "
                "charset=\"utf-8\">\n<title>Directory listing for "
                "%s</title>\n</head>\n"
                "<body>\n<h1>Directory listing for %s</h1>\n<hr>\n<ul>\n",
                path, path);

            DIR *directory = opendir(path + 1);
            struct dirent *dir;
            if (directory) {
                while ((dir = readdir(directory)) != NULL) {
                    ds_string_builder_append(&directory_builder, "<li><a href=\"%s/%s\">%s</a></li>\n", path + 1, dir->d_name, dir->d_name);
                }
                closedir(directory);
            }

            ds_string_builder_append(&directory_builder, "</ul>\n<hr>\n</body>\n</html>\n");
            ds_string_builder_build(&directory_builder, &content);
            content_len = strlen(content);

        } else {
            DS_LOG_ERROR("mode not supported yet");
            continue;
        }

        ds_string_builder response_builder;
        ds_string_builder_init(&response_builder);
        ds_string_builder_append(&response_builder, "HTTP/1.1 200 OK\nContent-Type: text/html\nContent-Length: %d\n\n%s", content_len, content);
        char *response = NULL;
        ds_string_builder_build(&response_builder, &response);
        int response_len = strlen(response);
        write(cfd, response, response_len);
    }

    result = close(sfd);
    if (result == -1) {
        DS_PANIC("close");
    }

    return 0;
}
