#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024
#define TIMESTAMP_INTERVAL 10

int server_fd = -1;
int is_daemon = 0;
pthread_mutex_t file_mutex;

// Struct for linked list of threads
struct thread_node {
    pthread_t thread_id;
    struct thread_node *next;
};

struct thread_node *head = NULL;

// Signal handler for SIGINT and SIGTERM
void handle_signal(int signal) {
    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    // Signal all threads to exit by closing the server socket
    while (head != NULL) {
        pthread_join(head->thread_id, NULL);
        struct thread_node *temp = head;
        head = head->next;
        free(temp);
    }
    unlink(FILE_PATH);  // Delete the file
    closelog();
    exit(0);
}

void setup_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// Daemonize the process
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Failed to fork process: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
}

// Thread function to handle each client connection
void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &addr_len);
    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Open file for appending
    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';

        // Write data to file
        pthread_mutex_lock(&file_mutex);
        int file_fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0666);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        write(file_fd, buffer, bytes_received);
        close(file_fd);
        pthread_mutex_unlock(&file_mutex);

        // Check for newline to send data back to client
        if (strchr(buffer, '\n') != NULL) {
            pthread_mutex_lock(&file_mutex);
            file_fd = open(FILE_PATH, O_RDONLY);
            if (file_fd == -1) {
                syslog(LOG_ERR, "Failed to read file: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                break;
            }
            while ((bytes_received = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
                send(client_fd, buffer, bytes_received, 0);
            }
            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
        }
    }

    if (bytes_received == -1) {
        syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
    }

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    close(client_fd);
    return NULL;
}

// Append new thread to linked list
void add_thread(pthread_t thread_id) {
    struct thread_node *new_node = malloc(sizeof(struct thread_node));
    new_node->thread_id = thread_id;
    new_node->next = head;
    head = new_node;
}

// Thread function for appending timestamps
void *timestamp_thread(void *arg) {
    while (1) {
        sleep(TIMESTAMP_INTERVAL);

        time_t now = time(NULL);
        struct tm *time_info = localtime(&now);
        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S\n", time_info);

        pthread_mutex_lock(&file_mutex);
        int file_fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0666);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file for timestamp: %s", strerror(errno));
        } else {
            write(file_fd, timestamp, strlen(timestamp));
            close(file_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        is_daemon = 1;
    }

    setup_signal_handler();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (is_daemon) {
        daemonize();
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Initialize the mutex
    pthread_mutex_init(&file_mutex, NULL);

    // Start the timestamp thread
    pthread_t ts_thread;
    if (pthread_create(&ts_thread, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
        return -1;
    }

    while (1) {
        int *client_fd_ptr = malloc(sizeof(int));
        if ((*client_fd_ptr = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            free(client_fd_ptr);
            continue;
        }

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_fd_ptr) != 0) {
            syslog(LOG_ERR, "Failed to create thread: %s", strerror(errno));
            close(*client_fd_ptr);
            free(client_fd_ptr);
            continue;
        }
        add_thread(client_thread);
    }

    pthread_mutex_destroy(&file_mutex);
    handle_signal(SIGTERM);
    return 0;
}
