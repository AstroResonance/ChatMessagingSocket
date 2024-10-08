#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <map>

// Port, Buffer Size, and Maximum Pending connections in the server queue
#define MY_PORT   "12346" /* arbitrary, but client and server must agree */
#define BUF_SIZE  4096
#define MAX_PENDING 5

using namespace std;
// Declare global variables and mutexes to prevent concurrency
std::mutex reg_users_mutex;
//Storing the socket descriptors of currently connected clients
std::vector<int> connected_clients;
// Stores client usernames
std::vector<std::string> usernames;
// Mapping for associating usernames with their socket descriptors
std::map<int, std::string> client_usernames;

/*
 * Function: trim
 * Purpose: To trim the commands off of the input so that the server can evaluate the content in the message
 * Parameters: Message received from the client
*/
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, last - first + 1);
}


/*
 * Function: getClientAddrString
 * Purpose: To convert the socket address to a string for reading/writing purposes.
 * Parameters: The client address
*/
static std::string getClientAddrString(const struct sockaddr_storage &clientAddr) {
    char addr_str[INET6_ADDRSTRLEN]; // Supports both IPv4 and IPv6
    if (clientAddr.ss_family == AF_INET) {
        // IPv4
        struct sockaddr_in *addr = (struct sockaddr_in *)&clientAddr;
        inet_ntop(AF_INET, &(addr->sin_addr), addr_str, INET_ADDRSTRLEN);
    } else {
        // IPv6
        struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&clientAddr;
        inet_ntop(AF_INET6, &(addr->sin6_addr), addr_str, INET6_ADDRSTRLEN);
    }
    return std::string(addr_str);
}



/*
 * Function: checkUsernameInFile
 * Purpose: To check if the username is in the file (Will return true if the username is in the file)
 * Parameters: filename, and the username we are looking for
*/
static bool checkUsernameInFile(const std::string& filename, const std::string& username) {
    // Lock the Mutex
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    // Open the file for reading
    std::ifstream REG_USERS(filename);
    if (!REG_USERS.is_open()) {
        // File does not exist; no usernames are registered yet.
        return false;
    }
    //Declare the line of type string
    std::string line;
    // Read each line from the file
    while (std::getline(REG_USERS, line)) {
        std::istringstream iss(line);
        std::string existing_username;
        // Read only the username
        iss >> existing_username;

        if (existing_username == username) {
            // Username exists
            return true;
        }
    }
    // Username not found, available
    return false;
}


/*
 * Function: existing_addr
 * Purpose: To check if the client address is in the file (Will return true if the address is in the file)
 * Parameters: filename, and the client address we are looking for
*/
static bool existing_addr(const std::string& filename, const sockaddr_storage cliaddr){
    // Lock the Mutex
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    std::ifstream REG_USERS(filename);
    if (!REG_USERS.is_open()) {
        // File does not exist; no addresses are registered yet.
        return false;
    }

    // Declare the line and client address of type string
    std::string line;
    std::string client_ipaddress = getClientAddrString(cliaddr);

    // Iterate through the file and read the username AND the stored IPaddress
    while (std::getline(REG_USERS, line)) {
        std::istringstream iss(line);
        std::string username, stored_ipaddress;
        iss >> username >> stored_ipaddress;

        if (stored_ipaddress == client_ipaddress) {
            // Address exists
            return true;
        }
    }
    // Address not found, available
    return false;
}



static void regWrite(const char *username, const struct sockaddr_storage &clientAddr){

    std::lock_guard<std::mutex> lock(reg_users_mutex);
    // Create and open the Registered Users file
    ofstream REG_USERS("REGISTERED_USERS", std::ios::app);

    // Get the string representation of the client address
    std::string clientAddrStr = getClientAddrString(clientAddr);

    // Write the username and the Client's address to the file
    REG_USERS << username << " " << clientAddrStr << std::endl;

    // Close the file
    REG_USERS.close();
}


/*
 * Function: broadcastJoin
 * Purpose: This function broadcasts to every other client that someone joined
 * Parameters: The message being sent out, and the socket descriptor of the client joining
*/
void broadcastJoin(const std::string& message, int sender_sockfd) {
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    for (int client_sockfd : connected_clients) {
        // If the client ID isn't the socket ID of the person joining, then send the message to that socket descriptor
        if (client_sockfd != sender_sockfd) {
            // Send the message, but if it fails, print out an error message
            if (send(client_sockfd, message.c_str(), message.size(), 0) < 0) {
                std::cerr << "Failed to send message to client socket: " << client_sockfd << std::endl;
            }
        }
    }
}

