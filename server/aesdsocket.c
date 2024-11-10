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

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
int file_fd = -1;
int is_daemon = 0;

// Signal handler for SIGINT and SIGTERM
void handle_signal(int signal)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    if (file_fd != -1) close(file_fd);
    unlink(FILE_PATH); // Delete the file
    closelog();
    exit(0);
}

void setup_signal_handler()
{
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Failed to fork process: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);  // Exit the parent process
    }

    // Set up the daemon
    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Redirect standard file descriptors to /dev/null
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDWR);
    dup(0);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received, bytes_written;
    
    // Open syslog for logging
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // Check if -d argument is provided for daemon mode
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        is_daemon = 1;
    }

    setup_signal_handler();

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Bind socket to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // If -d flag is set, daemonize the process
    if (is_daemon) {
        daemonize();
    }

    // Listen for incoming connections
    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Open file for appending
    if ((file_fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0666)) == -1) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (1) {
        // Accept connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Receive data from client
        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';

            // Write data to file
            bytes_written = write(file_fd, buffer, bytes_received);
            if (bytes_written == -1) {
                syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
                break;
            }

            // Check if newline is received, send file contents back to client
            if (strchr(buffer, '\n') != NULL) {
                // Seek to start and read entire file to send back to client
                lseek(file_fd, 0, SEEK_SET);
                while ((bytes_written = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
                    send(client_fd, buffer, bytes_written, 0);
                }
                // Return to append mode
                lseek(file_fd, 0, SEEK_END);
            }
        }

        if (bytes_received == -1) {
            syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
        }

        // Log disconnection and close client socket
        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(client_fd);
        client_fd = -1;
    }

    // Clean up and close file and sockets
    handle_signal(SIGTERM);
    return 0;
}
