#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t terminate = 0;

struct thread_data {
    int client_fd;
    struct sockaddr_in client_addr;
    pthread_t thread_id;
    bool completed;
};

struct thread_node {
    struct thread_data *data;
    struct thread_node *next;
};

static struct thread_node *thread_list_head = NULL;
static pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_signal(int signo) {
    syslog(LOG_INFO, "Caught signal, exiting");
    terminate = 1;
}

void *connection_handler(void *arg) {
    struct thread_data *thread_data = (struct thread_data *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int file_fd = -1;

    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(thread_data->client_addr.sin_addr));

    while ((bytes_received = recv(thread_data->client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';  // Null-terminate for string handling

        pthread_mutex_lock(&file_mutex);

        if (file_fd == -1) {
            file_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0666);
            if (file_fd == -1) {
                syslog(LOG_ERR, "Error opening file %s: %s", DATA_FILE, strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break;
            }
        }

        if (write(file_fd, buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "Error writing to file %s: %s", DATA_FILE, strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            break;
        }

        pthread_mutex_unlock(&file_mutex);

        // Read back entire file content and send to the client
        pthread_mutex_lock(&file_mutex);
        lseek(file_fd, 0, SEEK_SET);
        while ((bytes_received = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
            if (send(thread_data->client_fd, buffer, bytes_received, 0) == -1) {
                syslog(LOG_ERR, "Error sending data to client: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break;
            }
        }
        pthread_mutex_unlock(&file_mutex);
    }

    if (file_fd != -1) close(file_fd);

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(thread_data->client_addr.sin_addr));
    close(thread_data->client_fd);
    thread_data->completed = true;
    return NULL;
}

void cleanup_threads() {
    pthread_mutex_lock(&thread_list_mutex);
    struct thread_node *current = thread_list_head, *prev = NULL;

    while (current) {
        if (current->data->completed) {
            pthread_join(current->data->thread_id, NULL);
            free(current->data);
            struct thread_node *to_delete = current;
            if (prev) {
                prev->next = current->next;
            } else {
                thread_list_head = current->next;
            }
            current = current->next;
            free(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }

    pthread_mutex_unlock(&thread_list_mutex);
}

int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct sigaction sa;

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error setting signal handler: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "Error setting socket options: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Error binding socket: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Error listening on socket: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (!terminate) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (terminate) break;  // Stop accepting if terminating
            syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
            continue;
        }

        struct thread_data *new_thread_data = malloc(sizeof(struct thread_data));
        if (!new_thread_data) {
            syslog(LOG_ERR, "Error allocating memory for thread data: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        new_thread_data->client_fd = client_fd;
        new_thread_data->client_addr = client_addr;
        new_thread_data->completed = false;

        pthread_mutex_lock(&thread_list_mutex);
        struct thread_node *new_node = malloc(sizeof(struct thread_node));
        if (!new_node) {
            syslog(LOG_ERR, "Error allocating memory for thread node: %s", strerror(errno));
            close(client_fd);
            free(new_thread_data);
            pthread_mutex_unlock(&thread_list_mutex);
            continue;
        }

        new_node->data = new_thread_data;
        new_node->next = thread_list_head;
        thread_list_head = new_node;
        pthread_mutex_unlock(&thread_list_mutex);

        if (pthread_create(&new_thread_data->thread_id, NULL, connection_handler, new_thread_data) != 0) {
            syslog(LOG_ERR, "Error creating thread: %s", strerror(errno));
            close(client_fd);
            pthread_mutex_lock(&thread_list_mutex);
            thread_list_head = thread_list_head->next;
            free(new_node);
            free(new_thread_data);
            pthread_mutex_unlock(&thread_list_mutex);
        }
    }

    close(server_fd);

    cleanup_threads();
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&thread_list_mutex);

    syslog(LOG_INFO, "Exiting aesdsocket");
    closelog();

    return 0;
}

