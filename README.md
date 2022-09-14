# HTTP Server with Audit Logging
The following program is an implementation of a client-server system done in C. This implementation was a practice of modularity, robustness, abstraction, and to study the inner-workings of a client-server system. The implementation in question follows that of the HTTP 1.1 protocol described in the [RFC 2616](https://www.ietf.org/rfc/rfc2616.txt).  This program implements a simplified HTTP server by having three of the methods described the RFC 2616: GET, PUT, and APPEND. The server end will create, listen, and accept connections on the socket listening on a specified port. The socket will receive bytes, parse said bytes for request information, and based on the specified request will execute it.
>For information on design choices and program features, view the **Program Design** section below.
# Program Usage
## Building
```
<make>					Creates all binaries and their required object files.
<make all>				Creates all binaries and their required object files.
<make httpserver>		Creates the httpserver binary and its required object files.
<make clean>			Cleans all binaries and their required object files.
```
## Running
### Server
`./httpserver <port_number>`
### Client
You may run the client in several different ways. Two such ways is through **netcat** or **curl**.

**Netcat:** `printf "METHOD URI HTTP/1.1\r\nContent-Length: <length>\r\n\r\nMessage Body" | nc -NC localhost <server_port>`
You may also use `(printf "METHOD URI HTTP/1.1\r\nContent-Length: <file_size>\r\n\r\n; cat <file_nane>) | nc localhost <server_port>` in order to send the contents of a file through your request.

**Curl:** `curl http://localhost:<server_port>/<uri> -o <output_file>`

Note that **GET** does not need to be proceeded by a valid Content-Length header, but **PUT** and **APPEND** must be a non negative length.
The grammar for a proper URI must be preceded by a / and must not be proceeded by a / as directories are not valid URIs.

#### Request Grammar
```
Request-Line\r\n
(Header-Field\r\n)*
\r\n
Message-Body
```
## Requests
The following three requests are the current ways to interact with the HTTP server as the client. Each method leads the request line then runs as specified. Regardless of the type of request, a valid request must still follow the aforementioned grammar.
### GET
The **GET** request indicates that you, the client, would like to receive the contents of the specified file in your request. For each GET request, the **httpserver** will produce a response indicating the *status-code* and the *message* if no errors occurred. The message being the file contents. The message will also be preceded by its length in number of bytes.
### PUT
A valid **PUT** request indicated that you would like to replace the contents of the specified file. If a valid file is requested, and the file does indeed exist, then its contents will be truncated and the *message* body in the request will overwrite the file's contents. However, if the specified file does not exist, then a new file will be created and its contents will be the contents of the *message* body. After a successful request, the response will consist of the *status-code*.
### APPEND
The **APPEND** request works in the same way as the aforementioned **PUT** request. The exceptions being that the contents of the *message* body will be written to the end of the specified file and the file must exist in order to write to it. **APPEND** does not create the file if it does not exist. The response, on success, consists of the *status-code*.
## Status Codes and Responses
### Status Codes
|Status Code| Status Number| Status Cause  |
|-----------|--------------|---------------|
|OK |200| Successful Request
|CREATED |201|Resource Created
|BAD REQUEST |400|Bad Request Format
| FORBIDDEN |403|No Authorization
| NOT FOUND |404|No Matching URI
| INTERNAL ERROR |500|Unexpected Server Error
| NOT IMPLEMENTED |501|Functinality Not Supported
> Read more about these status codes in the RFC 2616.

### Responses
Responses will come in the following format. With the exception being successful **GET** requests which will have a message consisting of the file contents rather than the status phrase.
```
HTTP-Version Status-Code Status-Phrase\r\n
Content-Length: <Length of Phrase>\r\n
\r\n
<Status Phrase>\n
```

### Audit Logging
The audit log is a storage method for keeping track of the requests that were made to the server. When the server processes each request, it will add an entry to said log. Each entry has the following format `<METHOD>,<URI>,<Status-Code>,<Request-ID>\n`. The user can be assured that each log entry will not be partial or overwritten and will be consistent with the response of the server.

# Program Design
The overall design of this implementation of an HTTP server was meant as an exercise for simple systems design, multithreading, pipelining, and maintaining atomicity within a server-client program. As such this is a very simple implementation of an HTTP/1.1 protocal server with pipelining introduced. Handling pipelining introduces the complication of having to handle an unknown amount of incoming request connections with the caveat of not blocking incoming request to force a one-at-a-time system. This is where the practice of multithreading was useful in allowing each incoming connection to be handled by the first available thread in the server. The goal of multithreading and pipelining is to ensure the highest frequency of concurrency as possible while still mainting atomic and accurate requests. The issue that now arises however is the execution of non-idempotent requests and making sure every request is fully atomic. If one client requests a partial PUT while another client requests a full GET then the first client finishes their PUT request, the GET client will receive the partial PUT from client one. 
> More on multithreading and solutions to atomic operations in the **Multi-Threading** section below.

## Multi-Threading
Multi-threading is a way to implement parallelism in a program. By adding threads, we are able to enable the server to serve multiple client-requests simultaneously thus improving the throughput of the server-client. By following a thread-pool style design, we can create N number of threads and whenever a new thread is needed, which in our case is when a new client sends a request, we can simply (1) access the first available thread in the thread pool (2) queue up requests so when a thread is not in use, it can quickly grab the most recent in coming request. We can implement this by creating a queue and placing the client-requests in the queue. So when a worker-thread is done, the dispatcher can take the first request in the queue and provide it to the worker-thread.
```c
produce() {
	lock thread
	if semaphore == queue-size, wait on condition
	add one to queue
	unlock thread
	return
}
consume() {
	lock thread
	if semaphore == 0 ( queue empty ), wait on condition
	take from front of queue
	unlock thread
	return
}
runner() {
	infinite loop {
		consume()
		if connection stale
			produce()
			consume()
		handle_connection()
	}
}
```
> See the **Program Usage** section above for how to use threads.

### Thread-Safety
The following implementation of a multi-threaded HTTP server is indeed thread-safe. By implementing thread-locks via mutex, we are able to keep I/O operations both coherent and atomic. Doing this prevents possible errors such as race-conditions, overwritting information, and a lack of atomicity. On crucial portions of the program that require one-at-a-time use, we can provide a thread-lock to only allow one thread to work on that portion at a time. However we must be very conservative on how often and where we implement thread-locks, as they negate the efficiency gained by threading because they change operations to be sequential rather than in parallel.

### Non-Blocking IO and Polling
In addition to the funcionality of a thread pool and utilizing thread-safe functions. This implementation also uses Non Blocking IO and Polling. Non-Blocking IO is used when a regular syscall would hang on a Read/Write, non-blocking would simply return immediantly and the server will re-queue the stale connection until it is ready to be processed again. How do we know if a connection is no longer stale? By using polling, we can check, with a timeout if neccessary, if a connection has some data to be processed. 

### Atomicity and Idempotency
With the introduction of multithreading and pipelining in our HTTP server we have to account for the eventuality that may be a partial requests in conjunction with Non-idempotent requests. Since we are allowing multiple client connections to run at the same time, one client may execute a non-idempotent request such as PUT as a partial request while another client requests the same URI before the first PUT request was fully executed. The solution for this is to ensure that each request is fully atomic, meaning it must be completed or fail entirely. So when two or more clients request the same URI and one request is non-idempotent, each request must finish before the other may access the information within the URI. The solution within this implementation is a combination of ensuring that if there is a non-idempotent request, that it must be fully atomic, and having every request write to a temporary file before modifying the URI in the case that the connection goes stale, is partial, or errors for some other reason. 

## Request Modules
Each of the following functions acts as its only module so that when we call our method. We do not need to repeat ourselves but only need to call the function required. This also works as a layer of abstraction as we only need to provide each function with the proper inputs without having to worry about what is happening inside. This avoids repetition and was designed in a way that would allow for easy bug and error handling. As each module is made to do one specific thing, such as parses the request-line or send the message.
```c
handle_response ( connection_file_descriptor, content_length ) {
	if method get
		format response with status code and message
		write response
	else
		format reponse with status code
		write response
}
handle_urifd ( *method, *uri_path, *uri_file_descriptor ) {
	format uri_path
	if put
		open with read only and truncate file
		if error
			if no file, create file
			if access error, status-code <- forbidden
			if uri is directory, status-code <- forbidden
			else status-code <- bad request
	if get
		open file with read only
		if error
			if no entry, status-code <- not found
			if no access, status-code <- forbidden
			if uri is directory, status-code <- forbidden
			else status-code <- bad request
	if append
		open with read only and append to file
		if error
			if no file, create file
			if access error, status-code <- forbidden
			if uri is directory, status-code <- forbidden
			else status-code <- bad request	
}
handle_request ( *buffer, *method, *uri_file_descriptor ) {
	regex for proper request grammer
	regex for method
	regex for uri
	regex for http-version
	if at any point no match, status-code <- bad request
}
handle_hf ( *buffer, *hf, *content_length) {
	regex each header for proper grammer
	if header == Content-Length
		get length
	if at any point no match, status-code <- bad request
}
handle_message ( infile, outfile, content_length ) {
	read_bytes(infile, buffer, content_length)
	write_bytes(outfile, buffer, content_length);
	if at any point errored, status-code <- bad request
}
handle_put ( uri_file_descriptor, connection_file_descriptor, content_length ) {
	handle_message(connection_file_descriptor, uri_file_descriptor, content_length)
	handle_response(connection_file_descriptor, 0)
}
handle_get ( uri_file_descriptor, connection_file_descriptor, content_length ) {
	handle_message(uri_file_descriptor, connection_file_descriptor, content_length)
	handle_response(connection_file_descriptor, content_length)
}
handle_append ( uri_file_descriptor, connection_file_descriptor, content_length ) {
	handle_message(connection_file_descriptor, uri_file_descriptor, content_length)
	handle_response(connection_file_descriptor, 0)
}
```

## Handle Connection
Due to the modularity of our implementation, the handle_connection portion of the program only needs to do three things. Create the required variables to be shared across all required modules, read in the request line, and then call each module accordingly.
```
buffer[BUF_BLOCK]
header_buffer[BUF_BLOCK]
*Method_Functions <- { put, get, append }
method
content_length
uri_file_descriptor

while we have not read the entire request_length
	read more bytes
	if we have a double CRLF
		break out of loop
	if we have read more than 2048 bytes and have no double CRLF
		status-code <- bad_request
		break out of loop
		
handle_request ( buffer, &method, &uri_file_descriptor )
handle_hf ( buffer, hf, &content_length )
Method_Functions[method](uri_file_descriptor, connection_file_descriptor, content_length)
if error
	handle_response(connection_file_decriptor, 0)

reset buffers and variables in preparation for next request
```