/*
 * Function: broadcastToAll
 * Purpose: This function broadcasts to every client when a client leaves
 * Parameters: The message being sent out
*/
void broadcastToAll(const std::string& message) {
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    for (int client_sockfd : connected_clients) {
        if (send(client_sockfd, message.c_str(), message.size(), 0) < 0) {
            std::cerr << "Failed to send message to client socket: " << client_sockfd << std::endl;
        }
    }
}


/*
 * Function: broadcastMESG
 * Purpose: This function broadcasts to every client what the sender says, and doesn't send to the sender
 * Parameters: The message being sent out, the sender's socket descriptor, and the sender's username
*/
void broadcastMESG(const std::string& message, int sender_sockfd, const std::string& sender_username) {
    std::lock_guard<std::mutex> lock(reg_users_mutex);

    // Construct the message with the sender's username
    std::string full_message = sender_username + " (Public): " + message;

    // Send the message to all clients except the sender
    for (int client_sockfd : connected_clients) {
        if (client_sockfd != sender_sockfd) {
            if (send(client_sockfd, full_message.c_str(), full_message.size(), 0) < 0) {
                std::cerr << "Failed to send message to client socket: " << client_sockfd << std::endl;
            }
        }
    }
}

/*
 * Function: sendUserList
 * Purpose: This function sends the user list. Usually to a client joining/leaving
 * Parameters: The socket descriptor of the person joining/leaving
*/
void sendUserList(int sockfd){
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    std::ostringstream oss;
    size_t user_count = usernames.size();

    // Construct the message
    oss << user_count << " Connected Users:\n";
    for(const auto& user : usernames){
        oss << user << "\n";
    }
    std::string ack_message = oss.str();

    // Send the message to the client
    ssize_t bytes_sent = send(sockfd, ack_message.c_str(), ack_message.size(), 0);
    if (bytes_sent < 0) {
        std::cerr << "Failed to send ACK to client socket: " << sockfd << " Error: " << strerror(errno) << std::endl;
    }
}


/*
 * Function: registration
 * Purpose: This function registers the user and also checks the input for correct formatting
 * Parameters: The message being analyzed, the socket descriptor, and the client address
*/
static void registration(const char* mesg, int sockfd, const struct sockaddr_storage &cliaddr) {
    std::string username_string = trim(mesg + 4);

    // Check if username is non-empty after the space
    if (username_string.empty()) {
        std::string empty = "Please enter a valid username after 'REG'.\n";
        send(sockfd, empty.c_str(), empty.size(), 0);
        return;
    }

    // Validate username length and format
    size_t username_length = username_string.length();
    if (username_length < 1) {
        std::string shortUname = "Please enter a longer username\n";
        send(sockfd, shortUname.c_str(), shortUname.size(), 0);
        return;
    }
    // Username too long
    else if(username_length > 32){
        std::string lengthError = "ERR 1\n";
        send(sockfd, lengthError.c_str(), lengthError.size(), 0);
        return;
    }

    // Check for spaces in the username
    if (username_string.find(' ') != std::string::npos) {
        std::string spaceError = "ERR 2\n";
        send(sockfd, spaceError.c_str(), spaceError.size(), 0);
        return;
    }

    // Check if the username already exists in the file
    if (checkUsernameInFile("REGISTERED_USERS", username_string)) {
        std::string userExists = "ERR 3\n";
        send(sockfd, userExists.c_str(), userExists.size(), 0);
        return;  // Exit if the username already exists
    }
    // If the username exists already
    if(existing_addr("REGISTERED_USERS", cliaddr)){
        std::string addrExists = "You already have a username.\n";
        send(sockfd, addrExists.c_str(), addrExists.size(), 0);
        return;
    }

    // Write the username to the file if it's valid and doesn't exist
    regWrite(username_string.c_str(), cliaddr);
    {
        // Lock the mutex
        std::lock_guard<std::mutex> lock(reg_users_mutex);
        // Add the new username to the list
        usernames.push_back(username_string);
        // Add the new socket to the list
        connected_clients.push_back(sockfd);
        // Map the socket to the username
        client_usernames[sockfd] = username_string;
    }

    // Send ACK to the newly registered user with the list of connected users
    sendUserList(sockfd);

    // Broadcast to other users that a new user has joined
    std::string join_message = username_string + " has joined the chat.\n";
    broadcastJoin(join_message, sockfd);
}


