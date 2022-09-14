#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <poll.h>
#include <semaphore.h>
#include "utils.h"

#define DEFAULT_THREAD_COUNT 4
#define OPTIONS              "t:l:"
static FILE *logfile;
#define LOG(...) handle_log(logfile, __VA_ARGS__);

pthread_mutex_t mutex;
pthread_mutex_t lock;
pthread_cond_t add_conn;
pthread_cond_t take_conn;
int socket_count = 0;
pthread_t *thread_pool;
int thread_count = 0;

typedef struct conn_struct {
    char buffer[BLOCK_2048];
    int fd;
    int bytes_read;
} conn_struct;

conn_struct *conn_queue[BLOCK_2048];

conn_struct *get_connection(void);
void submit_connection(conn_struct *conn);
void *thread_dispatch(void *args);
void handle_connection(conn_struct *conn);

conn_struct *get_connection(void) {
    pthread_mutex_lock(&mutex);
    while (socket_count == 0) {
        pthread_cond_wait(&add_conn, &mutex);
    }
    assert(socket_count > 0);
    socket_count -= 1;
    conn_struct *conn = conn_queue[0];
    for (int i = 0; i < socket_count; i += 1) {
        conn_queue[i] = conn_queue[i + 1];
    }
    pthread_cond_signal(&take_conn);
    pthread_mutex_unlock(&mutex);
    return conn;
}

void submit_connection(conn_struct *conn) {
    pthread_mutex_lock(&mutex);
    if (socket_count == BLOCK_2048 - 1) {
        pthread_cond_wait(&take_conn, &mutex);
    }
    conn_queue[socket_count] = conn;
    socket_count += 1;
    pthread_cond_signal(&add_conn);
    pthread_mutex_unlock(&mutex);
    return;
}

void *thread_dispatch(void *args) {
    (void) args;
    struct pollfd pollfd[1];
    for (;;) {
        conn_struct *conn = get_connection();
        pollfd[0].fd = conn->fd;
        pollfd[0].events = POLLIN;
        while (poll(pollfd, 1, 100) < 0) {
            submit_connection(conn);
            conn = get_connection();
            pollfd[0].fd = conn->fd;
        }
        handle_connection(conn);
    }
}

void handle_connection(conn_struct *conn) {
    void (*Method_Functions[])() = { put_request, get_request, append_request };
    char hf[BLOCK_2048] = { 0 };
    int method = -1;
    int length = 0;
    int local_read = 0;
    int uri_fd = -1;
    char uri[BLOCK_2048] = { 0 };
    int status_code = OK;

    while ((local_read = read(conn->fd, conn->buffer + conn->bytes_read, 1)) > 0) {
        conn->bytes_read += local_read;
        if (conn->bytes_read >= BLOCK_2048 && strstr(conn->buffer, "\r\n\r\n") == NULL) {
            status_code = BAD_REQ;
            break;
        }
        if (strstr(conn->buffer, "\r\n\r\n") != NULL) {
            break;
        }
    }
    if (local_read <= -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            submit_connection(conn);
            return;
        } else {
            status_code = BAD_REQ;
        }
    }

    if (status_code == OK) {
        handle_request(conn->buffer, &method, &uri_fd, uri, &status_code);
        if (status_code == OK || status_code == CREATED) {
            handle_hf(conn->buffer, hf, &length, &status_code);
            if (status_code == OK || status_code == CREATED) {
                Method_Functions[method](uri_fd, uri, conn->fd, length, &status_code, &lock);
            } else {
                pthread_mutex_lock(&lock);
                handle_response(conn->fd, length, &status_code);
            }
        } else {
            pthread_mutex_lock(&lock);
            handle_response(conn->fd, length, &status_code);
        }
    } else {
        pthread_mutex_lock(&lock);
        handle_response(conn->fd, length, &status_code);
    }
    if (uri_fd != -1) {
        close(uri_fd);
        uri_fd = -1;
    }

    close(conn->fd);
    LOG(conn->buffer, &status_code);
    pthread_mutex_unlock(&lock);
    free(conn);
    return;
}

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
    char *last;
    long num = strtol(number, &last, 10);
    if (num <= 0 || num > UINT16_MAX || *last != '\0') {
        return 0;
    }
    return num;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
    struct sockaddr_in addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err(EXIT_FAILURE, "socket error");
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        err(EXIT_FAILURE, "bind error");
    }
    if (listen(listenfd, 128) < 0) {
        err(EXIT_FAILURE, "listen error");
    }
    return listenfd;
}

static void sigterm_handler(int sig) {
    if (sig == SIGTERM) {
        for (int i = 0; i < thread_count; i += 1) {
            pthread_cancel(thread_pool[i]);
            pthread_join(thread_pool[i], NULL);
            pthread_mutex_unlock(&mutex);
        }
        for (int i = 0; i < socket_count; i += 1) {
            close(conn_queue[i]->fd);
            free(conn_queue[i]);
        }
        pthread_mutex_destroy(&mutex);
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&add_conn);
        pthread_cond_destroy(&take_conn);
        free(thread_pool);
        warnx("received SIGTERM");
        fclose(logfile);
        exit(EXIT_SUCCESS);
    }
}

static void usage(char *exec) {
    fprintf(stderr, "usage: %s [-t threads] [-l logfile] <port>\n", exec);
}

int main(int argc, char *argv[]) {
    int opt = 0;
    int threads = DEFAULT_THREAD_COUNT;
    logfile = stderr;
    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't':
            threads = strtol(optarg, NULL, 10);
            if (threads <= 0) {
                errx(EXIT_FAILURE, "bad number of threads");
            }
            break;
        case 'l':
            logfile = fopen(optarg, "w");
            if (!logfile) {
                errx(EXIT_FAILURE, "bad logfile");
            }
            break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind >= argc) {
        warnx("wrong number of arguments");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t port = strtouint16(argv[optind]);
    if (port == 0) {
        errx(EXIT_FAILURE, "bad port number: %s", argv[1]);
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sigterm_handler);
    int listenfd = create_listen_socket(port);

    thread_count = threads;
    thread_pool = calloc(threads, sizeof(pthread_t));
    pthread_mutex_init(&lock, NULL);
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&add_conn, NULL);
    pthread_cond_init(&take_conn, NULL);
    for (int i = 0; i < threads; i += 1) {
        pthread_create(&thread_pool[i], NULL, &thread_dispatch, NULL);
    }

    for (;;) {
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            warn("accept error");
            continue;
        } else {
            conn_struct *conn = malloc(sizeof(conn_struct));
            fcntl(connfd, F_SETFL, O_NONBLOCK);
            conn->fd = connfd;
            memset(conn->buffer, 0, BLOCK_2048);
            conn->bytes_read = 0;
            submit_connection(conn);
        }
    }

    return EXIT_SUCCESS;
}
