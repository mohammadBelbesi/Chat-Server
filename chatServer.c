/**Name:Mohammad Belbesi**/
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <string.h>
#include "chatServer.h"

#define success 0
#define fail (-1)
int main_fd; /* socket descriptor */

conn_t * find_connection(conn_t * head, int sd);
static int end_server = 0;

/** use a flag to end_server to break the main loop **/
void intHandler(int SIG_INT) {
    end_server = 1;
    if (SIG_INT == SIGINT) {
        return;
    }
}

int main(int argc, char * argv[]) {

    signal(SIGINT, intHandler); //flag to end_server to break the main loop
    char * endptr;
    long num;
    int base = 10;
    if (argc != 2) {
        printf("Usage: chatServer <port>\n");
        //printf("Usage: server <port>\n");
        exit(EXIT_SUCCESS);
    }
    num = strtol(argv[1], & endptr, base);
    if(endptr == argv[1] || num < 1 || num > 65536){
        printf("Usage: chatServer <port>\n");
        exit(EXIT_SUCCESS);
    }
    conn_pool_t * pool = calloc(1, sizeof(conn_pool_t));
    if (pool == NULL) {
        fprintf(stderr, "message_content allocating memory is filed!\n");
        return EXIT_FAILURE;
    }
    init_pool(pool);

    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/

    ////////////////** set the server on the internet line **////////////////
    int long port = num;
    int newfd; /* returned by accept() */
    struct sockaddr_in srv; /* used by bind() */
    srv.sin_family = AF_INET; /* use the Internet addr family */
    srv.sin_addr.s_addr = htonl(INADDR_ANY); /* bind: a client may connect to any of my addresses */
    srv.sin_port = htons(port); /* bind socket ‘fd’ to port 80*/
    struct sockaddr_in cli; /* used by accept() */
    unsigned int cli_len = sizeof(cli); /* used by accept() */
    /** (1)create the socket **/
    if ((main_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket Error\n");
        free(pool);
        exit(EXIT_FAILURE);
    }

    /*************************************************************/
    /* Set socket to be nonblocking. All the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/

    int on = 1;
    int rc = ioctl(main_fd, (int) FIONBIO, (char * ) & on);
    if (rc < 0) {
        perror("error: ioctl\n");
        free(pool);
        exit(EXIT_FAILURE);
    }

    /** (2)bind the socket to a port **/
    if (bind(main_fd, (struct sockaddr * ) & srv, sizeof(srv)) < 0) {

        perror("bind Error\n");
        free(pool);
        close(main_fd);
        exit(EXIT_FAILURE);
    }
    /** (3) listen on the socket **/
    if (listen(main_fd, 5) < 0) {

        perror("listen Error\n");
        free(pool);
        close(main_fd);
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/
    pool -> maxfd = main_fd;
    FD_SET(main_fd, & pool -> read_set);
    char buffer[5000];
    memset(buffer, '\0', strlen(buffer));
    int nbytes;
    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
    do {
        FD_ZERO( & pool -> ready_write_set);
        FD_ZERO( & pool -> ready_read_set);
        pool -> ready_write_set = pool -> write_set;
        pool -> ready_read_set = pool -> read_set;
        pool -> maxfd = main_fd;
        conn_t * connection = pool -> conn_head;
        for (; connection != NULL; connection = connection -> next) {
            pool -> maxfd = (connection -> fd > pool -> maxfd) ? connection -> fd : pool -> maxfd;
        }
        printf("Waiting on select()...\nMaxFd %d\n", pool -> maxfd);

        int fds = pool -> maxfd + 1;
        int result = select(fds, & pool -> ready_read_set, & pool -> ready_write_set, NULL, NULL);
        if (result == fail) {
            break;
        } else {
            pool -> nready = result;
        }
        for (int i = 0; i < ((pool -> maxfd) + 1) && (pool -> nready) != 0; i++){ /* check all descriptors, stop when checked all valid fds */
            if (FD_ISSET(i, & pool -> ready_read_set)) {
                if (i == main_fd) {
                    newfd = accept(i, (struct sockaddr * ) & cli, & cli_len);
                    (pool -> nready) --;
                    add_conn(newfd, pool);
                } else {
                    printf("Descriptor %d is readable\n", i);
                    memset(buffer, '\0', strlen(buffer));
                    nbytes = (int) read(i, buffer, BUFFER_SIZE);
                    printf("%d bytes received from sd %d\n", (int) strlen(buffer), i);
                    (pool -> nready) --;
                    if (nbytes == 0) {
                        printf("Connection closed for sd %d\n", i);
                        remove_conn(i, pool);
                    }
                    add_msg(i, buffer, (int) strlen(buffer), pool);
                }
            }

            if (FD_ISSET(i, & pool -> ready_write_set)) {
                write_to_client(i, pool);
                (pool -> nready)--;
            }

        }
    } while (end_server == 0);

    conn_t * conn_to_free = pool -> conn_head, * prev_conn_to_free;
    for (int i = 0; i <= pool -> nr_conns && conn_to_free != NULL; i++) {
        prev_conn_to_free = conn_to_free -> next;
        printf("removing connection with sd %d \n", conn_to_free -> fd);
        remove_conn(conn_to_free -> fd, pool);
        conn_to_free = prev_conn_to_free;
    }
    FD_ZERO( & pool -> ready_write_set);
    FD_ZERO( & pool -> write_set);
    FD_ZERO( & pool -> ready_read_set);
    FD_ZERO( & pool -> read_set);
    close(main_fd);
    free(pool);
    return success;
}

/**
 * Init the conn_pool_t structure.
 * @pool - allocated pool
 * @ return value - 0 on success, -1 on failure
 */

int init_pool(conn_pool_t * pool) {
    //initialized all fields
    if (pool == NULL) {
        return fail; //return -1
    } else {
        /** initialize the pool args **/
        pool -> maxfd = 0;
        pool -> nready = 0;
        FD_ZERO( & pool -> read_set);
        FD_ZERO( & pool -> ready_read_set);
        FD_ZERO( & pool -> write_set);
        FD_ZERO( & pool -> ready_write_set);
        pool -> conn_head = NULL;
        pool -> nr_conns = 0;
    }
    return success; //return 0
}

/**
 * Add connection when new client connects the server.
 * @ sd - the socket descriptor returned from accept
 * @pool - the pool
 * @ return value - 0 on success, -1 on failure
 */

int add_conn(int sd, conn_pool_t * pool) {
    /*
     * 1. allocate connection and init fields
     * 2. add connection to pool
     */
    printf("New incoming connection on sd %d\n", sd);
    if (pool == NULL || sd < 0) {
        return fail;
    }
    conn_t * connection = (conn_t * ) malloc(sizeof(conn_t));
    if (connection == NULL) {
        fprintf(stderr, "connection allocated memory is failed!\n");
        return fail;
    }
    connection -> prev = NULL;
    connection -> next = NULL;
    connection -> fd = sd;
    connection -> write_msg_head = NULL;
    connection -> write_msg_tail = NULL;
    if (pool -> conn_head == NULL) { //if it's the first connection in the list
        pool -> conn_head = connection;
        connection -> prev = NULL;
        connection -> next = NULL;
    } else { //add to the start of the list
        connection -> next = pool -> conn_head;
        pool -> conn_head -> prev = connection;
        connection -> prev = NULL;
        pool -> conn_head = connection;
    }
    pool -> nr_conns++;
    FD_SET(sd, & pool -> read_set); //turn on the fd of the read
    if (sd > pool -> maxfd) { //update the maxfd
        pool -> maxfd = sd;
    }
    return success;
}

/**
 * Remove connection when a client closes connection, or clean memory if server stops.
 * @ sd - the socket descriptor of the connection to remove
 * @pool - the pool
 * @ return value - 0 on success, -1 on failure
 */
int remove_conn(int sd, conn_pool_t* pool) {
    /* Remove connection when a client closes connection, or clean memory if server stops.
     * 1. Remove connection from pool
     * 2. Deallocate connection
     * 3. Remove from sets
     * 4. Update max_fd if needed
     */

    // Find connection in the pool
    struct conn* curr = pool->conn_head;
    struct conn* prev = NULL;
    while (curr != NULL) {
        if (curr->fd == sd) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    // Connection not found
    if (curr == NULL) {
        return fail;
    }

    // Deallocate message memory
    struct msg* curr_msg = curr->write_msg_head;
    struct msg* prev_msg = NULL;
    while (curr_msg != NULL) {
        prev_msg = curr_msg;
        curr_msg = curr_msg->next;
        free(prev_msg->message);
        free(prev_msg);
    }

    // Remove connection from the pool
    if (prev == NULL) {
        pool->conn_head = curr->next;
    } else {
        prev->next = curr->next;
        if (curr->next != NULL) {
            curr->next->prev = prev;
        }
    }

    // Close the connection
    close(curr->fd);

    // Deallocate connection memory
    free(curr);

    // Update number of connections
    pool->nr_conns--;

    // Update max_fd if needed
    int max_fd = pool->maxfd;
    curr = pool->conn_head;
    while (curr != NULL) {
        if (curr->fd > max_fd) {
            max_fd = curr->fd;
        }
        curr = curr->next;
    }
    if (pool->conn_head != NULL) {
        pool->maxfd = max_fd;
    } else {
        pool->maxfd = main_fd;
    }
    // Remove from sets
    FD_CLR(sd, &pool->read_set);
    FD_CLR(sd, &pool->write_set);
    FD_CLR(sd, &pool->ready_write_set);
    FD_CLR(sd, &pool->ready_read_set);
    return success;
}
/**
 * Add msg to the queues of all connections (except of the origin).
 * @ sd - the socket descriptor to add this msg to the queue in its conn object
 * @ buffer - the msg to add
 * @ len - length of msg
 * @pool - the pool
 * @ return value - 0 on success, -1 on failure
 */

int add_msg(int sd, char * buffer, int len, conn_pool_t * pool) {
    /*
     * 1. add msg_t to write queue of all other connections
     * 2. set each fd to check if ready to write
     */
    if (sd < 0 || len < 0 || buffer == NULL || pool == NULL) {
        return fail;
    }
    conn_t * connection = pool -> conn_head;
    msg_t * connection_message;
    for (int i = 0; i < pool -> nr_conns; i++) {
        if (connection -> fd == sd) { //if this is the origin connection continue and don send to it
            connection = connection -> next;
            continue;
        } else { //Add msg to the queues of all the other connections
            connection_message = (msg_t * ) calloc(1, sizeof(msg_t));
            if (connection_message == NULL) {
                fprintf(stderr, "connection_message allocating memory is filed!\n");
                return fail;
            }
            connection_message -> message = (char * ) calloc((len + 1), sizeof(char));
            if (connection_message -> message == NULL) {
                fprintf(stderr, "message_content allocating memory is filed!\n");
                free(connection_message);
                return fail;
            }
            /*initialize the msg_t args and pointers*/
            connection_message -> size = len;
            connection_message -> prev = NULL;
            connection_message -> next = NULL;
            sprintf(connection_message -> message, "%s", buffer);
            if (connection -> write_msg_head == NULL) { //if it's the first message
                connection -> write_msg_head = connection_message;
                connection -> write_msg_tail = connection_message;
                connection_message -> prev = NULL;
                connection_message -> next = NULL;
            } else {
                connection -> write_msg_tail -> next = connection_message;
                connection_message -> prev = connection -> write_msg_tail;
                connection -> write_msg_tail = connection_message;
            }
            FD_SET(connection -> fd, & pool -> write_set);
            connection = connection -> next;
        }
    }
    return success;

}

/**
 * Write msg to client.
 * @ sd - the socket descriptor of the connection to write msg to
 * @pool - the pool
 * @ return value - 0 on success, -1 on failure
 */

int write_to_client(int sd, conn_pool_t * pool) {
    if (sd < 0 || pool == NULL) {
        return fail;
    }

    conn_t * connection = find_connection(pool -> conn_head, sd);
    if (connection == NULL) {
        return fail;
    }

    while (connection -> write_msg_tail != NULL) {
        msg_t * msg = connection -> write_msg_tail;
        int long nbytes = write(sd, msg -> message, msg -> size);
        if (nbytes < 0) {
            return fail;
        }

        connection -> write_msg_tail = msg -> prev;
        free(msg -> message);
        free(msg);
    }

    connection -> write_msg_head = NULL;
    FD_CLR(connection -> fd, & pool -> write_set);
    FD_CLR(connection -> fd, & pool -> ready_write_set);
    return success;
}

conn_t * find_connection(conn_t * head, int sd) {
    conn_t * curr_connection = head;
    while (curr_connection != NULL) {
        if (curr_connection -> fd == sd) {
            return curr_connection;
        }
        curr_connection = curr_connection -> next;
    }
    return NULL;
}
