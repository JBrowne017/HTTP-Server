#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <stdio.h>
#include <regex.h>

#pragma once

// Specifies a block of bytes
#define BLOCK_2048 2048
#define BLOCK_256  256

// REGEX used for parsing the request-line
#define REQ_REGEX  "([a-zA-Z]+)[ ]+(/+(/?[a-zA-Z0-9_.])+)*[ ]+(HTTP/1.1)[\r\n]"
#define METH_REGEX "^([a-zA-Z]+)"
#define HF_REGEX   "([a-zA-Z0-9_.-]+: [^\r\n]+)"
#define URI_REGEX  "/+(/?[a-zA-Z0-9_.])*"
#define HTTP_REGEX "HTTP/1.1"
#define DIR_REGEX  "([./]+/?[A-z0-9]+)+/"

enum STATUS_CODES {
    OK = 200,
    CREATED = 201,
    BAD_REQ = 400,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTER_SERV_ERROR = 500,
    NOT_IMPL = 501
};

// @brief Processes the response to the request.
// @param connfd The connections descriptor.
// @param content_length The length of the response.
// @param status_code The relevant status code to the processed request.
void handle_response(int connfd, int content_length, int *status_code);

// @brief Processes any audit logging for keeping track of processed requests.
// @param logfile The FILE type for the logfile
// @param buffer Buffer containing the request type and URI.
// @param status_code The relevant status code to the processed request.
void handle_log(FILE *logfile, char *buffer, int *status_code);

// @brief Processes the requests URI. Opens the valid URI or creates a URI if the URI specified is not present.
// @param method The request type.
// @param uri_path The specified path to open or create the URI for the request.
// @param uri_fd Pointer to keep track the URI descriptor among multiple functions.
// @param status_code Pointer for the status_code used across multiple functions.
void handle_urifd(int *method, char *uri_path, int *uri_fd, int *status_code);

// @brief Takes in bytes from the buffer and uses REGEX to parse for information relevant to the request such as the request type, URI, and message.
// @param buffer Buffer containting all relevant bytes present in the inbound request.
// @param method Pointer to keep track of the request's type across multiple functions.
// @param uri_fd Pointer to keep track of the URI's file descriptor across multiple functions.
// @param uri String containing the location of the request's URI.
// @param status_code Pointer to keep track of the status_code across multiple functions.
void handle_request(char *buffer, int *method, int *uri_fd, char *uri, int *status_code);

// @brief Uses REGEX to parse for relevant header-fields such as Content-Length.
// @param buffer Buffer containing bytes of the request-line.
// @param hf Buffer to hold header-fields parsed from the request buffer.
// @param length Variable for the content length of the request's message field.
// @param Current status_code of the request.
void handle_hf(char *buffer, char hf[BLOCK_2048], int *length, int *status_code);

// @brief Processes the message portion of the request. Writes from infile to outfile. Polls for stale connections.
// @param File descriptor for the file to read bytes from.
// @param File descriptor to write bytes to.
// @param Length of the specified message.
// @param Current status code of the request.
void handle_message(int in, int out, int *length, int *status_code);

// @brief Function for a PUT request. Writes the request's message to a tempfile and uses locks and sendfile to the specified URI to ensure fully atomic behavior.
// @param urifd File descriptor to the requested URI.
// @param uri Path specifying the requested URI.
// @param connfd File descriptor for the currently opened connection.
// @param content_length Length of the requested message.
// param status_code Current status code of the request.
void put_request(int urifd, char *uri, int connfd, int content_length, int *status_code);

// @brief Processes a GET request. Reads all bytes from the specified URI to a tempfile then writes to the socket to ensure atomicity.
// @param urifd File descriptor to the requested URI.
// @param uri Path specifying the requested URI.
// @param connfd File descriptor for the currently opened connection.
// @param content_length Length of the requested message.
// param status_code Current status code of the request.
void get_request(int urifd, char *uri, int connfd, int content_length, int *status_code);

// @brief Processes a APPEND request. Functions in the same way as a PUT request but writes to an OFFSET. This OFFSET being the EOF of the outfile.
// @param urifd File descriptor to the requested URI.
// @param uri Path specifying the requested URI.
// @param connfd File descriptor for the currently opened connection.
// @param content_length Length of the requested message.
// param status_code Current status code of the request.
void append_request(int urifd, char *uri, int connfd, int content_length, int *status_code);
