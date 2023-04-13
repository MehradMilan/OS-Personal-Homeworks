#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

#define PATH_LENGTH 4096
#define BUFFER_SIZE 16384
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

typedef struct proxy_state {
  int src_fd;
  int dst_fd;
  pthread_cond_t *cond;
  int is_alive;
} proxy_state;

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 * 
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sesnsitive to time-out errors.
 */
void serve_file(int fd, char *path) {

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));

  struct stat sb;
  stat(path, &sb);
  long c_size = sb.st_size;
  char *content_length = malloc(128 * sizeof(char));
  snprintf(content_length, 128, "%ld", c_size);
  http_send_header(fd, "Content-Length", content_length); // Change this too
  http_end_headers(fd);

  int file_fd = open(path, O_RDONLY);
  if (file_fd != -1) {
    void *buffer = malloc(BUFFER_SIZE * sizeof(char));
    size_t size;
    while((size = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
       http_send_data(fd, buffer, size);
    }
    free(buffer);
    free(content_length);
    close(file_fd);
  }

}


/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's responsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 *
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sensitive to time-out errors.
 */


void serve_directory(int fd, char *path) {
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
  http_end_headers(fd);

  DIR *dir = opendir(path);
  struct dirent *dirent;
  if(dir) {
    while((dirent = readdir(dir)) != NULL) {
      int message_size = strlen("<a href=./></a><br>\n") + strlen(dirent->d_name)*2 + 1;
      char *message = malloc(message_size * sizeof(char));
      snprintf(message, message_size, "<a href='./%s'>%s</a><br>\n", dirent->d_name, dirent->d_name);
      http_send_string(fd, message);
      free(message);
    }
  }
  free(dir);

}

char *check_directory_contain_file(char *path, struct stat sb) {
  char *file_path = malloc(PATH_LENGTH);
  strcpy(file_path, path);
  strcat(file_path, "/index.html");
  if(!stat(file_path, &sb))
    return file_path;
  else return NULL;
}


/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 * 
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {

  struct http_request *request = http_request_parse(fd);

  if (request == NULL || request->path[0] != '/') {
    http_start_response(fd, 400);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  if (strstr(request->path, "..") != NULL) {
    http_start_response(fd, 403);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    close(fd);
    return;
  }

  /* Remove beginning `./` */
  char *path = malloc(2 + strlen(server_files_directory) + strlen(request->path) + 1);
  path[0] = '.';
  path[1] = '/';
  memcpy(path + 2, server_files_directory, strlen(server_files_directory) + 1);
  memcpy(path + 2 + strlen(server_files_directory), request->path, strlen(request->path) + 1);

  struct stat sb;
  int state = stat(path, &sb);
  if(!state) {
    if(S_ISREG(sb.st_mode)) {
      serve_file(fd, path);
    } else if(S_ISDIR(sb.st_mode)) {
      char *file_path = check_directory_contain_file(path, sb);
      if(file_path != NULL) {
        serve_file(fd, file_path);
      } else {
        serve_directory(fd, path);
      }
    }
  } else {
    http_start_response(fd, 404);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
  }

  /* 
   * TODO: First is to serve files. If the file given by `path` exists,
   * call serve_file() on it. Else, serve a 404 Not Found error below.
   *
   * TODO: Second is to serve both files and directories. You will need to
   * determine when to call serve_file() or serve_directory() depending
   * on `path`.
   *  
   * Feel FREE to delete/modify anything on this function.
   */
  
}

void *serve_proxy(void *p_state) {
  proxy_state *state = (proxy_state *) p_state;
  void *buff = malloc(BUFFER_SIZE);
  size_t size;
  while ((size = read(state->src_fd, buff, BUFFER_SIZE)) > 0) {
    http_send_data(state->dst_fd, buff, size);
  }
  free(buff);
  state->is_alive = 0;
  pthread_cond_signal(state->cond);
  return NULL;
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */

void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int target_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (target_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    close(fd);
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    close(target_fd);
    close(fd);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(target_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    close(target_fd);
    close(fd);
    return;

  }

  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

  pthread_t proxy_threads[2];

  proxy_state *request = malloc(sizeof(proxy_state));
  request->src_fd = fd;
  request->dst_fd = target_fd;
  request->cond = &cond;
  request->is_alive = 1;
  pthread_create(&proxy_threads[0], NULL, serve_proxy, request);

  proxy_state *response = malloc(sizeof(proxy_state));
  response->src_fd = fd;
  response->dst_fd = target_fd;
  response->cond = &cond;
  response->is_alive = 1;
  pthread_create(&proxy_threads[1], NULL, serve_proxy, response);

  while (request->is_alive && response->is_alive) {
    pthread_cond_wait(&cond, &mutex);
  }

  pthread_cancel(proxy_threads[0]);
  pthread_cancel(proxy_threads[1]);

  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond);

  free(request);
  free(response);

  close(target_fd);
  close(fd);
  return;

  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
}

void *thread_start(void *handler) {
  void (*request_handler)(int) = handler;
  while(1) {
    int fd = wq_pop(&work_queue);
    request_handler(fd);
    close(fd);
  }
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
  wq_init(&work_queue);
  pthread_t thread_pool[num_threads];
  for (int i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, thread_start, request_handler);
  }
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    if (num_threads > 0) {
      wq_push(&work_queue, client_socket_number);
    } else {
      request_handler(client_socket_number);
      close(client_socket_number);
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);
  signal(SIGPIPE, SIG_IGN);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
