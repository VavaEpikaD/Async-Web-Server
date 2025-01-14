// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	conn->send_len = snprintf(conn->send_buffer, BUFSIZ,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: %ld\r\n"
		"\r\n", conn->file_size);
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	conn->send_len = snprintf(conn->send_buffer, BUFSIZ,
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 0\r\n"
		"\r\n");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->request_path, "static") != NULL) {
		return RESOURCE_TYPE_STATIC;
	} else if (strstr(conn->request_path, "dynamic") != NULL) {
		return RESOURCE_TYPE_DYNAMIC;
	}

	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *conn = malloc(sizeof(struct connection));
	DIE(conn == NULL, "malloc");

	conn->sockfd = sockfd;
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);
	memset(conn->filename, 0, BUFSIZ);
	memset(conn->request_path, 0, BUFSIZ);
	conn->file_pos = 0;
	conn->recv_len = 0;
	conn->state = STATE_RECEIVING_DATA;

	return conn;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	if (conn->fd >= 0)
		close(conn->fd);

	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;

	free(conn);
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	/* TODO: Accept new connection. */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");

	dlog(LOG_ERR, "Accepted connection from: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	/* TODO: Set socket to be non-blocking. */
	int flags = fcntl(sockfd, F_GETFL, 0);
	DIE(flags < 0, "fcntl F_GETFL");

	rc = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	DIE(rc < 0, "fcntl F_SETFL");

	/* TODO: Instantiate new connection handler. */
	conn = connection_create(sockfd);

	/* TODO: Add socket to epoll. */
	rc = w_epoll_add_ptr_in(epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_ptr_in");

	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
	conn->request_parser.data = conn;
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	int bytes_read = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ - conn->recv_len, 0);
	DIE(bytes_read < 0, "recv");

	if (bytes_read == 0) {
		conn->state = STATE_REQUEST_RECEIVED;
		dlog(LOG_INFO, "Received message: %s\n", conn->recv_buffer);
		return;
	}

	conn->recv_len += bytes_read;

	if (strcmp(conn->recv_buffer + conn->recv_len - 4, "\r\n\r\n") == 0) {
		conn->state = STATE_REQUEST_RECEIVED;
		dlog(LOG_INFO, "Received message: %s\n", conn->recv_buffer);
	}
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	conn->fd = open(conn->filename, O_RDONLY);
	DIE(conn->fd < 0, "open");

	struct stat st;
	int rc = fstat(conn->fd, &st);
	DIE(rc < 0, "fstat");

	conn->file_size = st.st_size;
	conn->file_pos = 0;

	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};

	int nparsed = http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, BUFSIZ);
	DIE(nparsed < 0, "http_parser_execute");

	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	int bytes_send = sendfile(conn->sockfd, conn->fd, &conn->file_pos, conn->file_size - conn->file_pos);
	DIE(bytes_send < 0, "sendfile");

	if (bytes_send == 0) {
		conn->state = STATE_DATA_SENT;
		return STATE_SENDING_HEADER;
	}

	return STATE_SENDING_DATA;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	dlog(LOG_INFO, "Send pos: %lu\nSend buffer: %s\n", conn->send_pos, conn->send_buffer);
	int bytes_sent = send(conn->sockfd, conn->send_buffer + conn->send_pos, conn->send_len - conn->send_pos, 0);
	DIE(bytes_sent < 0, "send");
	dlog(LOG_INFO, "Bytes sent: %d\n", bytes_sent);

	if (bytes_sent == 0) {
		conn->state = STATE_DATA_SENT;
		dlog(LOG_INFO, "Data sent\n");
		return 0;
	}

	conn->send_pos += bytes_sent;

	return bytes_sent;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	return 0;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */
	int rc;

	switch (conn->state) {
	case STATE_RECEIVING_DATA:
 		receive_data(conn);
		if (conn->state == STATE_REQUEST_RECEIVED)
			handle_input(conn);
		break;
	case STATE_REQUEST_RECEIVED:
		parse_header(conn);
		conn->state = STATE_SENDING_DATA;

		char buf[BUFSIZ / 2];
		strncpy(buf, conn->request_path, BUFSIZ / 2);
		snprintf(conn->filename, BUFSIZ, "./_test%s", buf);

		rc = access(conn->filename, R_OK);

		if (rc < 0) {
			conn->state = STATE_SENDING_404;
			connection_prepare_send_404(conn);
			dlog(LOG_INFO, "Requested file not found: %s\n", conn->filename);
		} else {
			dlog(LOG_INFO, "Requested file: %s\n", conn->filename);
			conn->res_type = connection_get_resource_type(conn);

			if (conn->res_type == RESOURCE_TYPE_STATIC) {
				connection_open_file(conn);
				connection_prepare_send_reply_header(conn);
				conn->state = STATE_SENDING_HEADER;
			} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
				connection_start_async_io(conn);
				conn->state = STATE_ASYNC_ONGOING;
			}
		}
		w_epoll_update_ptr_out(epollfd, conn->sockfd, conn);
		break;
	default:
		printf("shouldn't get here %d\n", conn->state);
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	switch (conn->state) {
	case STATE_SENDING_HEADER:
		if (connection_send_data(conn) == 0)
			conn->state = STATE_SENDING_DATA;
		break;
	case STATE_SENDING_DATA:
		if (conn->res_type == RESOURCE_TYPE_STATIC) {
			connection_send_static(conn);
		} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
			connection_send_dynamic(conn);
		}
		break;
	case STATE_SENDING_404:
		connection_send_data(conn);
		break;
	case STATE_ASYNC_ONGOING:
		break;
	case STATE_DATA_SENT:
		connection_remove(conn);
		break;
	default:
		// ERR("Unexpected state\n");
		dlog(LOG_INFO, "Unexpected state\n");
		exit(1);
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (event & EPOLLIN)
		handle_input(conn);

	if (event & EPOLLOUT)
		handle_output(conn);

	if (conn->state == STATE_CONNECTION_CLOSED) {
		w_epoll_remove_fd(epollfd, conn->sockfd);
	}
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */
	// memset(&ctx, 0, sizeof(ctx));
	// rc = io_setup(128, &ctx);
	// DIE(rc < 0, "io_setup");

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");

	/* TODO: Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");

		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.data.fd == listenfd) {
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			handle_client(rev.events, rev.data.ptr);
		}
	}

	return 0;
}
