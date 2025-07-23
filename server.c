#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h> // For non-blocking sockets
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define DURATION_SECONDS 30
static volatile struct timespec last_rec_time;

void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];
    int read_size;

    // Set socket to non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    printf("Server: New client connected (socket %d).\n", sock);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        read_size = recv(sock, buffer, BUFFER_SIZE, 0);

        if (read_size > 0) {
	    clock_gettime(CLOCK_MONOTONIC, &last_rec_time);	
            printf("Server: Received from client %d: %s\n", sock, buffer);
            if (strcmp(buffer, "Hello") == 0) {
                const char *response = "Hello";
                send(sock, response, strlen(response), 0);
                printf("Server: Sent to client %d: %s\n", sock, response);
            }
        } else if (read_size == 0) {
            printf("Server: Client %d disconnected.\n", sock);
            break;
        } else { // read_size < 0
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No data available, continue
                usleep(10000); // Sleep for 10ms to prevent busy-waiting
            } else {
                perror("Server: recv failed");
                break;
            }
        }
    }

    close(sock);
    free(socket_desc);
    pthread_exit(NULL);
}

int main() {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;
    struct timespec current_time;

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        perror("Server: Could not create socket");
        return 1;
    }
    printf("Server: Socket created.\n");

    // Set socket to non-blocking for the listening socket as well
    fcntl(socket_desc, F_SETFL, O_NONBLOCK);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Server: Bind failed");
        return 1;
    }
    printf("Server: Bind done.\n");

    clock_gettime(CLOCK_MONOTONIC, &last_rec_time);
    listen(socket_desc, 101);
    printf("Server: Waiting for incoming connections on port %d...\n", PORT);

    c = sizeof(struct sockaddr_in);
    while (1) {
        client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
        if (client_sock < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No incoming connections, continue
                usleep(100000); // Sleep for 100ms
		clock_gettime(CLOCK_MONOTONIC, &current_time);
		if((current_time.tv_sec - last_rec_time.tv_sec) >= DURATION_SECONDS)
			break;
                continue;
            } else {
                perror("Server: accept failed");
                break;
            }
        }

        printf("Server: Connection accepted, client socket: %d\n", client_sock);

        pthread_t client_thread;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        if (pthread_create(&client_thread, NULL, handle_client, (void*) new_sock) < 0) {
            perror("Server: Could not create thread");
            return 1;
        }
	clock_gettime(CLOCK_MONOTONIC, &current_time);

        pthread_detach(client_thread); // Detach thread so resources are freed automatically
    }

    printf("Server: Shutting down.\n");
    close(socket_desc);
    return 0;
}
