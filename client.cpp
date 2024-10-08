#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>  // for close()
#include <sys/select.h>  // for select()

#define DEST_PORT "12346"  /* arbitrary, but client and server must agree */
#define BUF_SIZE 4096

using namespace std;

/*
 * Function: handleServerError
 * Purpose: This function handles the error code given to it from the server
 * Parameters: The server message that has the error code in it
*/
void handleServerError(const string &message) {
    // Check if the message starts with "ERR"
    if (message.substr(0, 3) == "ERR") {
        // Extract the error code and convert it from a string to an integer for the switch statement
        int error_code = stoi(message.substr(4, 1));  // Extracts the error code from the message

        // Print a user-readable error message based on the error code
        switch (error_code) {
            case 1:
                cerr << "Error: Username should be between 1 and 32 characters." << endl;
                break;
            case 2:
                cerr << "Error: Username contains spaces. Please remove spaces from your username." << endl;
                break;
            case 3:
                cerr << "Error: Username is already taken. Please try another username." << endl;
                break;
            case 4:
                cerr << "Error: Unknown message format. Please check your input." << endl;
                break;
            default:
                cerr << "Error: Unrecognized error code." << endl;
                break;
        }
    }
}


int main(int argc, char **argv) {
    int sockfd;
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;   // either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream socket

    // Get server address information
    if (argc != 2) {
        cout << "Usage: client <server-name>" << endl;
        exit(1);
    }

    if ((getaddrinfo(argv[1], DEST_PORT, &hints, &servinfo)) != 0) {
        cerr << "client: can't get server address" << endl;
        exit(1);
    }

    // Open a TCP socket
    if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
        cerr << "client: can't open stream socket" << endl;
        exit(1);
    }

    // Connect the socket to the server
    if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        cerr << "client: can't connect to server" << endl;
        exit(1);
    }

    freeaddrinfo(servinfo);  // Free the address info structure

    char sendline[BUF_SIZE], recvline[BUF_SIZE];
    int datalen;

    // Set up file descriptor sets for select()
    fd_set readfds;
    int maxfd = sockfd;  // Track the largest file descriptor

    while (true) {
        // Clear the fd set and add both stdin and the socket to the set
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);  // Add the server socket to the set
        FD_SET(STDIN_FILENO, &readfds);  // Add standard input (user input) to the set

        // Call select to monitor both the socket and stdin
        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            cerr << "Error with select()" << endl;
            break;
        }

        // Check if there's data from the server to read
        if (FD_ISSET(sockfd, &readfds)) {
            datalen = recv(sockfd, recvline, BUF_SIZE - 1, 0);  // Receive data from server
            if (datalen > 0) {
                recvline[datalen] = '\0';   // Null terminate the received message
                std::string message(recvline);

                // Check if it's an error message and handle it
                if (message.substr(0, 3) == "ERR") {
                    handleServerError(message);  // Decipher and print the error message
                }
                // If the message starts with MESG, it's a broadcast and print out the message and who it's from
                else if(message.substr(0, 4) == "MESG") {
                    cout << "From " << message << endl;
                }
                // If the message starts with PMSG, it's a private message and just print out the message
                else if(message.substr(0, 4) == "PMSG") {
                    cout << message << endl;  // Private message from other user
                }
                // Otherwise just print ant input out
                else{
                    cout << message << endl;
                }

            } else if (datalen == 0) {
                cout << "Server disconnected." << endl;
                break;  // Server closed the connection
            } else {
                cerr << "Error receiving data from server." << endl;
                break;
            }
        }

        // Check if the user has entered data
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (cin.getline(sendline, BUF_SIZE)) {
                // Send user input to the server
                send(sockfd, sendline, strlen(sendline), 0);
            } else {
                cerr << "Error reading input from user." << endl;
            }
        }
    }

    close(sockfd);  // Close the socket when done
    return 0;
}
