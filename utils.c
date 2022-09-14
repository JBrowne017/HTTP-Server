#include "utils.h"
#include <err.h>
#include <poll.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <sys/file.h>

enum METHODS { PUT, GET, APPEND };

#define LOG(...) handle_log(logfile, __VA_ARGS__);

const char *STATUS_PHRASES[] = { [OK] = "HTTP/1.1 200 OK\r\nContent-Length: 3 \r\n\r\nOK\n",
    [CREATED] = "HTTP/1.1 201 Created\r\nContent-Length: 8 \r\n\r\nCreated\n",
    [BAD_REQ] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12 \r\n\r\nBad Request\n",
    [FORBIDDEN] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10 \r\n\r\nForbidden\n",
    [NOT_FOUND] = "HTTP/1.1 404 Not Found\r\nContent-Length: 10 \r\n\r\nNot Found\n",
    [INTER_SERV_ERROR]
    = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22 \r\n\r\nInternal Server Error\n",
    [NOT_IMPL] = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16 \r\n\r\nNot Implemented\n" };

void handle_log(FILE *logfile, char *buffer, int *status_code) {
    char method[BLOCK_256] = { 0 };
    char uri[BLOCK_256] = { 0 };
    int req_id = 0;
    sscanf(buffer, "%s %s", method, uri);
    char *cursor = strstr(buffer, "Request-Id: ");
    if (cursor) {
        sscanf(cursor + 12, "%d", &req_id);
    }
    if (method[0] != 0) {
        fprintf(logfile, "%s,%s,%d,%d\n", method, uri, *status_code, req_id);
    }
    return;
}

void handle_response(int connfd, int content_length, int *status_code) {
    if (content_length > 0) {
        content_length -= 1;
        char response[BLOCK_2048];
        snprintf(
            response, BLOCK_2048, "HTTP/1.1 200 OK\r\nContent-Length: %d \r\n\r\n", content_length);
        write(connfd, response, strlen(response));
    } else {
        write(connfd, STATUS_PHRASES[*status_code], strlen(STATUS_PHRASES[*status_code]));
    }
}

void handle_dir(char *uri_path) {
    regmatch_t match;
    regex_t dir_regex;
    regcomp(&dir_regex, DIR_REGEX, REG_EXTENDED);
    if (regexec(&dir_regex, uri_path, 1, &match, 0) != 0) {
        regfree(&dir_regex);
        return;
    } else {
        char *dir_buf = (char *) calloc(((match.rm_eo - match.rm_so) + 1), sizeof(char));
        memcpy(dir_buf, uri_path + match.rm_so, (match.rm_eo - match.rm_so));

        // Make Nested Directories
        char *cursor = dir_buf;
        while (cursor < dir_buf + strlen(dir_buf)) {
            if (*cursor == '/') {
                *cursor = '\0'; // mkdir goes to null term
                mkdir(dir_buf, S_IRWXU);
                *cursor = '/';
            }
            cursor += 1;
        }

        free(dir_buf);
    }

    regfree(&dir_regex);
    return;
}

void handle_urifd(int *method, char *uri_path, int *uri_fd, int *status_code) {
    if (*method == -1) {
        *status_code = NOT_IMPL;
        return;
    }

    char tmp[BLOCK_2048] = { 0 };
    snprintf(tmp, BLOCK_2048, "./%s", uri_path);
    memcpy(uri_path, tmp, strlen(tmp));

    switch (*method) {
    case (PUT): {
        return;
    }
    case (GET): {
        *uri_fd = open(uri_path, O_RDONLY, S_IRWXU);
        if (*uri_fd == -1) {
            if (errno == ENOENT) {
                *status_code = NOT_FOUND;
            } else if (errno == EACCES) {
                *status_code = FORBIDDEN;
            } else if (errno == EISDIR) {
                *status_code = FORBIDDEN;
            } else {
                *status_code = BAD_REQ;
            }
            return;
        }
        return;
    }
    case (APPEND): {
        *uri_fd = open(uri_path, O_WRONLY, S_IRWXU);
        if (*uri_fd == -1) {
            if (errno == ENOENT) {
                *status_code = NOT_FOUND;
            } else if (errno == EACCES) {
                *status_code = FORBIDDEN;
            } else if (errno == EISDIR) {
                *status_code = FORBIDDEN;
            } else {
                *status_code = BAD_REQ;
            }
            return;
        }
        return;
    }
    }
}

void handle_request(char *buffer, int *method, int *uri_fd, char *uri, int *status_code) {
    regmatch_t match;
    regex_t uri_regex;
    regex_t method_regex;
    regex_t http_regex;
    regex_t req_regex;

    regcomp(&req_regex, REQ_REGEX, REG_EXTENDED);
    if (regexec(&req_regex, buffer, 0, NULL, 0) != 0) {
        *status_code = BAD_REQ;
        regfree(&req_regex);
        return;
    }
    regfree(&req_regex);

    // METHOD REGEX
    regcomp(&method_regex, METH_REGEX, REG_EXTENDED);
    if (regexec(&method_regex, buffer, 1, &match, 0) != 0) {
        *status_code = BAD_REQ;
        regfree(&method_regex);
        return;
    }
    regfree(&method_regex);
    char *method_buf = (char *) calloc(((match.rm_eo - match.rm_so) + 1), sizeof(char));
    memcpy(method_buf, buffer + match.rm_so, (match.rm_eo - match.rm_so));

    if (strncasecmp(method_buf, "PUT", 3) == 0) {
        *method = PUT;
    } else if (strncasecmp(method_buf, "GET", 3) == 0) {
        *method = GET;
    } else if (strncasecmp(method_buf, "APPEND", 6) == 0) {
        *method = APPEND;
    } else {
        *status_code = NOT_IMPL;
        free(method_buf);
        return;
    }

    free(method_buf);

    // URI REGEX
    regcomp(&uri_regex, URI_REGEX, REG_EXTENDED);
    if (regexec(&uri_regex, buffer, 1, &match, 0) != 0) {
        *status_code = BAD_REQ;
        regfree(&uri_regex);
        return;
    }
    regfree(&uri_regex);
    memcpy(uri, buffer + match.rm_so, (match.rm_eo - match.rm_so));
    handle_urifd(method, uri, uri_fd, status_code);

    // HTTP REGEX
    regcomp(&http_regex, HTTP_REGEX, REG_EXTENDED);
    if (regexec(&http_regex, buffer, 1, &match, 0) != 0) {
        *status_code = BAD_REQ;
        regfree(&http_regex);
        return;
    }
    regfree(&http_regex);
    return;
}

void handle_hf(char *buffer, char hf[BLOCK_2048], int *length, int *status_code) {
    regex_t regex;
    regcomp(&regex, HF_REGEX, REG_EXTENDED);
    int tmp = 0;
    char *cursor = strstr(buffer, "\r\n") + 2;
    while ((tmp = sscanf(cursor, "%[^\r\n]", hf)) > 0) {
        if (tmp < 0) {
            *status_code = BAD_REQ;
            break;
        }
        if (regexec(&regex, hf, 0, NULL, 0) != 0) {
            *status_code = BAD_REQ;
            regfree(&regex);
            return;
        }
        if (strncmp(hf, "Content-Length: ", 16) == 0) {
            sscanf(hf + 16, "%d", length);
        }
        cursor += strlen(hf) + 2;
    }
    if (tmp < 0) {
        *status_code = BAD_REQ;
    }
    regfree(&regex);
    if (*length < 0) {
        *status_code = BAD_REQ;
    }
    return;
}

void handle_message(int in, int out, int *length, int *status_code) {

    char buffer[BLOCK_2048] = { 0 };
    int bytes = 0;
    int local_read = 0;
    int local_write = 0;
    int num_bytes = (BLOCK_2048 < *length) ? BLOCK_2048 : (*length);
    struct pollfd pollfds[1];
    pollfds[0].fd = in;
    pollfds[0].events = POLLIN;

    for (;;) {
        while ((local_read = read(in, buffer, num_bytes)) > 0) {
            bytes += local_read;
            local_write = write(out, buffer, local_read);
            if (local_write == -1) {
                if (errno == EAGAIN) {
                    poll(pollfds, 1, -1);
                    continue;
                } else {
                    *status_code = BAD_REQ;
                    return;
                }
            }
            if (local_write == 0) {
                return;
            }
            if (local_write == -1) {
                printf("ERR[%s]\n", strerror(errno));
                assert(local_write != -1);
            }
            num_bytes = (BLOCK_2048 < *length - bytes) ? BLOCK_2048 : (*length - bytes);
        }
        if (*length == bytes || local_read == 0) {
            return;
        }
        if (*status_code == -1) {
            if (errno == EAGAIN) {
                poll(pollfds, 1, -1);
                continue;
            } else {
                *status_code = BAD_REQ;
                return;
            }
        }
    }

    return;
}

void put_request(int urifd, char *uri, int connfd, int length, int *code) {

    off_t offset = 0;

    int tmp_fd = open("./", __O_TMPFILE | O_RDWR, S_IRWXU);

    handle_message(connfd, tmp_fd, &length, code);

    urifd = open(uri, O_WRONLY | O_TRUNC, S_IRWXU);
    if (urifd == -1) {
        if (errno == ENOENT) {
            handle_dir(uri);
            urifd = open(uri, O_CREAT | O_WRONLY, S_IRWXU);
            *code = CREATED;
        } else {
            *code = BAD_REQ;
        }
    }
    if (*code != BAD_REQ) {
        flock(urifd, LOCK_EX);
        sendfile(urifd, tmp_fd, &offset, length);
        flock(urifd, LOCK_UN);
    }
    close(tmp_fd);
    handle_response(connfd, 0, code);
    return;
}

void get_request(int urifd, char *uri, int connfd, int length, int *code) {
    (void) uri;

    off_t offset = 0;
    struct stat uri_stat;
    fstat(urifd, &uri_stat);
    length = uri_stat.st_size;
    handle_response(connfd, length + 1, code);
    flock(urifd, LOCK_SH);
    int tmp_fd = open("./", __O_TMPFILE | O_RDWR, S_IRWXU);
    handle_message(urifd, tmp_fd, &length, code);
    flock(urifd, LOCK_UN);
    sendfile(connfd, tmp_fd, &offset, length);
    close(tmp_fd);
    return;
}

void append_request(int urifd, char *uri, int connfd, int length, int *code) {
    (void) uri;

    off_t offset = 0;

    int tmp_fd = open("./", __O_TMPFILE | O_RDWR, S_IRWXU);
    handle_message(connfd, tmp_fd, &length, code);

    lseek(urifd, 0, SEEK_END);
    if (*code != BAD_REQ) {
        flock(urifd, LOCK_EX);
        sendfile(urifd, tmp_fd, &offset, length);
        flock(urifd, LOCK_UN);
    }
    close(tmp_fd);
    handle_response(connfd, 0, code);
    return;
}
