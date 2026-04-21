#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

#define PORT 8787
#define BUFSIZE 65536

typedef int (*fn_init)(const char*);
typedef void (*fn_shutdown)(void);
typedef const char* (*fn_rpc)(const char*, const char*);
typedef const char* (*fn_status)(void);
typedef void (*fn_free)(const char*);

static fn_init    p_init;
static fn_shutdown p_shutdown;
static fn_rpc     p_rpc;
static fn_status  p_status;
static fn_free    p_free;

static volatile int running = 1;

static void handle_signal(int sig) { (void)sig; running = 0; }

static int load_lib(const char* path) {
    void* h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return -1; }
    p_init     = (fn_init)dlsym(h, "synapsed_init");
    p_shutdown = (fn_shutdown)dlsym(h, "synapsed_shutdown");
    p_rpc      = (fn_rpc)dlsym(h, "synapsed_rpc_call");
    p_status   = (fn_status)dlsym(h, "synapsed_get_status");
    p_free     = (fn_free)dlsym(h, "synapsed_free_string");
    if (!p_init || !p_rpc || !p_status || !p_free) {
        fprintf(stderr, "missing symbols\n");
        return -1;
    }
    return 0;
}

static void send_response(int fd, int code, const char* body) {
    char hdr[512];
    int blen = body ? (int)strlen(body) : 0;
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        code, blen);
    write(fd, hdr, hlen);
    if (body && blen > 0) write(fd, body, blen);
}

static void handle_request(int fd) {
    char buf[BUFSIZE];
    int total = 0;
    int n;
    while (total < BUFSIZE - 1) {
        n = read(fd, buf + total, BUFSIZE - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (total <= 0) { close(fd); return; }

    if (strncmp(buf, "OPTIONS", 7) == 0) {
        send_response(fd, 200, "{}");
        close(fd);
        return;
    }

    char* body = strstr(buf, "\r\n\r\n");
    if (body) body += 4; else body = "";

    char* cmd_start = strstr(body, "\"cmd\"");
    if (!cmd_start) {
        send_response(fd, 400, "{\"error\":\"no cmd\"}");
        close(fd);
        return;
    }

    char cmd[256] = {0};
    char* q1 = strchr(cmd_start + 5, '"');
    if (q1) {
        char* q2 = strchr(q1 + 1, '"');
        if (q2 && q2 - q1 - 1 < 255) {
            memcpy(cmd, q1 + 1, q2 - q1 - 1);
        }
    }

    const char* result = NULL;

    if (strcmp(cmd, "rpc_call") == 0 || strcmp(cmd, "synapsed_rpc_call") == 0) {
        char method[256] = {0};
        char params[BUFSIZE] = "{}";

        char* mp = strstr(body, "\"method\"");
        if (mp) {
            char* mq1 = strchr(mp + 8, '"');
            if (mq1) {
                char* mq2 = strchr(mq1 + 1, '"');
                if (mq2 && mq2 - mq1 - 1 < 255)
                    memcpy(method, mq1 + 1, mq2 - mq1 - 1);
            }
        }

        char* pp = strstr(body, "\"params\"");
        if (pp) {
            char* pq1 = strchr(pp + 8, '"');
            if (pq1) {
                char* pq2 = strchr(pq1 + 1, '"');
                if (pq2 && pq2 - pq1 - 1 < BUFSIZE - 1)
                    memcpy(params, pq1 + 1, pq2 - pq1 - 1);
            }
        }

        result = p_rpc(method, params);
    } else if (strcmp(cmd, "get_status") == 0 || strcmp(cmd, "synapsed_get_status") == 0) {
        result = p_status();
    } else if (strcmp(cmd, "init") == 0 || strcmp(cmd, "synapsed_init") == 0) {
        p_init("{}");
        result = NULL;
        send_response(fd, 200, "{\"ok\":true}");
        close(fd);
        return;
    } else {
        send_response(fd, 400, "{\"error\":\"unknown cmd\"}");
        close(fd);
        return;
    }

    if (result) {
        send_response(fd, 200, result);
        if (p_free) p_free(result);
    } else {
        send_response(fd, 200, "{\"ok\":true}");
    }
    close(fd);
}

int main(int argc, char** argv) {
    const char* libpath = argc > 1 ? argv[1] : "./libsynapsed.so.0.1.0";
    if (load_lib(libpath) < 0) return 1;

    p_init("{}");
    fprintf(stderr, "[bridge] engine initialized\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(srv, 16);
    fprintf(stderr, "[bridge] listening on 127.0.0.1:%d\n", PORT);

    while (running) {
        int cfd = accept(srv, NULL, NULL);
        if (cfd < 0) continue;
        handle_request(cfd);
    }

    if (p_shutdown) p_shutdown();
    close(srv);
    fprintf(stderr, "[bridge] shutdown\n");
    return 0;
}
