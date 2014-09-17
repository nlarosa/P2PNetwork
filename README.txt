Nick LaRosa
Siddharth Saraph
Computer Networks

Project 3 Readme

For project 3, we decided to implement a peer to peer file transfer program.
The protocol is as follows:

The p2pd starts on the tracker, and listens for clients to register. When 
registering clients, it stores their IP and port number data in an array
of structures. Next, clients (peers) can start up on other host machines.
As soon as they start up, they register with the tracker. The tracker 
checks its array to make sure there are no duplicate clients registered
(only one entry per IP address). If a client tries to reregister, the
tracker just updates the client's port number data.

Once the client program is started, a thread listens on the specified
port for file search and transfer requests. If a peer tries to connect
to this port, it is accepted and transfered to a different thread. The
new thread determines whether the request is a search, transfer, or 
neither, and handles it appropriately.

While these threads are running, the client also displays a command prompt
for a user to enter commands. The valid commands are:

exit
search <File_String>
get <Host_Number> <Exact_File_Name>

The exit command simply terminates the client. 

The search contacts the tracker for a list of registered peers, and stores this 
list. Each peer in the list is contacted, and asked to return matching file 
names. The file matching part of our program handles multiple wildcards and 
regular expressions because we used grep to search for files. The search 
command only searches for files in the directory in which the client is running.
The search command displays matching results and a corresponding host number.
(HANDLING GENERAL FILE SEARCH REQUESTS IS OUR EXTRA FEATURE). 

The get command takes a host number, and an EXACT file name. The program will
transfer the file requested to the directory that the client is running in. 
The program should handle any requests for already-existent or non-existent
files appropriately.
