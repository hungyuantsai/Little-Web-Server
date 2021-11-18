#include "webserver.h"

const char *get_content_type(const char* path) {
    const char *last_dot = strrchr(path, '.');
    if (last_dot) {
        if (strcmp(last_dot, ".css") == 0) return "text/css";
        if (strcmp(last_dot, ".csv") == 0) return "text/csv";
        if (strcmp(last_dot, ".gif") == 0) return "image/gif";
        if (strcmp(last_dot, ".htm") == 0) return "text/html";
        if (strcmp(last_dot, ".html") == 0) return "text/html";
        if (strcmp(last_dot, ".ico") == 0) return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0) return "application/javascript";
        if (strcmp(last_dot, ".json") == 0) return "application/json";
        if (strcmp(last_dot, ".png") == 0) return "image/png";
        if (strcmp(last_dot, ".pdf") == 0) return "application/pdf";
        if (strcmp(last_dot, ".svg") == 0) return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0) return "text/plain";
    }

    return "application/octet-stream";
}


SOCKET create_socket(const char* host, const char *port) {
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);

    printf("Creating socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family,
            bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Binding socket to local address...\n");
    if (bind(socket_listen,
                bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    printf("Listening...\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return socket_listen;
}



#define MAX_REQUEST_SIZE 250000000

struct client_info {
    socklen_t address_length;
    struct sockaddr_storage address;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE + 1];
    int received;
    struct client_info *next;
};

static struct client_info *clients = 0;

struct client_info *get_client(SOCKET s) {
    struct client_info *ci = clients;

    while(ci) {
        if (ci->socket == s)
            break;
        ci = ci->next;
    }

    if (ci) return ci;
    struct client_info *n =
        (struct client_info*) calloc(1, sizeof(struct client_info));

    if (!n) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    n->address_length = sizeof(n->address);
    n->next = clients;
    clients = n;
    return n;
}


void drop_client(struct client_info *client) {
    CLOSESOCKET(client->socket);

    struct client_info **p = &clients;

    while(*p) {
        if (*p == client) {
            *p = client->next;
            free(client);
            return;
        }
        p = &(*p)->next;
    }

    fprintf(stderr, "drop_client not found.\n");
    exit(1);
}


const char *get_client_address(struct client_info *ci) {
    static char address_buffer[100];
    getnameinfo((struct sockaddr*)&ci->address,
            ci->address_length,
            address_buffer, sizeof(address_buffer), 0, 0,
            NI_NUMERICHOST);
    return address_buffer;
}




fd_set wait_on_clients(SOCKET server) {
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;

    struct client_info *ci = clients;

    while(ci) {
        FD_SET(ci->socket, &reads);
        if (ci->socket > max_socket)
            max_socket = ci->socket;
        ci = ci->next;
    }

    if (select(max_socket+1, &reads, 0, 0, 0) < 0) {
        fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return reads;
}


void send_400(struct client_info *client) {
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\nBad Request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

void send_404(struct client_info *client) {
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Length: 9\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}



void serve_resource(struct client_info *client, const char *path) {

    printf("serve_resource %s %s\n", get_client_address(client), path);

    if (strcmp(path, "/") == 0) path = "/index.html";

    if (strlen(path) > 100) {
        send_400(client);
        return;
    }

    if (strstr(path, "..")) {
        send_404(client);
        return;
    }

    char full_path[128];
    sprintf(full_path, "public%s", path);

#if defined(_WIN32)
    char *p = full_path;
    while (*p) {
        if (*p == '/') *p = '\\';
        ++p;
    }
#endif

    FILE *fp = fopen(full_path, "rb");

    if (!fp) {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);

    const char *ct = get_content_type(full_path);

#define BSIZE 1024
    char buffer[BSIZE];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %lu\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    int r = fread(buffer, 1, BSIZE, fp);
    while (r) {
        send(client->socket, buffer, r, 0);
        r = fread(buffer, 1, BSIZE, fp);
    }

    fclose(fp);
    drop_client(client);
}

#define buffersize 8192

void upload_resorce(struct client_info *client,const char *path){
    printf("upload_resource %s %s\n", get_client_address(client), path);

    FILE *fp = fopen(path, "rb");

    if (!fp) {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);

    const char *ct = get_content_type(path);

    char buffer[buffersize];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %lu\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    int r = fread(buffer, 1, buffersize, fp);
    while (r) {
        send(client->socket, buffer, r, 0);
        r = fread(buffer, 1, buffersize, fp);
    }

    fclose(fp);
    drop_client(client);

}

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

long getContentLen(char *data) {
    int contentLen = 0;
    char *ptr = strstr(data, "Content-Length: "), *qtr = NULL, *len = calloc(32, sizeof(char));
    qtr = len;
    ptr = ptr + 16;
    while (*ptr != '\n' && *ptr != '\r') {
        *qtr++ = *ptr++; 
    }
    return strtol(len, NULL, 10);
}

long getDisposLen(char *data) {
    char *ptr = strstr(data, "Content-Disposition");
    char *qtr = strstr(ptr, "\r\n");
    return qtr - ptr + 2;
}

long getTypeLen(char *data) {
    char *ptr = strstr(data, "Content-Type"), *qtr = NULL;
    ptr = strstr(ptr, "\r\n");
    ptr = strstr(ptr, "Content-Type");
    qtr = strstr(ptr, "\r\n\r\n");
    return qtr - ptr + 4;
}


int getFileContent(struct client_info *client, char *filename, char *boundary) {
    int size = 0, ret = 0;
    char *ptr = strstr(client->request, filename), *qtr = NULL;
    ptr = strstr(ptr, "Content-Type");
    ptr = strstr(ptr, "\r\n\r\n");  // Get the start of the target date

    if (ptr == NULL) return 0;
    ptr = ptr + 4;                  // Jump \r\n\r\n
    
    long contentLen = getContentLen(client->request);
    long disposLen = getDisposLen(client->request);
    long typeLen = getTypeLen(client->request);

    contentLen = contentLen - typeLen - disposLen - (strlen(boundary) + 2) * 2 - 2 - 6;

    char path[256];
    sprintf(path, "upload/%s", filename);

    FILE *f = fopen(path, "w");
    fwrite(ptr, sizeof(char), contentLen, f);
    upload_resorce(client, path);

    fclose(f);
    return 1;
}

int main() {

#if defined(_WIN32)
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif

    SOCKET server = create_socket(0, "8080");

    while(1) {
        fd_set reads;
        reads = wait_on_clients(server);

        if (FD_ISSET(server, &reads)) {
            struct client_info *client = get_client(-1);

            client->socket = accept(server,
                    (struct sockaddr*) &(client->address),
                    &(client->address_length));

            if (!ISVALIDSOCKET(client->socket)) {
                fprintf(stderr, "accept() failed. (%d)\n",
                        GETSOCKETERRNO());
                return 1;
            }


            printf("New connection from %s.\n",
                    get_client_address(client));
        }


        struct client_info *client = clients;
        while(client) {
            struct client_info *next = client->next;

            if (FD_ISSET(client->socket, &reads)) {

                if (MAX_REQUEST_SIZE == client->received) {
                    send_400(client);
                    client = next;
                    continue;
                }

                int r = recv(client->socket, client->request + client->received, MAX_REQUEST_SIZE - client->received, 0);
                
                char *ptr = client->request;
                for (int i = 0; i < r; i++)
                    printf("%x\n", *(ptr+i));
                
                if (r < 1) {
                    printf("Unexpected disconnect from %s.\n",
                            get_client_address(client));
                    drop_client(client);

                } else {
                    client->received += r;
                    client->request[client->received] = 0;

                    char *q = strstr(client->request, "\r\n\r\n");
                    if (q) {
                        if (strncmp("GET /", client->request, 5) && strncmp("POST /", client->request, 6)) {
                            *q = 0;
                            send_400(client);
                        }
                        else if (strncmp("POST /", client->request, 6) == 0) {
                            // Get filename
                            char *filename = calloc(256, sizeof(char));
                            filename = getFileName(client->request);
                            if (filename == NULL) { // There is no filename
                                fprintf(stderr, "Server: Get filaneme failed!\n");
                                continue;
                            } 
                            printf("Server: Get filename: %s!\n", filename);

                            // Get boundary
                            char *boundary = calloc(256, sizeof(char));
                            boundary = getBoundary(client->request);
                            if (boundary == NULL) { // There is no boundary
                                fprintf(stderr, "Server: Get boundary failed!\n");
                                continue;
                            } 
                            printf("Server: Get boundary: %s!\n", boundary);

                            // Read file content
                            if (!getFileContent(client, filename, boundary)) {
                                fprintf(stderr, "Server: Get file content failed!\n");
                                continue;
                            }
                            printf("Server: Upload file seccess!\n");
                        }
                        else {
                            *q = 0;
                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");

                            if (!end_path) {
                                send_400(client);

                            } else {
                                *end_path = 0;
                                serve_resource(client, path);

                            }
                        }
                    }
                }
            }

            client = next;
        }

    } //while(1)


    printf("\nClosing socket...\n");
    CLOSESOCKET(server);


#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}

