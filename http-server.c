#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <signal.h>

#define MAXPENDING 5
#define DISK_IO_BUF_SIZE 4096
#define MAX_REQ_LINE_SIZE 2000

static void terminateWithError(const char *msg) {
    perror(msg);
    exit(1);
}

static struct {
    int status;
    char *reason;
} HTTP_StatusCodes[] = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 501, "Not Implemented" },
    { 0, NULL }
};
//Declarimg the functions. 
static int initListeningSocket(unsigned short port);
static int initMdbConnection(const char *host, unsigned short port);
static ssize_t safeWrite(int sock, const char *data);
static const char *getStatusReason(int code);
static void sendStatus(int sock, int code);
static void sendError(int sock, int code);
static int handleMdb(const char *uri, FILE *mdbFp, int mdbSock, int clntSock);
static int handleFile(const char *root, const char *uri, int clntSock);
//Initializing a listening socket on the specified port.
static int initListeningSocket(unsigned short port) {
    int s;
    struct sockaddr_in addr;

    if ((s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        terminateWithError("socket");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        terminateWithError("Error binding the socket to the specified port");
    if (listen(s, MAXPENDING) < 0)
        terminateWithError("Error setting the socket to listen mode");

    return s;
}

static int initMdbConnection(const char *host, unsigned short port) {
    int s;
    struct sockaddr_in srv;
    struct hostent *he;
//Resolving the hostname to an IP address.
    if ((he = gethostbyname(host)) == NULL)
        terminateWithError("gethostbyname");

    char *ip = inet_ntoa(*(struct in_addr *)he->h_addr);
    if ((s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        terminateWithError("Failed to create socket");

    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(ip);
    srv.sin_port = htons(port);

    if (connect(s, (struct sockaddr *)&srv, sizeof(srv)) < 0)
        terminateWithError("connect");

    return s;
}

static ssize_t safeWrite(int sock, const char *data) {
    size_t len = strlen(data);
    ssize_t n = send(sock, data, len, 0);
    if (n != (ssize_t)len) {
        perror("send");
        return -1;
    }
    return n;
}

static const char *getStatusReason(int code) {
    for (int i = 0; HTTP_StatusCodes[i].status; i++) {
        if (HTTP_StatusCodes[i].status == code)
            return HTTP_StatusCodes[i].reason;
    }
    return "Unknown";
}
//Sending the HTTP status line to the client.
static void sendStatus(int sock, int code) {
    char buf[1000];
    const char *r = getStatusReason(code);
    sprintf(buf, "HTTP/1.0 %d %s\r\n", code, r);
    safeWrite(sock, buf);
}

static void sendError(int sock, int code) {
    sendStatus(sock, code);
    safeWrite(sock, "\r\n");
    char buf[1000];
    sprintf(buf, "<html><body><h1>%d %s</h1></body></html>\n", code, getStatusReason(code));
    safeWrite(sock, buf);
}
//Handling requests to the mdb-lookup endpoint.
static int handleMdb(const char *uri, FILE *mdbFp, int mdbSock, int clntSock) {
    int code = 200;
    const char *form =
        "<html><body>\n"
        "<h1>mdb-lookup</h1>\n"
        "<p>\n"
        "<form method=GET action=/mdb-lookup>\n"
        "lookup: <input type=text name=key>\n"
        "<input type=submit>\n"
        "</form>\n"
        "<p>\n";

    const char *prefix = "/mdb-lookup?key=";

    if (strncmp(uri, prefix, strlen(prefix)) == 0) {
        const char *key = uri + strlen(prefix);
        if (safeWrite(mdbSock, key) < 0 || safeWrite(mdbSock, "\n") < 0) {
            code = 501;
            sendError(clntSock, code);
            perror("mdb-lookup-server");
            return code;
        }
//Sending the HTTP status and form to the client.
        sendStatus(clntSock, code);
        safeWrite(clntSock, "\r\n");
        safeWrite(clntSock, form);
        safeWrite(clntSock, "<p><table border>");

        char line[1000];
        int row = 1;
        const char *rainbowColors[] = {"red","orange","yellow","green","blue","indigo","violet"};
        while (fgets(line, sizeof(line), mdbFp)) {
            if (strcmp(line, "\n") == 0)
                break;
            char colorBuffer[1000];
            sprintf(colorBuffer, "<tr><td bgcolor=%s>", rainbowColors[(row++) % 7]);

            const char *color = (1) ? colorBuffer : colorBuffer;

            safeWrite(clntSock, color);
            safeWrite(clntSock, line);
        }
        safeWrite(clntSock, "</table>");
    } else {
        sendStatus(clntSock, code);
        safeWrite(clntSock, "\r\n");
        safeWrite(clntSock, form);
    }

    safeWrite(clntSock, "</body></html>\n");
    return code;
}
//Handling requests for static files.

static int handleFile(const char *root, const char *uri, int clntSock) {
    int code;
    FILE *fp = NULL;

    char *path = malloc(strlen(root) + strlen(uri) + 100);
    if (!path) terminateWithError("malloc");
    strcpy(path, root);
    strcat(path, uri);

    if (path[strlen(path)-1] == '/')
        strcat(path, "index.html");

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        code = 403;
        sendError(clntSock, code);
        free(path);
        return code;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        code = 404;
        sendError(clntSock, code);
        free(path);
        return code;
    }

    code = 200;
    sendStatus(clntSock, code);
    safeWrite(clntSock, "\r\n");

    char buf[DISK_IO_BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send(clntSock, buf, n, 0) != (ssize_t)n) {
            perror("send");
            break;
        }
    }
    //Checking for read errors.
    if (ferror(fp))
        perror("fread");

    free(path);
    fclose(fp);
    return code;
}

int main(int argc, char *argv[]) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        terminateWithError("signal");

    if (argc != 5) {
        fprintf(stderr, "usage: %s <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n", argv[0]);
        exit(1);
    }

    unsigned short port = atoi(argv[1]);
    const char *root = argv[2];
    const char *mdbHost = argv[3];
    unsigned short mdbPort = atoi(argv[4]);

    int mdbSock = initMdbConnection(mdbHost, mdbPort);
    FILE *mdbFp = fdopen(mdbSock, "r");
    if (!mdbFp) terminateWithError("fdopen");

    int listenSock = initListeningSocket(port);

    char hdr[1000];
    char req[MAX_REQ_LINE_SIZE];
    int status;
    struct sockaddr_in cAddr;

    for (;;) {
        unsigned int cLen = sizeof(cAddr);
        int cSock = accept(listenSock, (struct sockaddr *)&cAddr, &cLen);
        if (cSock < 0) {
            perror("accept");
            continue;
        }

        FILE *cFp = fdopen(cSock, "r");
        if (!cFp) {
            perror("fdopen");
            close(cSock);
            continue;
        }

        if (!fgets(req, sizeof(req), cFp)) {
            status = 400;
            goto done;
        }

        char *seps = "\t \r\n";
        char *method = strtok(req, seps);
        char *uri = strtok(NULL, seps);
        char *vers = strtok(NULL, seps);
        char *extra = strtok(NULL, seps);

        if (!method || !uri || !vers || extra) {
            status = 400;
            sendError(cSock, status);
            goto done;
        }

        if (strcmp(method, "GET") != 0) {
            status = 501;
            sendError(cSock, status);
            goto done;
        }

        if (strcmp(vers, "HTTP/1.0") != 0 && strcmp(vers, "HTTP/1.1") != 0) {
            status = 501;
            sendError(cSock, status);
            goto done;
        }

        if (uri[0] != '/') {
            status = 400;
            sendError(cSock, status);
            goto done;
        }

        int len = (int)strlen(uri);
        if ((len >= 3 && strcmp(uri + len - 3, "/..") == 0) || strstr(uri, "/../") != NULL) {
            status = 400;
            sendError(cSock, status);
            goto done;
        }

        for (;;) {
            if (!fgets(hdr, sizeof(hdr), cFp)) {
                status = 400;
                goto done;
            }
            if (strcmp(hdr, "\r\n") == 0 || strcmp(hdr, "\n") == 0)
                break;
        }

        if (strncmp(uri, "/mdb-lookup", 11) == 0) {
            status = handleMdb(uri, mdbFp, mdbSock, cSock);
        } else {
            status = handleFile(root, uri, cSock);
        }

done:
        fprintf(stderr, "%s \"%s %s %s\" %d %s\n",
                inet_ntoa(cAddr.sin_addr),
                method ? method : "-",
                uri ? uri : "-",
                vers ? vers : "-",
                status,
                getStatusReason(status));
        fclose(cFp);
    }

    return 0;
}