/*
 * Function: sendPrivateMessage
 * Purpose: This function lets one client send a private message to another client
 * Parameters: The recipient's username, the message being sent, the file descriptor of the receiver, and the sender's username
*/
void sendPrivateMessage(const std::string& recipient_username, const std::string& message, int sender_sockfd, const std::string& sender_username) {
    // Lock the mutex
    std::lock_guard<std::mutex> lock(reg_users_mutex);

    // Find the socket descriptor of the recipient
    int recipient_sockfd = -1;
    for (const auto& pair : client_usernames) {
        if (pair.second == recipient_username) {
            recipient_sockfd = pair.first;
            break;
        }
    }

    if (recipient_sockfd == -1) {
        // Recipient not found, send error to sender
        std::string error_msg = "ERR 3\n";  // Error code for unknown user
        send(sender_sockfd, error_msg.c_str(), error_msg.size(), 0);
        return;
    }

    // Construct the message
    std::string full_message = "From " + sender_username + " (private): " + message;

    // Send the message to the recipient
    if (send(recipient_sockfd, full_message.c_str(), full_message.size(), 0) < 0) {
        std::cerr << "Failed to send private message to client socket: " << recipient_sockfd << std::endl;
    }
}

/*
 * Function: removeUserFromFile
 * Purpose: This function removes a client from all data structures that hold information about that client
 * Parameters: The filename, and the client's username who is leaving
*/
void removeUserFromFile(const std::string& filename, const std::string& username) {
    //Lock the mutex
    std::lock_guard<std::mutex> lock(reg_users_mutex);
    std::ifstream infile(filename);
    // If the file is open, print error message
    if (!infile.is_open()) {
        std::cerr << "Error opening the file!" << std::endl;
        return;
    }
    // Declare line data type
    std::vector<std::string> lines;
    std::string line;
    // Parse the file and remove the user
    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        std::string existing_username;
        iss >> existing_username;
        if (existing_username != username) {
            lines.push_back(line);
        }
    }
    infile.close();

    // Now write back to the file without the user's line
    std::ofstream outfile(filename, std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "Error opening the file!" << std::endl;
        return;
    }
    for (const auto& l : lines) {
        outfile << l << std::endl;
    }
    outfile.close();
}

