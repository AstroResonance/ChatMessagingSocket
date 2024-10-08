# **Client-Server Instructions**


## 1) Put the server.cpp and client.cpp files into your directory
## 2) Make executable objects with the files by doing the following:
  ```g++ -std=c++11 -pthread -o server server.cpp``` (for server) <br>
  ```g++ -std=c++11 -o client client.cpp``` (for client) <br>
## 3) Run these executable objects by doing the following:
```
   ./client {server_name}
   ./server
```

## 4) Once the programs are running, you can do the following commands<br>
  1) ```REG {Username}``` (This will register you and let you chat with other clients)<br>
  2) ```MESG {Message}``` (This is the global chat command that will let you chat with other clients)<br>
  3) ```PMSG {Username} {Message} ```(This will let you send a direct message to another user)<br>
  4) ```EXIT``` (This will let you exit the chat)<br>



  # **Happy Chatting!**
 
