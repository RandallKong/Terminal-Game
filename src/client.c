#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

static void           parse_arguments(int argc, char *argv[], char **address, char **port_str);
static void           handle_arguments(const char *binary_name, const char *address, const char *port_str, in_port_t *port);
static in_port_t      parse_in_port_t(const char *binary_name, const char *port_str);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void           convert_address(const char *address, struct sockaddr_storage *addr, socklen_t *addr_len);
static int            socket_create(int domain, int type, int protocol);
static void           get_address_to_server(struct sockaddr_storage *addr, in_port_t port);
static void           socket_close(int sockfd);

static void send_init_message(int sockfd, const struct sockaddr *addr, socklen_t addr_len);
static void handle_init_message(const char *message);

static void handle_input(int sockfd, struct sockaddr *addr, socklen_t addr_len);
static void read_from_keyboard(int sockfd, const struct sockaddr *addr, socklen_t addr_len);
void        enableRawMode(void);

static void send_quit_message(int sockfd, const struct sockaddr *addr, socklen_t addr_len);

void handle_position_change(char *message);

static void setup_signal_handler(void);
static void sigint_handler(int signum);

// static int redirect_output_to_file(const char *file_path);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

// #define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BUFFER_SIZE 1024
#define BASE_TEN 10
#define INIT_MESSAGE_PREFIX_LEN 5

typedef struct
{
    int height;
    int width;
} WindowDimensions;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
WindowDimensions window;

int main(int argc, char *argv[])
{
    char                   *address;
    char                   *port_str;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;
    fd_set                  read_fds;

    socklen_t addr_len = sizeof(addr);

    address  = NULL;
    port_str = NULL;
    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);
    convert_address(address, &addr, &addr_len);
    sockfd = socket_create(addr.ss_family, SOCK_DGRAM, 0);
    get_address_to_server(&addr, port);

    setup_signal_handler();

    //    if(redirect_output_to_file("output") != 0)
    //    {
    //        return 1;    // Exit if redirection fails
    //    }

    enableRawMode();

    send_init_message(sockfd, (const struct sockaddr *)&addr, addr_len);

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    while(!exit_flag)
    {
        fd_set tmp_fds = read_fds;

        // Wait for activity on the socket or stdin
        if(select(sockfd + 1, &tmp_fds, NULL, NULL, NULL) == -1)
        {
            //            perror("select");
            break;
        }

        // Check for activity on the socket
        if(FD_ISSET(sockfd, &tmp_fds))
        {
            handle_input(sockfd, (struct sockaddr *)&addr, addr_len);
        }

        // Check for activity on stdin
        if(FD_ISSET(STDIN_FILENO, &tmp_fds))
        {
            read_from_keyboard(sockfd, (struct sockaddr *)&addr, addr_len);
        }
    }

    send_quit_message(sockfd, (struct sockaddr *)&addr, addr_len);
    // SEND QUIT TO SERVER

    socket_close(sockfd);

    return EXIT_SUCCESS;
}

// static int redirect_output_to_file(const char *file_path)
//{
//     FILE *file;
//     int   client_id = 1;    // Starting client ID
//     bool  file_exists;
//
//     char file_name[BUFFER_SIZE];
//     do
//     {
//         // Construct file name with client ID
//         snprintf(file_name, sizeof(file_name), "%s%d.txt", file_path, client_id);
//
//         // Check if file exists
//         file = fopen(file_name, "re");
//         if(file != NULL)
//         {
//             fclose(file);
//             client_id++;    // Increment client ID if file exists
//             file_exists = true;
//         }
//         else
//         {
//             file_exists = false;
//         }
//     } while(file_exists);
//
//     // Open the file for appending
//     file = fopen(file_name, "we");
//     if(file == NULL)
//     {
//         // Handle error if unable to create file
//         perror("Error creating file");
//         return 1;    // Return non-zero to indicate failure
//     }
//     fclose(file);
//
//     // Redirect stdout to file
//     if(freopen(file_name, "w", stdout) == NULL)
//     {
//         perror("Error redirecting stdout");
//         return 1;    // Return non-zero to indicate failure
//     }
//
//     // Redirect stderr to file
//     if(freopen(file_name, "a", stderr) == NULL)
//     {
//         perror("Error redirecting stderr");
//         return 1;    // Return non-zero to indicate failure
//     }
//
//     return 0;    // Return zero to indicate success
// }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

#pragma GCC diagnostic pop

static void setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void send_init_message(int sockfd, const struct sockaddr *addr, socklen_t addr_len)
{
    const char *init_message = "INIT";
    ssize_t     bytes_sent;

    // Send the "INIT" message over the socket
    bytes_sent = sendto(sockfd, init_message, strlen(init_message), 0, addr, addr_len);

    if(bytes_sent == -1)
    {
        perror("sendto");
        exit(EXIT_FAILURE);
    }

    printf("Sent INIT message\n");
}

