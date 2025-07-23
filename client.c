#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h> // For non-blocking sockets
#include <errno.h> // For non-blocking error checks
#include <time.h>  // For time tracking

#define PORT 8080
#define SERVER_IP "127.0.0.1" // Localhost
#define NUM_THREADS 100
#define DURATION_SECONDS 100

// Global variable to signal threads to stop
volatile int keep_running = 1;

void *client_thread_func(void *thread_id_ptr) {
    long thread_id = (long)thread_id_ptr;
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    const char *message = "Hello";
    struct timespec start_time, current_time;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client: Socket creation error");
        pthread_exit(NULL);
    }

    // Set socket to non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Client: Invalid address/ Address not supported");
        close(sock);
        pthread_exit(NULL);
    }

    // Connect to server (non-blocking)
    int connect_status = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (connect_status < 0) {
        if (errno == EINPROGRESS) {
            // Connection in progress, wait for it to complete
            fd_set write_fds;
            struct timeval tv;

            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);
            tv.tv_sec = 5; // 5 second timeout for connection
            tv.tv_usec = 0;

            if (select(sock + 1, NULL, &write_fds, NULL, &tv) == 0) {
                printf("Client Thread %ld: Connection timed out.\n", thread_id);
                close(sock);
                pthread_exit(NULL);
            } else {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    fprintf(stderr, "Client Thread %ld: Connection error: %s\n", thread_id, strerror(so_error));
                    close(sock);
                    pthread_exit(NULL);
                }
            }
        } else {
            perror("Client: Connection failed");
            close(sock);
            pthread_exit(NULL);
        }
    }

    printf("Client Thread %ld: Connected to server.\n", thread_id);

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (keep_running) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if ((current_time.tv_sec - start_time.tv_sec) >= DURATION_SECONDS) {
            break; // Time limit reached for this thread
        }

        // Send "Hello" (non-blocking)
        ssize_t send_status = send(sock, message, strlen(message), 0);
        if (send_status < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Buffer full, try again later
                usleep(10000); // Sleep for 10ms
            } else {
                perror("Client: Send failed");
                break;
            }
        } else {
            // printf("Client Thread %ld: Sent: %s\n", thread_id, message);
        }

        // Receive "Hello" (non-blocking)
        memset(buffer, 0, sizeof(buffer));
        ssize_t valread = recv(sock, buffer, 1024, 0);
        if (valread > 0) {
            // printf("Client Thread %ld: Received: %s\n", thread_id, buffer);
        } else if (valread == 0) {
            printf("Client Thread %ld: Server disconnected.\n", thread_id);
            break;
        } else { // valread < 0
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No data available, continue
                usleep(10000); // Sleep for 10ms to prevent busy-waiting
            } else {
                perror("Client: Recv failed");
                break;
            }
        }

        usleep(100000); // Small delay to avoid hammering the server
    }

    printf("Client Thread %ld: Closing connection after %d seconds.\n", thread_id, DURATION_SECONDS);
    close(sock);
    pthread_exit(NULL);
}

int main() {
    pthread_t client_threads[NUM_THREADS];
    long i;

    printf("Client: Starting %d threads for %d seconds...\n", NUM_THREADS, DURATION_SECONDS);

    for (i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&client_threads[i], NULL, client_thread_func, (void *)i) < 0) {
            perror("Client: Could not create thread");
            return 1;
        }
    }

    // Let the threads run for the specified duration
    sleep(DURATION_SECONDS + 5); // Give a bit more time for threads to finish their last operations

    printf("Client: Signalling threads to stop.\n");
    keep_running = 0; // Signal all threads to stop

    // Join threads to ensure they finish and clean up resources
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(client_threads[i], NULL);
    }

    printf("Client: All client threads finished. Exiting.\n");
    return 0;
}
