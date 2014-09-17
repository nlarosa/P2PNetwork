
/*

Nicholas LaRosa, Siddharth Saraph
CSE 30264, Project 3a, Client

usage: p2p <Server_IP_Address> <Server_Port_Number> <Client_Port_Number>

*/

#include <sys/socket.h>
#include <sys/time.h>

#include <mhash.h>
#include <pthread.h>

#include <netdb.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>

#define BUFFER 1024
#define INPUT 64
#define HASH_SIZE 16
#define THREADS 1024
#define DEBUG 0

pthread_mutex_t hMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t atMutex = PTHREAD_MUTEX_INITIALIZER;		// mutex will lock activeThreads variable
pthread_mutex_t fpMutex = PTHREAD_MUTEX_INITIALIZER;		// mutex will lock file pointer (one write at a time)

pthread_cond_t atCond = PTHREAD_COND_INITIALIZER;		// allows the main thread to wait for changes to activeThreads

struct peer
{
	char ip[16];
	uint16_t port;
};

struct varsNeeded
{
	int socket;			// socket between server and client
	int * activeThreads;		// pointer to number of threads active
};

void * handleClient( void * arg )			// new thread will receive HAS or GET request from client
{
	if (DEBUG) printf("entered handleClient function\n");
	int socket = (( struct varsNeeded * )arg)->socket;			// dereference the arguments
	int * activeThreads = (( struct varsNeeded * )arg)->activeThreads;
	char recvLine[ BUFFER + 1 ];
	char sendLine[ BUFFER + 1 ];

	uint32_t fileNameSize;
	char fileName[ BUFFER + 1 ];

	int bytes, inputBytes, outputBytes, totalBytes;
	uint32_t fileSize;
	
	char grepCommand[ BUFFER + 1 ];
	size_t grepLen = 0;
	char * line = NULL;
	int ch;				// new line counter
	uint32_t lines = 0;		// line number
	int exitCode;
	char fileExists = 'e';

	if( read( socket, recvLine, sizeof(char) ) <= 0 )                		// first recieve the request
	{
		perror( "Request read()" );
		exit( 1 );
	}
	if (DEBUG) printf("received '%c' from a client\n", recvLine[0] );

	if( recvLine[0] == 'h' )			// file search
	{
		if (DEBUG) printf("hasFile? request from client\n");
		if( (exitCode = read( socket, recvLine, sizeof(uint32_t)) ) <= 0 )                	// first recieve the filename size
		{
			perror( "Filename size: read()" );
			exit( 1 );
		}
	
		if (DEBUG) printf("number of bytes read %d\n", exitCode);
		if (DEBUG) printf("before memcpy\n");
		memcpy( &fileNameSize, recvLine, sizeof(uint32_t) );		

		fileNameSize = ntohl( fileNameSize );
		if (DEBUG) printf("received fileNameSize %d from client\n", fileNameSize);

		if( read( socket, recvLine, fileNameSize ) <= 0 )                	// and receive the filename
		{
			perror( "Filename: read()" );
			exit( 1 );
		}
	
		memset( fileName, 0, BUFFER + 1 );
		memcpy( fileName, recvLine, fileNameSize );

		if (DEBUG) printf("Request file string %s \n", fileName);		

		pthread_mutex_lock( &fpMutex );		// lock the file pointer
		
		if (DEBUG) printf("Entered Mutex-Lock before grep.\n");
		sprintf( grepCommand, "ls | grep '%s' > .proj3a", fileName );		// build a grep command
		system( grepCommand );							// run the grep command
		if (DEBUG) printf("passed the grep command.\n");

		FILE * filePtr = fopen( ".proj3a", "r" );				// open the file for reading

		if( filePtr == NULL )					// file could not be opened (grep error)
		{
			pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
			if(DEBUG) printf( "AT Mutex locked at thread %u.\n", pthread_self() );
			(*activeThreads)--;			// decrement activeThreads right before exit
			if(DEBUG) printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
			pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
			pthread_mutex_unlock( &atMutex );	// unlock our mutex
			if(DEBUG) printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	
		
			close( socket );
                	pthread_exit( NULL );
		}
		else								// otherwise, search for results
		{
			if (DEBUG) printf( "The match file was created.\n" );
			while( EOF != ( ch = fgetc( filePtr ) ) )	// count number of lines
			{
				if (DEBUG) printf( "new line\n" );
				if( ch == '\n' )	++lines;
			}

			if (DEBUG) printf( "Matched found in client directory: %d\n", lines );

			lines = htonl( lines );
			memcpy( sendLine, &lines, sizeof(uint32_t) );

			if( write( socket, sendLine, sizeof(uint32_t) ) <= 0 )                	// send the number of matches
			{
				perror( "Grep matches: write()" );
				exit( 1 );
			}

			fseek( filePtr, 0, SEEK_SET );		// reset file pointer to beginning
	
			while( ( getline( &line, &grepLen, filePtr ) != -1 ) )	// each line
			{
				memset( sendLine, 0, INPUT );
				memcpy( sendLine, line, INPUT );		// line still have \n
				if( write( socket, sendLine, INPUT ) <= 0 )	// sending filename one at a time
				{
					perror( "Match files: write()" );
					exit( 1 );
				}
				
				if (DEBUG) printf( "Sendline is %s\n", sendLine );
			}
		}

		fclose( filePtr );

		if( ( exitCode = remove( ".proj3a" ) ) == -1 )
		{
			perror( "Search failed" );
			exit( 1 );
		}

		pthread_mutex_unlock( &fpMutex );		// unlock the file pointer
	}
	else if( recvLine[0] == 'g' )		// file transfer
	{
		if (DEBUG) printf( "get the file.\n" );

		memset( recvLine, 0, BUFFER );
		if( read( socket, recvLine, sizeof(uint32_t) ) <= 0 )                	// first recieve the filename size
		{
			perror( "Filename size: read()" );
			exit( 1 );
		}
	
		memcpy( &fileNameSize, recvLine, sizeof(uint32_t) );		

		fileNameSize = ntohl( fileNameSize );

		if( read( socket, recvLine, fileNameSize ) <= 0 )                	// and receive the filename
		{
			perror( "Filename: read()" );
			exit( 1 );
		}
	
		memset( fileName, 0, BUFFER + 1 );
		memcpy( fileName, recvLine, fileNameSize );

		FILE *filePtr = fopen( fileName, "r" );					// prepare to read the file again

		if( filePtr == NULL )
		{	
			fileExists = 'n';	
		}

		// close the file pointer, throw file mutex

		if (DEBUG) printf( "Sending %c\n", fileExists );
	
		memset( sendLine, 0, BUFFER );
		memcpy( sendLine, &fileExists, sizeof(char) );				// send character	
		if( ( outputBytes = write( socket, sendLine, sizeof(char) ) ) <= 0 )
        	{
			perror( "File exists: write()" );
			exit( 1 );
        	}

		if( fileExists == 'n' )
		{	
			pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
			if(DEBUG) printf( "AT Mutex locked at thread %u.\n", pthread_self() );
			(*activeThreads)--;			// decrement activeThreads right before exit
			if(DEBUG) printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
			pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
			pthread_mutex_unlock( &atMutex );	// unlock our mutex
			if(DEBUG) printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	
			
			pthread_exit( NULL );
		}

		fseek( filePtr, 0, SEEK_END );				
		fileSize = ftell( filePtr );				// get number of bytes
		fseek( filePtr, 0, SEEK_SET );				// reset file pointer to beginning

		if (DEBUG) printf("Found file size.\n" );
		if( DEBUG ) printf( "Sending file size %d\n", fileSize );
		
		fileSize = htonl( fileSize );					// convert the byte order to that of the network

		memset( sendLine, 0, BUFFER );
		memcpy( sendLine, &fileSize, sizeof( uint32_t ) );		// place the size in a character buffer to be sent

		if (DEBUG) printf("Memcpy went okay\n");
		if (DEBUG) printf("Sendline: %d %d %d %d\n", sendLine[0], sendLine[1], sendLine[2], sendLine[3]);

		if( ( outputBytes = write( socket, sendLine, sizeof( uint32_t ) ) ) <= 0 )
        	{
			perror( "File size: write()" );
			exit( 1 );
        	}

		inputBytes = 0;								// inputBytes is the number of bytes in the current buffer
		totalBytes = 0;
	
		fileSize = ntohl( fileSize );

		if (DEBUG) printf( "Sending %d bytes...\n", fileSize );

		while( totalBytes < fileSize )						// continue through 
		{
			if( fileSize > BUFFER ) 						// fill the rest of the buffer if our file is too big for a single
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), BUFFER - inputBytes, filePtr ) ) <= 0 )	// transfer just enough to fill the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( socket );
					exit( 1 );
				}
				
				inputBytes += bytes;
			}
			else                                                                            // if our file will not overflow the buffer
			{
				if( ( bytes = fread( sendLine + inputBytes, sizeof( char ), fileSize - inputBytes, filePtr ) ) <= 0 )	// transer the entire file into the buffer
				{
					perror( "Error reading from file" );					// close the socket if we recieve zero
					close( socket );
					exit( 1 );
				}

				inputBytes += bytes;
				if( inputBytes >= fileSize )	break;
			}
			totalBytes += bytes;

			if( inputBytes >= BUFFER )                              // if our buffer is full
			{
				sendLine[ BUFFER ] = '\0';
				if( ( outputBytes = write( socket, sendLine, BUFFER ) ) <= 0 )
        			{
					perror( "Error sending file" );
					pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
					if(DEBUG) printf( "AT Mutex locked at thread %u.\n", pthread_self() );
					(*activeThreads)--;			// decrement activeThreads right before exit
					if(DEBUG) printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
					pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
					pthread_mutex_unlock( &atMutex );	// unlock our mutex
					if(DEBUG) printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	
			
					pthread_exit( NULL );
        			}
				inputBytes = 0;
			}
		}
	
		if(DEBUG) printf( "And %d bytes more.\n" );

		if( inputBytes > 0 )                                            // if there are still bytes in the buffer to write
		{
			sendLine[ inputBytes ] = '\0';
			if( ( outputBytes = write( socket, sendLine, inputBytes ) ) <= 0 )
        		{
				perror( "Error sending file" );
				pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
				if(DEBUG) printf( "AT Mutex locked at thread %u.\n", pthread_self() );
				(*activeThreads)--;			// decrement activeThreads right before exit
				if(DEBUG) printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
				pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
				pthread_mutex_unlock( &atMutex );	// unlock our mutex
				if(DEBUG) printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	
			
				pthread_exit( NULL );
        		}	
		}

		fclose( filePtr );
	}

	pthread_mutex_lock( &atMutex );		// lock our mutex around activeThreads