/*
 * Function: handleClient
 * Purpose: This function is the control center for the server. This will direct every function and knows where to go
 * Parameters: The file descriptor, and the Address of the client.
*/
void handleClient(int newsockfd, struct sockaddr_storage cliaddr) {
    // Declaration of the data length, message size, and if the user exited
    int datalen;
    char mesg[BUF_SIZE];
    bool user_exited = false;

    while ((datalen = recv(newsockfd, mesg, BUF_SIZE, 0)) > 0) {
        mesg[datalen] = '\0';  // Null-terminate the message

        // Trim the message to handle whitespace-only commands
        std::string trimmed_message = trim(mesg);

        if (trimmed_message.empty()) {
            memset(mesg, 0, BUF_SIZE);  // Clear the buffer
            continue; // Ignore empty or whitespace-only messages
        }
        // If the command is REG, do registration
        if (strncmp(mesg, "REG ", 4) == 0) {
            registration(mesg, newsockfd, cliaddr);
        }
        // If the command is MESG, get the username, content, and broadcast it
        else if (strncmp(mesg, "MESG ", 5) == 0) {
            std::string sender_username = client_usernames[newsockfd];
            // Remove "MESG " from the message
            std::string message_content = mesg + 5;
            // Trim the message content
            message_content = trim(message_content);
            // Send the message to all other clients
            broadcastMESG(message_content, newsockfd, sender_username);
        }
        // If the command is PMSG, get the username, content, and send it to the recipient
        else if(strncmp(mesg, "PMSG ", 5) == 0){
            std::string sender_username = client_usernames[newsockfd];
            std::string rest_of_message = mesg + 5;
            rest_of_message = trim(rest_of_message);

            // Find the first space after the recipient username
            size_t space_pos = rest_of_message.find(' ');
            if (space_pos == std::string::npos) {
                // Invalid format, send error
                std::string UnknownError = "ERR 4\n";
                send(newsockfd, UnknownError.c_str(), UnknownError.size(), 0);
                continue;
            }

            std::string recipient_username = rest_of_message.substr(0, space_pos);
            std::string message_content = rest_of_message.substr(space_pos + 1);
            message_content = trim(message_content);

            sendPrivateMessage(recipient_username, message_content, newsockfd, sender_username);
        }
        // If the message is just EXIT, then get the username from the client, remove the user information, and close the socket
        else if(trimmed_message == "EXIT"){
            std::string username = client_usernames[newsockfd];



            // Remove the user from the server's data structures
            {
                std::lock_guard<std::mutex> lock(reg_users_mutex);
                connected_clients.erase(std::remove(connected_clients.begin(), connected_clients.end(), newsockfd), connected_clients.end());
                client_usernames.erase(newsockfd);
                usernames.erase(std::remove(usernames.begin(), usernames.end(), username), usernames.end());
            }

            sendUserList(newsockfd);


            // Send a message to the user confirming they have exited
            std::string exit_message = "You have exited the chat.\n";
            send(newsockfd, exit_message.c_str(), exit_message.size(), 0);

            // Notify other users that the user has left
            std::string leave_message = username + " has left the chat.\n";
            broadcastToAll(leave_message);

            // Remove the user from REGISTERED_USERS file
            removeUserFromFile("REGISTERED_USERS", username);

            // Close the socket
            close(newsockfd);

            user_exited = true;

            // Break the loop to end the thread
            break;

        }
        // Otherwise give an unknown format error
        else{
            std::string UnknownError = "ERR 4\n";
            send(newsockfd, UnknownError.c_str(), UnknownError.size(), 0);
            memset(mesg, 0, BUF_SIZE);
            continue;
        }
        // Clear the buffer to prevent garbled messages
        memset(mesg, 0, BUF_SIZE);
    }
    if (datalen < 0) {
        std::cerr << "Error receiving from client" << std::endl;
    }
    std::string disconnected_username = client_usernames[newsockfd];
    {
        // Lock Mutex
        std::lock_guard<std::mutex> lock(reg_users_mutex);
        // Erase the client information
        connected_clients.erase(std::remove(connected_clients.begin(), connected_clients.end(), newsockfd), connected_clients.end());
        client_usernames.erase(newsockfd);
        usernames.erase(std::remove(usernames.begin(), usernames.end(), disconnected_username), usernames.end());
    }
    // Broadcast to other clients that the user has left
    std::string leave_message = disconnected_username + " has left the chat.\n";
    broadcastToAll(leave_message);
    // Close the socket for the client leaving
    close(newsockfd);
}

/*
 * Function: main
 * Purpose: This function keeps the server on, and has to be here
*/

int main(int argc, char **argv)
{
    int sockfd;      /* socket listening for incoming connections */
    int newsockfd;   /* socket created for sending and receiving data */
    struct addrinfo hints = {0};
    struct addrinfo *servinfo;
    struct sockaddr_storage cliaddr; /* will store address of the client */
    socklen_t addrlen;

    hints.ai_family = AF_UNSPEC; 	// either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;	// TCP stream socket
    hints.ai_flags = AI_PASSIVE;	// fill in my IP for me


    if ((getaddrinfo(NULL, MY_PORT, &hints, &servinfo)) != 0) {
        cerr << "server: can't get address info" << endl;
        exit(1);
    }

    int datalen;
    char mesg[BUF_SIZE];

    /*
     * Open a TCP socket (an Internet stream socket).
     */
    if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
        cerr << "server: can't open stream socket" << endl;
        exit(1);
    }

    /*
     * Bind our local address so that the client can send to us.
     */
    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        cerr << "server: can't bind local address" << endl;
        exit(1);
    }

    freeaddrinfo(servinfo); // Done with this structure

    /*
     * Listen for incoming connection.
     */
    if (listen(sockfd, MAX_PENDING) < 0) {
        cerr << "server: error in listening" << endl;
        exit(1);
    }

    ofstream REG_USERS("REGISTERED_USERS", std::ios::app);
    // Close the file
    REG_USERS.close();

    for( ; ; ) {
        addrlen = sizeof cliaddr;
        int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &addrlen);
        if (newsockfd < 0) {
            cerr << "server: can't accept connection" << endl;
            continue;
        }

        // Create a new thread to handle the client
        std::thread client_thread(handleClient, newsockfd, cliaddr);
        client_thread.detach();  // Detach the thread to allow it to run independently
    }

    close(sockfd);
    return 0;
}
