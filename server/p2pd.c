
/*

Nicholas LaRosa, Siddharth Saraph
CSE 30264, Project 3a, Server

usage: p2pd <Port_Number>

*/

#include <sys/socket.h>
#include <sys/time.h>

#include <mhash.h>

#include <netdb.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>

#define BUFFER 1024
#define HASH_SIZE 16
#define DEBUG 0

struct client
{
	char ip[16];
	uint16_t port;
};

int main( int argc, char** argv )
{
	int sockListen, sockConnect, isHost, i, err, willAccept;
	int inputBytes, bytes, outputBytes, totalBytes;
	struct sockaddr_in serverAddress;
	struct sockaddr_in * intermediate;
	struct sockaddr_storage clientAddress;
	struct addrinfo *hostInfo, *p;
	socklen_t socketSize;
	unsigned char * ipAddress;

	int reregisterIndex = -1;
	
	char sendLine[ BUFFER + 1 ];
	char recvLine[ BUFFER + 1 ];
	char ipstr[INET6_ADDRSTRLEN];
	char tempStr[16];

	char * filename;

	uint16_t filenameSize;		 	// client sends the size of the requested file name
	uint32_t fileSize;			// client recieves the size of the requested file
	unsigned int length;			// length of bytes written to a file	

	struct timeval startTimer;		// keep structs for recording timestamp at start and end of transfer
	struct tm * startTimeLocal;
	struct timeval endTimer;
	struct tm * endTimeLocal;	

	MHASH myHash;					// prepare for the md5 hashing of file retrieved
	unsigned char * readHash;	
	unsigned char * computeHash;			// have separate character arrays for hash retrieved and hash computed after retrieval
	unsigned char hashBuffer;	

	uint16_t portNumber;

	struct client clientArray[1024];		// holds all clients connected to the application
	uint32_t clientCount = 0;			// keep track of count
	uint32_t clientCountSend = 0;			// network ordered

	if( argc != 2 )
	{
		printf("\nusage: p2pd <Port_Number>\n\n");
		exit( 1 );
	}
	
	if( ( sockListen = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Server - socket() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( atoi( argv[1] ) );			// and receive server port

	if( bind( sockListen, ( struct sockaddr * )&serverAddress, sizeof( struct sockaddr_in) ) < 0 )
	{
		perror( "Server - bind() error" );
		exit( 1 );
	}
		
	if( listen( sockListen, 1024 ) < 0 ) 					// listen to up to 1024 connections
	{
		perror( "Server - listen() error" );
		exit( 1 );
    	}

	while( 1 )								// continue until server is ended
	{
		socketSize = sizeof( clientAddress );
	
		if ( ( sockConnect = accept( sockListen, ( struct sockaddr * )&clientAddress, &socketSize ) ) < 0 )	// wait for connection, and then accept it
		{
			perror( "Server - accept() error" );
			continue;
		}

		if( read( sockConnect, recvLine, sizeof( char ) ) <= 0 )	// first recieve either register (r) or update (u) 
		{
			perror( "Server - read() error upon command" );
			continue;
		}

		intermediate = ( struct sockaddr_in * )&clientAddress;
		ipAddress = ( unsigned char * )&intermediate->sin_addr.s_addr;

		if( recvLine[0] == 'r' )						// client needs to be registered
		{
			reregisterIndex = -1;
			if( DEBUG )	printf( "Received registration request.\n" );
			for( i = 0; i < clientCount; i++ )				// now begin sending each packet
			{
				sprintf( tempStr, "%d.%d.%d.%d\0", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3] );
				// tempStr is ip address of client that wants to register

				if( strcmp( tempStr, clientArray[i].ip ) == 0 )		// not the client's struct
				{
					reregisterIndex = i;	
					break;
				}
			}

			if (reregisterIndex == -1) reregisterIndex = clientCount;
			// if reregisterIndex != -1, then it is still equal to i from the above loop
			
			sprintf( clientArray[reregisterIndex].ip, "%d.%d.%d.%d\0", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3] );	// add IP address to current struct

			if( read( sockConnect, recvLine, sizeof( uint16_t ) ) <= 0 )	// receive port number
			{
				perror( "Server - read() error upon port" );
				continue;
			}	

			memcpy( &portNumber, recvLine, sizeof( uint16_t ) );		// store the first four bytes as a 32-bit integer

			portNumber = ntohs( portNumber );				// convert this to host long

			if( portNumber <= 1024 || portNumber >= 65534  )		// check for invalid ports
			{
				fprintf( stderr, "Invalid port number.\n" );
				close( sockConnect );
				continue;
			}
			
			clientArray[reregisterIndex].port = portNumber;			// add port number to current struct
			
			if( DEBUG  && (reregisterIndex == clientCount)) printf( "IP: %s, Port: %d now registered.\n", clientArray[clientCount].ip, clientArray[clientCount].port );

			if(reregisterIndex == clientCount) clientCount++;		// and increment to next
		
			close( sockConnect );
		}
		else if( recvLine[0] == 'u' )						// client needs updated list of peers
		{
			clientCountSend = htonl( clientCount - 1 );			// not including itself
			memcpy( sendLine, &clientCountSend, sizeof( uint32_t ) );	// copy client count to char buffer

			if( ( outputBytes = write( sockConnect, sendLine, sizeof( uint32_t ) ) ) <= 0 )		// send the number of peers
			{
				perror( "Server - write() error" );
				continue;
			}

			for( i = 0; i < clientCount; i++ )				// now begin sending each packet
			{
				sprintf( tempStr, "%d.%d.%d.%d\0", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3] );
				
				if( strcmp( tempStr, clientArray[i].ip ) != 0 )		// not the client's struct
				{
					memcpy( sendLine, clientArray[i].ip , sizeof( clientArray[i].ip ) );  //tempStr should be 16 bytes
					if (DEBUG) printf("sending ip address %s to client %s", clientArray[i].ip, tempStr);

					if( ( outputBytes = write( sockConnect, sendLine, sizeof( tempStr ) ) ) <= 0 )		// send the IP address
					{
						perror( "Server - write() error" );
						continue;
					}
					if( DEBUG ) printf("sent client ip address.\n");
				
					portNumber = htons( clientArray[i].port );
					memcpy( sendLine, &portNumber, sizeof( clientArray[i].port ) );
					
					if( ( outputBytes = write( sockConnect, sendLine, sizeof( clientArray[i].port ) ) ) <= 0 )		// send the port number
					{
						perror( "Server - write() error" );
						continue;
					}
				}
			}

			close( sockConnect );
		}
		else									// invalid packet
		{
			close( sockConnect );
			continue;
		}

	}

	if( close( sockListen ) != 0 )							// close the socket
	{
		printf( "Server - sockfd closing failed!\n" );
	}		

	return 0;
}