static void handle_init_message(const char *message)
{
    char    *endptr;
    long int width;
    long int height;

    // Extract username
    char username[BUFFER_SIZE];
    int  i;
    for(i = INIT_MESSAGE_PREFIX_LEN; message[i] != '|' && message[i] != '\0'; ++i)
    {
        username[i - INIT_MESSAGE_PREFIX_LEN] = message[i];
    }
    username[i - INIT_MESSAGE_PREFIX_LEN] = '\0';    // Null-terminate the username

    // Parse the INIT message to extract width and height
    height = strtol(message + i + 1, &endptr, BASE_TEN);
    if(*endptr != '|')
    {
        fprintf(stderr, "Invalid INIT message format\n");
        return;
    }
    width = strtol(endptr + 1, &endptr, BASE_TEN);
    if(*endptr != '\0')
    {
        fprintf(stderr, "Invalid INIT message format\n");
        return;
    }

    // Check for conversion errors
    if((height == LONG_MIN || height == LONG_MAX || width == LONG_MIN || width == LONG_MAX) && errno == ERANGE)
    {
        fprintf(stderr, "Conversion error: out of range\n");
        return;
    }

    // Check if the values are within the range of int
    if(height < INT_MIN || height > INT_MAX || width < INT_MIN || width > INT_MAX)
    {
        fprintf(stderr, "Conversion error: out of range for int\n");
        return;
    }

    // Set the width and height here as needed
    window.height = (int)height;
    window.width  = (int)width;
    printf("Im %s. Setting width: %d, height: %d\n", username, window.width, window.height);
    // Assuming you have variables to store width and height in the client code
    // Update those variables accordingly
}

static void handle_input(int sockfd, struct sockaddr *addr, socklen_t addr_len)
{
    char    input_buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Receive message from the server
    bytes_received = recvfrom(sockfd, input_buffer, sizeof(input_buffer), 0, addr, &addr_len);

    if(bytes_received == -1)
    {
        perror("recvfrom");
        exit(EXIT_FAILURE);
    }
    else if(bytes_received == 0)
    {
        printf("Server closed connection\n");
        exit(EXIT_SUCCESS);
    }
    else
    {
        input_buffer[bytes_received] = '\0';

        if(strcmp(input_buffer, "QUIT") == 0)
        {
            printf("Received QUIT message. Exiting...\n");
            exit_flag = 1;
            return;    // Exit the function immediately after setting the exit_flag
        }

        // Check if the received message is an "INIT" message
        if(strncmp(input_buffer, "INIT:", INIT_MESSAGE_PREFIX_LEN) == 0)
        {
            // Call a function to handle the INIT message
            handle_init_message(input_buffer);
        }
        else
        {
            printf("Received %zu bytes: \"%s\"\n", (size_t)bytes_received, input_buffer);
            handle_position_change(input_buffer);
        }
    }
}

void handle_position_change(char *message)
{
    char *token;
    char *rest;

    // Tokenize the message using strtok_r and print client information
    token = strtok_r(message, "()", &rest);
    while(token != NULL)
    {
        char username[BUFFER_SIZE];
        int  x = 0;
        int  y = 0;

        // Extract username, x-coordinate, and y-coordinate using sscanf with field width limits
        // NOLINTNEXTLINE(cert-err34-c,-warnings-as-errors)
        if(sscanf(token, "%99[^,], %d, %d", username, &x, &y) == 3)
        {
            printf("%s -> (%d, %d)\n", username, x, y);

            // TODO: DO LOGIC HERE TO DISPLAY.
        }

        token = strtok_r(NULL, "()", &rest);    // Get next token
    }
}

// Function to set terminal to raw mode
void enableRawMode(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void read_from_keyboard(int sockfd, const struct sockaddr *addr, socklen_t addr_len)
{
    char    c;
    ssize_t bytes_sent;
    char    key_pressed[BUFFER_SIZE];
    read(STDIN_FILENO, &c, 1);

    if(c == '\x1b')
    {    // Check if the first byte is the escape character
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1)
        {
            return;
        }
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
        {
            return;
        }

        if(seq[0] == '[')
        {
            switch(seq[1])
            {
                case 'A':
                    sprintf(key_pressed, "Up");
                    printf("Up Arrow Key pressed\n");
                    break;
                case 'B':
                    sprintf(key_pressed, "Down");
                    printf("Down Arrow Key pressed\n");
                    break;
                case 'C':
                    sprintf(key_pressed, "Right");
                    printf("Right Arrow Key pressed\n");
                    break;
                case 'D':
                    sprintf(key_pressed, "Left");
                    printf("Left Arrow Key pressed\n");
                    break;

                default:
                    printf("invalid key pressed");
            }
        }
    }
    else
    {
        sprintf(key_pressed, "Key pressed: %c", c);
//        printf("Key pressed: %c\n", c);
        return;
    }
    fflush(stdout);

    bytes_sent = sendto(sockfd, key_pressed, strlen(key_pressed), 0, addr, addr_len);

    if(bytes_sent == -1)
    {
        perror("sendto");
        exit(EXIT_FAILURE);
    }
}

static void send_quit_message(int sockfd, const struct sockaddr *addr, socklen_t addr_len)
{
    const char *quit_message = "QUIT";
    ssize_t     bytes_sent;

    // Send the "QUIT" message over the socket
    bytes_sent = sendto(sockfd, quit_message, strlen(quit_message), 0, addr, addr_len);

    if(bytes_sent == -1)
    {
        perror("sendto");
        exit(EXIT_FAILURE);
    }

    //    printf("Sent QUIT message\n");
}

static void parse_arguments(int argc, char *argv[], char **address, char **port_str)
{
    // Parse command line arguments
    if(argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_address> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    *address  = argv[1];
    *port_str = argv[2];
}

static void handle_arguments(const char *binary_name, const char *address, const char *port_str, in_port_t *port)
{
    if(address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <address> <port> <message>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    exit(exit_code);
}

static void convert_address(const char *address, struct sockaddr_storage *addr, socklen_t *addr_len)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
        *addr_len       = sizeof(struct sockaddr_in);
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
        *addr_len       = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void get_address_to_server(struct sockaddr_storage *addr, in_port_t port)
{
    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr             = (struct sockaddr_in *)addr;
        ipv4_addr->sin_family = AF_INET;
        ipv4_addr->sin_port   = htons(port);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr              = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_family = AF_INET6;
        ipv6_addr->sin6_port   = htons(port);
    }
}

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}