//	printf( "AT Mutex locked at thread %u.\n", pthread_self() );
	(*activeThreads)--;			// decrement activeThreads right before exit
//	printf( "Closing thread. There are now %d threads active.\n", *activeThreads );
	pthread_cond_signal( &atCond );		// signal that activeThreads has been modified
	pthread_mutex_unlock( &atMutex );	// unlock our mutex
//	printf( "AT Mutex unlocked at thread %u.\n", pthread_self() );	

	if( close( socket ) != 0 )		// close the socket
	{
		printf( "Server - socket closing failed!\n" );
	}

	pthread_exit( NULL );
}

void * waitForClients( void * arg )		// wait for connections from clients
{
	int serverPort = *((int *)arg);

	int sockListen;
	int sockConnect[ THREADS ];		// array of sockets and threads
	int activeThreads;
	pthread_t threads[ THREADS ];
	socklen_t socketSize;
	struct sockaddr_in serverAddress;		// this client becomes a server
	struct sockaddr_storage clientAddress;	// listening for other clients

	int sockNumber = 0;			// current socket number

	if( ( sockListen = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Server - socket() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( serverPort );				// and set server port

	if (DEBUG) printf("Listening for peers on port %d\n", serverPort);
	
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

	pthread_mutex_lock( &atMutex );
	activeThreads = 0;							// initialize thread count
	pthread_mutex_unlock( &atMutex );

	while( 1 )								// continue until server is ended
	{
		if (DEBUG) printf("begin accept loop\n");
		pthread_mutex_lock( &atMutex );
		while( activeThreads < THREADS )
		{
			if (DEBUG) printf("begin activeThreads loop\n");
			pthread_mutex_unlock( &atMutex );			// unlock mutex after comparison		
	
			socketSize = sizeof( clientAddress );
	
			if ( ( sockConnect[sockNumber] = accept( sockListen, ( struct sockaddr * )&clientAddress, &socketSize ) ) < 0 )	// accept new connection
			{
				perror( "Server - accept() error" );
				continue;
			}
			if (DEBUG) printf("Accepted new peer\n");

			pthread_mutex_lock( &atMutex );
			struct varsNeeded threadArg;
			threadArg.socket = sockConnect[sockNumber];
			threadArg.activeThreads = &activeThreads;	
			pthread_mutex_unlock( &atMutex );

			if (DEBUG) printf( "Before handling client...\n" );

			if( pthread_create( &threads[sockNumber], NULL, handleClient, ( void * )&threadArg ) != 0 )
			{
				perror( "Server - pthread_create() error" );
				continue;
			} 
	
			if (DEBUG) printf( "Inside of waiting loop.\n" );	
			pthread_mutex_lock( &atMutex );
			activeThreads++;					// increment thread counter
			if (DEBUG) printf( "Starting thread. There are now %d threads active.\n", activeThreads );
			pthread_mutex_unlock( &atMutex );

			sockNumber = ( sockNumber + 1 ) % THREADS;		// array index for socket
		}
			
		pthread_mutex_lock( &atMutex );
		pthread_cond_wait( &atCond, &atMutex );				// wait to start any more threads until we get the signal
		pthread_mutex_unlock( &atMutex );				// unlock our mutex
	}

	pthread_exit( NULL );
}

int main( int argc, char** argv )
{
	int sockfd, err, isHost, sockTracker, sockPeer;
	int inputBytes, bytes, outputBytes, totalBytes, length;
	struct sockaddr_in serverAddress, peerAddress;
	struct addrinfo *hostInfo, *p;

	int i, j, k, m, n;

	char sendLine[ BUFFER + 1 ];
	char recvLine[ BUFFER + 1 ];
	char commandBuffer[ INPUT + 1 ];
	char * split;
	int splitNumber;
	int peerIndex;

	char fileName[64];
	char * outputString;
	uint32_t fileNameSize = 0;

	uint16_t portNumber;
	uint32_t numPeers, numFiles;
	uint32_t fileSize;
	
	struct timeval startTimer;		// keep structs for recording timestamp at start and end of transfer
	struct timeval endTimer;

	int finalSec;
	int finalMilli;
	long int totalMilli;			// total number of milliseconds
	float speedInMB;			// we will calculate the file transfer speed in MB per second

	char fileExists = 'n';
	struct peer peerArray[1024];
	pthread_t thread;

	memset(fileName, 0, sizeof(fileName));	// initialize filename to null characters

	if( argc != 4 )				// client is listening on given port for peers
	{
		printf("\nusage: p2p <Server_IP_Address> <Server_Port_Number> <Client_Port_Number>\n\n");
		exit( 1 );
	}

	// before registering, begin threaded listening

	portNumber = atoi( argv[3] );

	if( portNumber <= 1024 || portNumber >= 65534 )			// check for valid port number
	{
		fprintf( stderr, "Invalid port number.\n" );
		exit( 1 );
	}

	if( pthread_create( &thread, NULL, waitForClients, (void *)&portNumber ) != 0 )
	{
		perror( "Server - pthread_create() error" );
		exit( 1 );
	}
	
	// now open socket to begin registering

	if( ( sockfd = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
	{
		perror( "Client - socket() error" );
		exit( 1 );
	}

	memset( ( char * )&serverAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons( atoi( argv[2] ) );			// and receive server port
	serverAddress.sin_addr.s_addr = inet_addr( argv[1] );			// receive server address

	if( connect( sockfd, (struct sockaddr *)&serverAddress, sizeof( struct sockaddr_in ) ) < 0 )	// connect() via TCP
	{
		perror( "Client - connect() error" );
		exit( 1 );
	}

	memset(sendLine, 114, 1);				// char 114 corresponds to 'r' for registration
	if( ( outputBytes = write( sockfd, sendLine, 1 ) ) <= 0 )
	{
		perror( "Client - write() error" );
		exit( 1 );
	}

	portNumber = htons( atoi( argv[3] ) );			// convert argument port to 32 bit

	memcpy( sendLine, &portNumber, sizeof( uint16_t ) );	// store port number in char array

	if( ( outputBytes = write( sockfd, sendLine, sizeof( uint16_t ) ) ) <= 0 )
	{
		perror( "Client - sendto() error" );
		exit( 1 );
	}

	close( sockfd );

	// begin command line after registering
	
	// numPeers will only go up upon searching, because we haven't implemented 
	// peer removal at the the tracker
	numPeers = 0;
	while( 1 )
	{
		if (DEBUG) printf( "Hello loop");
		printf( "\n[p2p]$ " );						// command prompt
		fgets( commandBuffer, sizeof( commandBuffer ), stdin );		// retrieve full buffer, not more
	
		split = strtok( commandBuffer, " \n\t" );				// split on space token
		if (split == NULL)
		{
			if (DEBUG) printf( "split NULL1\n");
			printf( "\n[p2p]$ Commands: 'search <file_name>', 'get <host_number> <file_name>', 'exit'\n" );
			continue;	
		}

		if( !strcmp( split, "search" ) )			// first split should be search
		{
			if (DEBUG) printf( "Entered 'search' branch\n");
			split = strtok(NULL, "");
			if (split == NULL || split[0] < 32 )
			{
				if (DEBUG) printf( "split NULL2\n");
				printf( "\n[p2p]$ Commands: 'search <file_name>', 'get <host_number> <file_name>', 'exit'\n" );
				continue;	
			}
			if (DEBUG) printf("split is %s\n", split);

			memset(fileName, 0, INPUT);
			memcpy(fileName, split, strlen(split)-1); 				//split is null terminated
			fileNameSize = strlen(fileName);
			if (DEBUG) printf("fileNameSize to send is %d\n", fileNameSize);
			if (DEBUG) printf("fileName is %s\n", fileName);
			fileNameSize = htonl(fileNameSize);
			if (DEBUG) printf("fileNameSize is now %d\n", fileNameSize);
			if( ( sockTracker = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
			{
				perror( "Client - search socket() error " );
				exit( 1 );
			}

			if( connect( sockTracker, (struct sockaddr *)&serverAddress, sizeof( struct sockaddr_in ) ) < 0 )	// connect() via TCP
			{
				perror( "Client - search connect() error" );
				exit( 1 );
			}

			// sending update request
			memset(sendLine, 117, 1); 			// char 117 corresponds to 'u'
			if( ( outputBytes = write( sockTracker, sendLine, 1 ) ) <= 0 )
			{
				perror( "Client - search sendto() error" );
				exit( 1 );
			}

			if( ( inputBytes = read( sockTracker, recvLine, sizeof(uint32_t) ) ) <= 0 )
			{
				perror( "Client - search read() numPeers error" );
				exit( 1 );
			}

			memcpy(&numPeers, recvLine, sizeof(uint32_t));
			numPeers = ntohl(numPeers);

			// receive list of peers from tracker
			for(i = 0; i < numPeers; i++)		
			{
				if( ( inputBytes = read( sockTracker, recvLine, sizeof(peerArray[i].ip) ) ) <= 0 )
				{
					perror( "Client - search read() ip error" );
					exit( 1 );
				}
				memcpy(peerArray[i].ip, recvLine, sizeof(peerArray[i].ip));

				if( ( inputBytes = read( sockTracker, recvLine, sizeof(peerArray[i].port) ) ) <= 0 )
				{
					perror( "Client - search read() port error" );
					exit( 1 );
				}
				
				memcpy(&peerArray[i].port, recvLine, sizeof(peerArray[i].port)); 
				peerArray[i].port = ntohs(peerArray[i].port);
			}
			if ( numPeers == 0)
			{
				printf("\n[p2p]$ No available peers for search.\n");
				continue;
			}

			close(sockTracker);

			// connect to each peer to search for file
			for(i = 0; i < numPeers; i++)
			{	
				memset(sendLine, 0, sizeof(sendLine));
				if( ( sockPeer = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
				{
					perror( "Client - socket() error" );
					exit( 1 );
				}

				if (DEBUG) printf("Attempting connection to %s at port %d\n", peerArray[i].ip, peerArray[i].port);

				memset( ( char * )&peerAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
				peerAddress.sin_family = AF_INET;
				peerAddress.sin_port = htons( peerArray[i].port );			//peer port
				peerAddress.sin_addr.s_addr = inet_addr( peerArray[i].ip );		//peer address

				if( connect( sockPeer, (struct sockaddr *)&peerAddress, sizeof( struct sockaddr_in ) ) < 0 )	// connect() via TCP
				{
					perror( "\n[p2p]$ Peer unreachable" );
					close( sockPeer );
					continue;
				}

				memset(sendLine, 104, 1); // char 10 corresponds to 'h' for hasFile request to peer
				if( ( outputBytes = write( sockPeer, sendLine, 1 ) ) <= 0 )
				{
					perror( "Client - write() error" );
					exit( 1 );
				}

				// fileNameSize and fileName were filled in at entry of the search branch
				memset(sendLine, 0, sizeof(sendLine));
				memcpy(sendLine, &fileNameSize, sizeof(fileNameSize));
				if (DEBUG) printf("fileNameSize is now %d\n", fileNameSize);
				if( ( outputBytes = write( sockPeer, sendLine, sizeof(fileNameSize) ) ) <= 0 )
				{
					perror( "Client - write() error" );
					exit( 1 );
				}

				memset(sendLine, 0, sizeof(sendLine));
				memcpy(sendLine, fileName, strlen(fileName));
				// send the search term (size followed by string) to peer
				if( ( outputBytes = write( sockPeer, sendLine, strlen(fileName) ) ) <= 0 )
				{
					perror( "Client - write() error" );
					exit( 1 );
				}

				// receive number of matching files and file names
				if( ( inputBytes = read( sockPeer, recvLine, sizeof(uint32_t) ) ) <= 0 )
				{
					perror( "Client - search read() numFiles error" );
					exit( 1 );
				}
				memcpy(&numFiles, recvLine, sizeof(uint32_t));
				numFiles = ntohl(numFiles);
				if (DEBUG) printf( "Number of matches: %d\n", numFiles );

				for(j = 0; j < numFiles; j++)
				{
					memset(recvLine, 0, INPUT+1);
					if( ( inputBytes = read( sockPeer, recvLine, INPUT ) ) <= 0 )
					{
						perror( "Client - search read() fileName error" );
						exit( 1 );
					}
					//print out index + 1
					printf("\nPeer number %d at %s port %d has file: %s", i+1, peerArray[i].ip, peerArray[i].port, recvLine);
				}
				if (numFiles == 0)
				{
					printf("\nPeer number %d at %s port %d has no matching file.\n", i+1, peerArray[i].ip, peerArray[i].port);
				}

				close( sockPeer );
			}
		}
		else if( !strcmp( split, "get" ) )
		{
			if (DEBUG) printf( "Entered 'get' branch\n");
			split = strtok( NULL, " \n\t" );			// split again to get host number
			if (split == NULL)
			{
				if (DEBUG) printf( "split NULL3\n");
				printf( "\n[p2p]$ Commands: 'search <file_name>', 'get <host_number> <file_name>', 'exit'\n" );
				continue;	
			}

			peerIndex = atoi( split );

			if( peerIndex <= 0 )				// atoi() indicates this is not a number
			{
				printf( "\n[p2p]$ Please specify a valid host number.\n" );
				continue;
			}

			if (DEBUG) printf( "You asked for peer %d out of %d peers.\n", peerIndex, numPeers );

			if (peerIndex > numPeers )
			{
				printf( "\n[p2p]$ No such host exists. Search for an updated host listing.\n" );
			}
			else
			{
				split = strtok( NULL, "" );			// get the rest of the input, which should be filename
				if (split == NULL || split[0] < 32 )
				{
					if (DEBUG) printf( "split NULL1\n");
					printf( "\n[p2p]$ Commands: 'search <file_name>', 'get <host_number> <file_name>', 'exit'\n" );
					continue;	
				}

				memset( ( char * )&peerAddress, 0, sizeof( struct sockaddr_in ) );	// secure enough memory for the server socket
				peerAddress.sin_family = AF_INET;
				peerAddress.sin_port = htons( peerArray[peerIndex-1].port );		// and receive peer port
				peerAddress.sin_addr.s_addr = inet_addr( peerArray[peerIndex-1].ip );	// receive peer address

				if( ( sockPeer = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )	// open stream socket, address family internet
				{
					perror( "Client - socket() error" );
					exit( 1 );
				}

				if( connect( sockPeer, (struct sockaddr *)&peerAddress, sizeof( struct sockaddr_in ) ) < 0 )    // connect() via TCP
				{
					perror( "Peer connect()" );
					continue;
				}

				memset(sendLine, 103, 1); // char 103 corresponds to 'g' for getFile request to peer
				if( ( outputBytes = write( sockPeer, sendLine, 1 ) ) <= 0 )
				{
					perror( "Get request: write()" );
					exit( 1 );
				}
		
				memset(fileName, 0, INPUT);
				memcpy(fileName, split, strlen(split)-1);
				fileNameSize = strlen(fileName);
				fileNameSize = htonl(fileNameSize);
				
				// fileNameSize and fileName were filled in at entry of the search branch
				memset(sendLine, 0, sizeof(sendLine));
				memcpy(sendLine, &fileNameSize, sizeof(fileNameSize));
				if (DEBUG) printf("fileNameSize is now %d\n", fileNameSize);
				if( ( outputBytes = write( sockPeer, sendLine, sizeof(fileNameSize) ) ) <= 0 )
				{
					perror( "Filename size: write()" );
					exit( 1 );
				}

				memset(sendLine, 0, sizeof(sendLine));
				memcpy(sendLine, fileName, strlen(fileName));
				if (DEBUG) printf("fileName is now %s\n", fileName);
				// send the search term (size followed by string) to peer
				if( ( outputBytes = write( sockPeer, sendLine, strlen(fileName) ) ) <= 0 )
				{
					perror( "Filename: write()" );
					exit( 1 );
				}

				fileExists = 'n';
				if( ( inputBytes = read( sockPeer, &fileExists, sizeof(char ) ) ) <= 0 )
				{
					perror( "File exists: read()" );
					exit( 1 );
				}

				if (DEBUG) printf( "Read character %c\n", fileExists );				

				if( fileExists == 'n' )
				{
					printf( "\n[p2p]$ Peer %d has no such file.\n", peerIndex );
					continue;
				}

				if (DEBUG) printf( "%s file exists\n", fileName );
				
				memset( recvLine, 0, BUFFER );

				// write the received character array to our 32-bit int, holding file size	
				if( ( inputBytes = read( sockPeer, recvLine, sizeof( uint32_t ) ) ) <= 0 )
				{
					perror( "File size: read()" );
					exit( 1 );
				}

				memcpy( &fileSize, recvLine, sizeof( uint32_t ) );
				fileSize = ntohl( fileSize );	

				if(DEBUG) printf( "The file will be %d bytes\n", fileSize );

				if( access( fileName, F_OK ) != -1 )
				{
					printf( "\n[p2p]$ File already exists in your directory.\n" );
					continue;
				}

				FILE *filePtr = fopen( fileName, "w" );			// prepare to read and write to file
				if( filePtr == NULL )
				{
					perror( "File cannot be retrieved" );
					continue;
				}

				inputBytes = 0;							// inputBytes is the number of bytes in the current buffer
				totalBytes = 0;							// and totalBytes is the number of bytes currently written to our fill

				while( totalBytes < fileSize )
				{
					if( fileSize > BUFFER )					// fill the rest of the buffer if our file is too big for a single
					{
						if( ( bytes = read( sockPeer, sendLine + inputBytes, BUFFER - inputBytes ) ) <= 0 )	// transfer just enough to fill the buffer
						{
							perror( "Client - read() error" );						// close the socket if we recieve zero
							close( sockPeer );
							exit( 1 );
						}
					
						inputBytes += bytes;
					}
					else												// if our file will not overflow the buffer
					{
						if( ( bytes = read( sockPeer, sendLine + inputBytes, fileSize - inputBytes ) ) <= 0 )	// transer the entire file into the buffer
						{
							perror( "Client - read() error" );				// close the socket if we recieve zero
							close( sockPeer );
							exit( 1 );
						}

						inputBytes += bytes;
						if( inputBytes >= fileSize )	break;
					}
					totalBytes += bytes;

					if( inputBytes >= BUFFER )							// if our buffer is full
					{
						sendLine[ BUFFER ] = '\0';
						length = fwrite( sendLine, sizeof( char ), BUFFER, filePtr );
						if( length == 0 )
						{
							perror( "Error writing to file" );
							exit( 1 );
						}

						inputBytes = 0;
					}
				}
				
				if(DEBUG) printf( "\n[p2p]$ %d total bytes downloaded.\n", totalBytes );
				if(DEBUG) printf( "\n[p2p]$ %d more bytes.\n", inputBytes );

				if( inputBytes > 0 )						// if there are still bytes in the buffer to write
				{
					sendLine[ inputBytes ] = '\0';
					length = fwrite( sendLine, sizeof( char ), inputBytes, filePtr );
					if( length == 0 )
					{
						perror( "Error writing to file" );
						exit( 1 );
					}
				}

				fclose( filePtr );
			}

			close( sockPeer );
		}
		else if( !strcmp( split, "exit" ) )		// user exits
		{
			printf( "\n[p2p]$ That's all, folks!\n\n" );
			exit( 0 );
		}
		else
		{
			printf( "\n[p2p]$ Commands: 'search <file_name>', 'get <host_number> <file_name>', 'exit'\n" );
		}
	}

	if( close( sockfd ) != 0 )					// close the socket
	{
		printf( "Client - sockfd closing failed!\n" );
	}		

	return 0;
}

