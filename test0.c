#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <libgen.h>

char *getFileName(char *data) {
    char *filename = calloc(256, sizeof(char)), *ptr = data, *qtr = NULL, needle[10] = "filename=";
    ptr = strstr(data, needle);
    if (ptr == NULL)
        return NULL;
    ptr = ptr + strlen(needle) + 1; // (filename=")..."
    qtr = filename;
    while (*ptr != '"') {
        *qtr++ = *ptr++;
    }
    *qtr = '\0';
    basename(filename);
    return filename;
}

char *getBoundary(char *data) {
    char *boundary = calloc(256, sizeof(char)), *ptr = data, *qtr = NULL, needle[10] = "boundary=";
    ptr = strstr(data, needle);
    if (ptr == NULL)
        return NULL;
    ptr = ptr + strlen(needle);
    qtr = boundary;
    while (*ptr != '\r' && *ptr != '\n') { *qtr++ = *ptr++; }
    *qtr = '\0';
    return boundary;
}

int getFileContent(char *req, char *filename, char *boundary) {
    FILE *f = fopen(filename, "w+");
    char *ptr = strstr(req, boundary), *end = NULL, *qtr = NULL, *data = calloc(10000, sizeof(char));
    ptr = strstr(ptr + strlen(boundary), boundary); // The second boundary is th start of Content-Disposition:...
    end = strstr(ptr + strlen(boundary), boundary); // The end of file
    if (ptr == NULL || end == NULL)                 // Can not find boundary
        return 0;
    for (int i = 0; i < 4; ++i) {
        while (*ptr != '\n') { ptr++; }             // Jump the boundary and Content-Disposition:...
        ptr++;
    }
    qtr = data;
    while (ptr < end) { *qtr++ = *ptr++; }
    *qtr = '\0';

    int ret = fwrite(data, sizeof(char), strlen(data) - 2, f);
    if (ret >= 0) return 1;
    return 0;
}

int main() {
    int fd;
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    unsigned val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in sin;
    bzero(&sin, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(8080);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(-1);
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(-1);
    }

    signal(SIGCHLD, SIG_IGN); // prevent child zombie

    struct sockaddr_in psin;
    while (1) {
        socklen_t psinLen = sizeof(psin);

        int pfd = accept(fd, (struct sockaddr *)NULL, NULL);
        fprintf(stderr, "accept!\n");
        if (pfd < 0) {
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        }
        if (pid == 0) {
            char header[] =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                ;
            write(pfd, header, sizeof(header) - 1);

            // int imgfd = open("test.jpeg", O_RDONLY);
            // char buf[8192];
            // for (int s = 0; (s = read(imgfd, buf, 1024)) > 0; ) {
            //     write(pfd, buf, s);
            // }

            FILE *html = fopen("b.html", "r");
            char buf[1025];
            while (fread(buf, sizeof(char), 1, html)) {
                write(pfd, buf, strlen(buf));
            }

            // Get file name
            char req[50000];
            recv(pfd, req, sizeof(req), 0);
            // If recv req is POST
            if (*req == 'P') {
                // Get filename
                char *filename = calloc(256, sizeof(char));
                filename = getFileName(req);
                if (filename == NULL) { continue; } // There is no filename
                printf("Upload %s success!\n", filename);

                // Get boundary
                char *boundary = calloc(256, sizeof(char));
                boundary = getBoundary(req);
                if (boundary == NULL) { continue; } // There is no boundary
                printf("Get boundary: %s!\n", boundary);

                // Read file content
                if (!getFileContent(req, filename, boundary)) { perror("getFileContent"); }
            }

            shutdown(pfd, SHUT_RDWR);
            close(pfd);
            fclose(html);
            exit(0);
        }
        // close(pfd);
    }
}
