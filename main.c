#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

int main() {
    int sfd, cfd, result;
    socklen_t client_addr_size;
    struct sockaddr_in addr, client_addr;

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        handle_error("socket");
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(8001);
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);
    result = bind(sfd, (struct sockaddr *) &addr, sizeof(addr));
    if (result == -1) {
        handle_error("bind");
    }

    result = listen(sfd, 10);
    if (result == -1) {
        handle_error("listen");
    }

    client_addr_size = sizeof(client_addr);
    cfd = accept(sfd, (struct sockaddr *) &client_addr, &client_addr_size);
    if (cfd == -1) {
         handle_error("accept");
    }

    // send one message
    write(cfd, "Hello\n", 6);

    char buffer[1024] = {0};
    read(cfd, buffer, 1024);
    printf("client: '%s'\n", buffer);

    result = close(sfd);
    if (result == -1) {
        handle_error("close");
    }

    return 0;
}
