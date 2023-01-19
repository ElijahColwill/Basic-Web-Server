// Import Required Libaries
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <stdint.h>
#include <algorithm>
#include <dlfcn.h>
#include <link.h>
#include <arpa/inet.h>
#include <ctime>
#include <cmath>

// Initialize queue length
int QueueLength = 5;

// Create response strings to send to client
std::string foundResponse = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\nContent-type: ";
std::string cgiResponse = "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n";
std::string notFoundResponse = "HTTP/1.1 404 File Not Found \r\nServer: CS 252 lab5\r\nContent-type: ";
std::string errorMessage = "Error 404: File Not Found - Could not find the specified URL.";
std::string authMessage = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"myhttpd-cs252\"\r\n\r\n";

// Initialize mode string to store server mode
std::string mode;

// Process time request
void processRequest( int socket );

// Pool of threads
void poolThreads( int socket );

// Pool of processes
void poolProcesses( int socket );

// Loop socket from pool of threads/processes
void loopSocket( int socket );

// Display a directory listing with a sort flag
void listDirectory( int socket, std::string finalPath, std::string documentRequested, std::string flag );

// CGI-BIN Handler Function
void cgiBin( int socket, std::string finalPath, std::string queryString );

// Stat and Logs Page Handler Functions and Relevant Variables
void statPage( int socket );
void updateTime(double complete_time, std::string documentRequested); 
int globalRequest = 0;
char * clientIP;
time_t startTime;
int minTime = -1;
int maxTime = -1;
std::string minRequest;
std::string maxRequest;

// Logs and Stats File Objects and paths
FILE * logsFile;
FILE * statsFile;
const char * logs_path = "./http-root-dir/htdocs/logs.txt";
const char * stat_path = "./http-root-dir/htdocs/stat.html";

// Loadable Module Handler Function and TypeDef for httpfun function
void loadableModule( int socket, std::string queryString, std::string documentRequested );
typedef void(*httprun_f)(int ssock, const char* query_string);

// Declare Mutexes
pthread_mutex_t mutex;
pthread_mutex_t mutex_count;

// Help Function: Usage information for invalid input or invalid port
const char * usage =
"                                                               \n"
"myhttpd Usage:                                                 \n"
"                                                               \n"
"HTTP Web Server - Lab 5 Implementation                         \n"
"- Elijah Colwill/ecolwill                                      \n"
"                                                               \n"
"To run the server type:                                        \n"
"                                                               \n"
"   myhttpd [-f|-t|-p|-o] <port>                                \n"
"                                                               \n"
"Where 1024 < port < 65536. If no flag is given, the server     \n"
"will run in iterative mode. If no port is given, the server    \n"
"will use the default port for myhttpd (3302)                   \n"
"                                                               \n"
"Options for flag:                                              \n"
"      -f: Create a new process for each request (process mode) \n"
"      -t: Create a new thread for each request (thread mode)   \n"
"      -p: Pool of threads (pool-t mode)                        \n"
"      -o: Pool of processes (pool-f mode)			\n"
"                                                               \n"
"Authentication is required on the client side to load          \n"
"documents.                                                     \n"
"                                                               \n";

// Kill zombie processes
extern "C" void sigCHLD (int sig) {
	if (sig == SIGCHLD) {
		while(waitpid(-1, NULL, WNOHANG) > 0);
	}
}

// File Information Struct for Directory Listing
struct fileStruct {
	std::string type;
	std::string path;
	std::string name;
	std::string time;
	std::string size;
};


// Comparator functions to sort files
int comp_name(fileStruct file_A, fileStruct file_B) {
	return file_A.name < file_B.name;
}

int comp_date(fileStruct file_A, fileStruct file_B) {
	return file_A.time < file_B.time;
}

int comp_size(fileStruct file_A, fileStruct file_B) {
	return std::stof(file_A.size) < std::stof(file_B.size);
}

// Main function
int main(int argc, char ** argv) {
	// Check number of arguments for correct usage
	if ( argc >= 4 ) {
		fprintf( stderr, "%s", usage );
		exit( -1 );
	}

	
	// Set Server Mode and Port Depending on Flag
	int port;

	// One Argument: Default port number and iterative move
	if ( argc == 1 ) {
		port = 3302; // Default Port Number for myhttpd
		mode = "iterative";
	}

	// Two Arguments: Custom port or mode
	if ( argc == 2 ) {
		port = 3302; // Default Port Number of myhttpd
		mode = "iterative";
		if ( !strcmp(argv[1], "-f") ) {
			mode = "process";
		} else if ( !strcmp(argv[1], "-t") ) {
			mode = "thread";
		} else if ( !strcmp(argv[1], "-p") ) {
			mode = "pool-t";
		} else if ( !strcmp(argv[1], "-o") ) {
			mode = "pool-p";
		} else {
			port = atoi( argv[1] );
			if (port == 0 || port <= 1024 || port >= 65536 ) {
				fprintf( stderr, "%s", usage );
				exit( -1 );
			}
		}
	}

	// Three Arguments: Custom port and mode
	if ( argc == 3 ) {
		port = atoi( argv[2] );
		if (port == 0 || port <= 1024 || port >= 65536 ) {
				fprintf( stderr, "%s", usage );
				exit( -1 );
		}
		if ( !strcmp(argv[1], "-f") ) {
			mode = "process";
		} else if ( !strcmp(argv[1], "-t") ) {
			mode = "thread";
		} else if ( !strcmp(argv[1], "-p") ) {
			mode = "pool-t";
		} else if ( !strcmp(argv[1], "-o") ) {
			mode = "pool-p";
		} else {
			fprintf( stderr, "%s", usage );
			exit( -1 );
		}
	}

	// Initialize start time and start counting total uptime
	time(&startTime);

	// Initialize mutexes
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&mutex_count, NULL);

	// Initialize signal handler for zombie processes
	struct sigaction sa;
	sa.sa_handler = sigCHLD;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	// Set listeners for SIGCHLD and SIGPIPE processes
	if(sigaction(SIGCHLD, &sa, NULL)) {
		perror("sigaction-sigchld");
		exit(2);
	}

	if(sigaction(SIGPIPE, &sa, NULL)) {
		perror("sigaction-sigpipe");
		exit(2);		
	}


	// Set the IP Address and Port for the Server
	struct sockaddr_in serverIPAddress;
	memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
	serverIPAddress.sin_family = AF_INET;
	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
	serverIPAddress.sin_port = htons((u_short) port);

	// Allocate a socket
	int masterSocket = socket(PF_INET, SOCK_STREAM, 0);
	if ( masterSocket < 0 ) {
		perror("socket");
		exit( -1 );
	}

	// Set socket options to reuse port without wait period
	int optval = 1;
	int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR,
			(char *) &optval, sizeof( int ));

	// Bind the socket to the IP address and port
	int error = bind( masterSocket,
			(struct sockaddr *)&serverIPAddress,
			sizeof( serverIPAddress ));
	if( error ) {
		perror("bind");
		exit( -1 );
	}

	// Put the socket in listening mode and set size of queue for
	// unprocessed connections
	error = listen( masterSocket, QueueLength );
	if ( error ) {
		perror("listen");
		exit( -1 );
	}


	if (mode == "pool-t") {
		// Create pool of theads
		poolThreads( masterSocket );
		exit(0);
	}

	if (mode == "pool-p") {
		// Create pool of processes
		poolProcesses( masterSocket );
		exit(0);
	}

	// Wait and Process Requests/Connections
	while( 1 ) {
		// Accept incoming connections
		struct sockaddr_in clientIPAddress;
		int alen = sizeof( clientIPAddress );
		int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, 
			       (socklen_t *)&alen );

		if (slaveSocket < 0) {
			perror("accept");
			exit( -1 );
		}

		// Store client IP address for log purposes 
		clientIP = inet_ntoa(clientIPAddress.sin_addr);

		if (mode == "iterative") {
			// Process Request
			processRequest( slaveSocket );
		}

		if (mode == "process") {
			// Create new process for each request
			int ret = fork();
			if (ret == 0) {
				// Process Request
				processRequest( slaveSocket );
				exit(0);
			}
			close( slaveSocket );
		}

		if (mode == "thread") {
			// Initalize concurrency resources
			pthread_t thread;
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			// Process Request on new thread
			pthread_create(&thread, &attr, (void* (*)(void*)) processRequest, (void *) slaveSocket );
		}
		
	}

}

void poolThreads( int socket ) {
	// Create pool of threads and call loopSocket for each one
        pthread_t thread[4];
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); 

	for (int i = 0; i < 4; i++) {
		pthread_create(&thread[i], NULL, (void* (*)(void *)) loopSocket, (void *) socket);
	}
	loopSocket( socket );
}

void poolProcesses( int socket ) {
	// Create pool of processes and call loopSocket for each one
	for (int i = 0; i < 4; i++) {
		int pid = fork();
		if (pid == 0) {
			loopSocket( socket );
		}
	}
	loopSocket( socket );
}

void loopSocket ( int socket ) {
	while(1) {
		// Accept incoming connections
		struct sockaddr_in clientIPAddress;
		int alen = sizeof( clientIPAddress );
		// Force accept() to be atomic
		pthread_mutex_lock(&mutex);
		int slaveSocket = accept( socket, (struct sockaddr *)&clientIPAddress, 
			       (socklen_t *)&alen );
		pthread_mutex_unlock(&mutex);
		
		if (slaveSocket < 0) {
			perror("accept");
			exit( -1 );
		}

		// Store client IP address for log purposes 
		clientIP = inet_ntoa(clientIPAddress.sin_addr);

		// Process Request
		processRequest( slaveSocket );

	}
}

void listDirectory( int socket, std::string finalPath, std::string documentRequested, std::string flag ) {

	// Create query to pass to table, flipping if the Ascending flag is set for an option
	std::string renderFlag1 = "?C=N;O=A";
	std::string renderFlag2 = "?C=M;O=A";
	std::string renderFlag3 = "?C=S;O=A";

	// Change render flags for descending order based on previous request
	if (flag == "name-A") renderFlag1 = "?C=N;O=D";
	if (flag == "moddate-A") renderFlag2 = "?C=M;O=D";
	if (flag == "size-A") renderFlag3 = "?C=S;O=D";


	// Set parent directory based on calculating the absoltute path
	size_t slash_idx = finalPath.find_last_of('/');
	int includeParent = 0;
	std::string parentDir = "ERROR";
	if (slash_idx != std::string::npos) {
		char pwd[256];
		getcwd(pwd, 256);
		parentDir = finalPath.substr(0, slash_idx + 1);
		if (parentDir.substr(0, strlen(pwd) + 21) == (std::string(pwd) + "/http-root-dir/htdocs") && 
				parentDir.size() > (strlen(pwd) + 21)) {
			parentDir = parentDir.substr(strlen(pwd) + 21, std::string::npos);
			includeParent = 1;
		} 
	}

	// Pre and post list strings to render directory list HTML flag with links
	std::string dlString1 = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
			   "<html>\n"
 			   "<head>\n";
	std::string dlString2 = "<title>Index of " + finalPath + "</title>\n";
	std::string dlString3 = "</head>\n"
 			   "<body>\n";
	std::string dlString4 = "<h1>Index of " + finalPath + "</h1>\n";
	std::string dlString5 = "<table>\n"
   			   "<tr><th valign=\"top\"><img src=\"/icons/blank.gif\" alt=\"[ICO]\"></th><th><a href=\"" + renderFlag1 + 
			   "\">Name</a></th><th><a href=\"" + renderFlag2 + 
			   "\">Last modified</a></th><th><a href=\"" + renderFlag3 + 
			   "\">Size</a></th><th><a href=\"" + renderFlag1 + "\">Description</a></th></tr>\n"
   			   "<tr><th colspan=\"5\"><hr></th></tr>\n";
	std::string dlParent = "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"" + parentDir + 
		"\">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>\n";


	std::string dlStringEnd = "<tr><th colspan=\"5\"><hr></th></tr>\n"
			     "</table>\n"
			     "</body></html>\n";

	// Write beginning section HTML strings to socket
	write(socket, dlString1.c_str(), dlString1.size());
	write(socket, dlString2.c_str(), dlString2.size());
	write(socket, dlString3.c_str(), dlString3.size());
	write(socket, dlString4.c_str(), dlString4.size());
	write(socket, dlString5.c_str(), dlString5.size());
	// If the parent director is not above http-root-dir/htdocs, write a button to the above directory
	if (includeParent) write(socket, dlParent.c_str(), dlParent.size());

	// Parse files from directory to send to sorting algorithms
	std::vector<fileStruct> fileVector;

	// Open directory handler and dirent struct to traverse directory
	DIR * dir = opendir(finalPath.c_str());
	struct dirent * ent;

	// Loop through each file in directory
	while ((ent = readdir(dir)) != NULL) {
		// Skip file if current file is a hidden file
		if (strncmp(ent->d_name, ".", 1) == 0) {continue;}

		// Create final file path
		std::string file_path = finalPath + "/" + std::string(ent->d_name);

		// Set up stat struct and time struct to fetch information about file
		struct stat file_stat;
		if (stat(file_path.c_str(), &file_stat) == -1) {
			perror("stat");
			exit( -1 );
		}

		// Create float of the size of the files in kilobytes
		float file_size = (float) file_stat.st_size / 1000.0;

		// Create time_struct and use it to construct modification time string
		struct tm * time_struct = localtime(&file_stat.st_mtime);
		std::string file_time = std::to_string(time_struct->tm_year + 1900) + "-" + 
			std::to_string(time_struct->tm_mon + 1) + "-" + 
			std::to_string(time_struct->tm_mday) + " " +
			std::to_string(time_struct->tm_hour) + ":" + 
			std::to_string(time_struct->tm_min);

		// Set file type based off of suffix of the file according to following formats.
		// File type is set to the corresponding icon for that file type in ./http-root-dir/icons/
		std::string file_type = "unknown.gif";
		
		if (strstr(ent->d_name, ".gif") || strstr(ent->d_name, ".png") || strstr(ent->d_name, ".jpg") ||
				strstr(ent->d_name, ".jpeg") || strstr(ent->d_name, ".xbm")) {
			file_type = "image.gif";		
		} else if (strstr(ent->d_name, ".mp4") || strstr(ent->d_name, ".mov")) {
			file_type = "movie.gif";
		} else if (strstr(ent->d_name, ".txt") || strstr(ent->d_name, ".doc") || strstr(ent->d_name, ".docx") || 
				strstr(ent->d_name, ".o") || strstr(ent->d_name, ".c") || strstr(ent->d_name, ".cc") || 
				strstr(ent->d_name, ".html")) {
			file_type = "text.gif";	
		} else if (strstr(ent->d_name, ".mp3") || strstr(ent->d_name, ".wav")) {
			file_type = "audio.gif";	
		} else if (strstr(ent->d_name, ".tar")) {
			file_type = "tar.gif";
		} else if (strstr(ent->d_name, ".out")) {
			file_type = "binary.gif";	
		} else {
			// Test if an entry in the directory is a subdirectory and set file type accordingly,
			// cleaning up residual resources.
			DIR * file_directory_test = opendir(file_path.c_str());
			if (file_directory_test) {
				file_type = "folder.gif";
				file_size = -1;
				closedir(file_directory_test);
			}
		}
		
		// Create instance of fileStruct will all file information and add to fileVector
		fileVector.push_back({file_type, file_path, std::string(ent->d_name), file_time, std::to_string(file_size)});
	}

	// Close directory
	closedir(dir);

	// Sort and output files based on flag given from filevector, calling the respective comparator
	if (flag == "name-A" || flag == "name-D") std::sort(fileVector.begin(), fileVector.end(), comp_name);
	if (flag == "moddate-A" || flag == "moddate-D") std::sort(fileVector.begin(), fileVector.end(), comp_date);
	if (flag == "size-A" || flag == "size-D") std::sort(fileVector.begin(), fileVector.end(), comp_size);

	// Ascending Order Render of file list
	if (flag == "name-A" || flag == "moddate-A" || flag == "size-A" || flag == "none") {
		// Iterate through fileVector for this directory
		for (fileStruct file_entry : fileVector) {
			// Create alt text string based off of file type
			std::string alt = "   ";
			if (file_entry.type == "image.gif") alt = "IMG";
		       	if (file_entry.type == "folder.gif") alt = "DIR";
			if (file_entry.type == "text.gif") alt = "TXT";

			// Create size string to display based on of size is not found
			// or the entry is a directory.
			char size_string_buffer[256];
			std::string size_string = "-";
			if (file_entry.size != "-1" && file_entry.type != "folder.gif") {
				sprintf(size_string_buffer, "%.2f K", std::stof(file_entry.size));
				size_string = std::string(size_string_buffer);
			}

			// Create path link for a tag link, handling case where request doesn't have trailing slash
			std::string path_link = std::string(documentRequested);
			if (documentRequested.back() == '/') path_link += file_entry.name;
			else path_link += '/' + file_entry.name;

			// Construct entry string
			std::string write_string = "<tr><td valign=\"top\"><img src=\"/icons/" + file_entry.type + "\" alt=\"[" + alt +
			       	"]\"></td><td><a href=\"" + path_link + 
				"\">" + file_entry.name + "</a></td><td align=\"right\">" + file_entry.time + 
				"  </td><td align=\"right\">" + size_string + "</td><td>&nbsp;</td></tr>";
			
			// Write entry to socket
			write(socket, write_string.c_str(), write_string.size());
		}
	} else {
		// Descending Order Render of the file list: Reverse iterate through fileVector
		for (std::vector<fileStruct>::reverse_iterator file_entry = fileVector.rbegin(); file_entry != fileVector.rend(); ++file_entry) {
			// Crate alt text string based off of file type
			std::string alt = "   ";
			if (file_entry->type == "image.gif") alt = "IMG";
		       	if (file_entry->type == "folder.gif") alt = "DIR";
			if (file_entry->type == "text.gif") alt = "TXT";

			// Create size string to display based on of size is not found
			// or the entry is a directory.
			char size_string_buffer[256];
			std::string size_string = "-";
			if (file_entry->size != "-1" && file_entry->type != "folder.gif") {
				sprintf(size_string_buffer, "%.2f K", std::stof(file_entry->size));
				size_string = std::string(size_string_buffer);
			}
			
			// Create path link for a tag link, handling case where request doesn't have trailing slash
			std::string path_link = std::string(documentRequested);
			if (documentRequested.back() == '/') path_link += file_entry->name;
			else path_link += '/' + file_entry->name;
	
			// Construct entry string
			std::string write_string = "<tr><td valign=\"top\"><img src=\"/icons/" + file_entry->type + "\" alt=\"[" + alt +
			       	"]\"></td><td><a href=\"" + path_link + 
				"\">" + file_entry->name + "</a></td><td align=\"right\">" + file_entry->time + 
				"  </td><td align=\"right\">" + size_string + "</td><td>&nbsp;</td></tr>";
			
			// Write entry to socket		
			write(socket, write_string.c_str(), write_string.size());

		}
	}

	// Print closing message
	write(socket, dlStringEnd.c_str(), dlStringEnd.size());
}

void loadableModule( int socket, std::string queryString, std::string documentRequested ) {
	
	// Create loadable module request string from root directory (removing /cgi-bin/
	std::string requestStr = "./" + documentRequested.substr(9, std::string::npos);

	// Test if module has already been loaded into memory, and if it has reference it
	void * dl_module = dlopen(requestStr.c_str(), RTLD_LAZY | RTLD_NOLOAD);

	// If module is not yet loaded in, load module into memory
	if (dl_module == NULL) {
		dl_module = dlopen(requestStr.c_str(), RTLD_LAZY);
	}

	// If module was successfully loaded in
	if (dl_module) {
		// Call httprun function from loaded dynamically linked libaray
		httprun_f httprun = (httprun_f) dlsym(dl_module, "httprun");
		// If function was found successfully, write header to socket and run function.
		// If not, write 404 response.
		if (httprun) {
			write(socket, cgiResponse.c_str(), cgiResponse.size());
			httprun(socket, queryString.c_str());
		} else {
			write(socket, notFoundResponse.c_str(), notFoundResponse.size());
			write(socket, "text/plain\r\n\r\n", 14);
			write(socket, errorMessage.c_str(), errorMessage.size());
		}
	// If module not successfully loaded in, write 404 response		
	} else {
		write(socket, notFoundResponse.c_str(), notFoundResponse.size());
		write(socket, "text/plain\r\n\r\n", 14);
		write(socket, errorMessage.c_str(), errorMessage.size());
	}	
}

// Function for finding the number of readable bytes from the post-query file
int getContentLength() {
	// Open file to hard coded post_test to find POST query string
	FILE * fp;
	int count = 0;
	char filename[14] = "post_test.txt";
	char c;

	// Open the file
	fp = fopen(filename, "r");

    	// Check if file exists
    	if (fp == NULL) {
        	fprintf(stderr, "ERROR: No Post File");
        	_exit(0);
   	 }

    	// Extract characters from file
   	 // and store in character c
    	for (c = getc(fp); c != EOF; c = getc(fp)) {
        	// Increment count for this character
        	count = count + 1;
    	}

    	// Close the file
    	fclose(fp);
    
	// Return number of readable bytes
    	return count;
}


void cgiBin ( int socket, std::string finalPath, std::string queryString ) {
	// Find request method based on hard-coded POST query test cgi-bin
	size_t last_slash = finalPath.find_last_of('/');

	std::string requestMethod = "GET";

	if (finalPath.substr(last_slash + 1, std::string::npos) == "post-query") {
		requestMethod = "POST";
	}
	
	// Duplicate stdout and redirect output to socket
	int default_out = dup(1);
	
	dup2(socket, 1);
	close(socket);

	// If post mode is set, duplicate stdin and redirect input to post_test
	int default_in;
	if (requestMethod == "POST") {
		default_in = dup(0);
		int input_fd = open("post_test.txt", O_RDONLY);
		dup2(input_fd, 0);
		close(input_fd);
	}

	// Fork into parent and child process
	int ret = fork();

	if (ret == 0) {	
		// In the child process, set appropriate environmental variables and query strings
		// Note that quertString will be "" if no arguments are passed.
		if (requestMethod == "GET") {
			setenv("REQUEST_METHOD", "GET", 1);
			setenv("QUERY_STRING", queryString.c_str(), 1);
		} else if (requestMethod == "POST") {
			setenv("REQUEST_METHOD", "POST", 1);
			setenv("CONTENT_TYPE", "application/x-www-form-urlencoded", 1);		
			
			// Calculate content length into a const char * to pass to setenv
			std::string num_char_tmp = std::to_string(getContentLength());
    			char const * content_length = num_char_tmp.c_str();
				
			setenv("CONTENT_LENGTH", content_length, 1);
		}
		
		// Write header and create and push elements for argument vector
		write(1, cgiResponse.c_str(), cgiResponse.size());	
		std::vector<char*> arg_vector;
		arg_vector.push_back(const_cast<char*>(finalPath.c_str()));
		arg_vector.push_back(NULL);
		
		// Call execvp on cgi-bin executable and exit if execvp fails
		execvp(finalPath.c_str(), arg_vector.data());
		exit(2);
	}

	// Redirect output (and input if in post mode) to stdout and stdin and clean
	// up residual file descriptors in the parent process.
	dup2(default_out, 1);
	close(default_out);

	if(requestMethod == "POST") {
		dup2(default_in, 0);
		close(default_in);
	} 
}

void statPage( int socket ) {
	// Create time_t struct to hold end of server up time calculation
	time_t endTime;
	time(&endTime);


	// Convert difference between start and end time into human 
	// readable format from start_time (seconds)
	double start_time = difftime(endTime, startTime);

	int start_hours = (int) trunc(start_time / 3600);
	start_time = std::fmod(start_time, 3600);

	int start_min = (int) trunc(start_time / 60);
	start_time = std::fmod(start_time, 60);

	int start_sec = (int) trunc(start_time);
	
	// Write header and content type to socket
	write(socket, foundResponse.c_str(), foundResponse.size());
	write(socket, "text/html\r\n\r\n", 13);
	
	// Create HTML strings to set up stat page based on updated global variables 
	std::string statPage_start = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
				     "<html>\n"
				     "<head>\n"
				     "<title>CS 252 Lab 5 Webserver Implementation - Stats Page</title>\n"
				     "</head>\n"
				     "<body><h1>CS 252 Lab 5 Webserver Implementation - Stats Page</h1>\n"
				     "<h2>Written and Developed by: Elijah Colwill</h2>\n"
				     "<h4>Department of Computer Science: Purdue University - West Lafayette</h4>\n"
				     "<h4>Spring 2022 CS 25200: Systems Programming</h4>\n"
				     "<hr>\n"
				     "<ul>\n";
	std::string statPage_uptime = "<li>Current Server Uptime: " + std::to_string(start_hours) + 
		"hr " + std::to_string(start_min) + "min " + std::to_string(start_sec) + "sec\n";

	std::string statPage_requests = "<li>Number of Requests Since Server Start: " + std::to_string(globalRequest) + "\n";

	std::string statPage_minMax = "<li>Minimum Service Time: " + std::to_string(minTime) + "ms by Request For: " + minRequest + "\n"
				      "<li>Maximum Service Time: " + std::to_string(maxTime) + "ms by Request For: " + maxRequest + "\n";

	std::string statPage_end = "</ul>\n"
				   "</body>\n"
			   	   "</html>";

	// Write updated stat html pages to client
	write(socket, statPage_start.c_str(), statPage_start.size());	
	write(socket, statPage_uptime.c_str(), statPage_uptime.size());
	write(socket, statPage_requests.c_str(), statPage_requests.size());
	write(socket, statPage_minMax.c_str(), statPage_minMax.size());
	write(socket, statPage_end.c_str(), statPage_end.size());		

}

void updateTime(double complete_time, std::string documentRequested) {
	// If this is the first process or new fastest process, update minTime
	// and store the request that is the new minimum.
	if (minTime == -1 || complete_time < minTime) {
		minTime = complete_time;
		minRequest = std::string(documentRequested);
	}

	// If this is the first process or the new slowest process, update maxTiem
	// and store the request that is the new maximum.
	if (maxTime == -1 || complete_time > maxTime) {
		maxTime = complete_time;
		maxRequest = std::string(documentRequested);
	}
}

void processRequest( int socket ) {
	// Buffer used to store the information received by the client
	const int MaxDocument = 2000000;
	char document[MaxDocument + 1];
	int docLength = 0;

	// Initialize time struct
	time_t start_time;
	time(&start_time);	

	// Initialize char to be read to
	unsigned char newChar;

	// Add to request count
	pthread_mutex_lock(&mutex_count);
	globalRequest++;
	pthread_mutex_unlock(&mutex_count);	

	// Read message from client and store in document array. The server will read from
	// the client until <CR><LF><CR><LF> is read
	int n;
	while ( docLength < MaxDocument && 
			(n = read( socket, &newChar, sizeof(newChar))) > 0 ) {
		if (docLength >= 3) {
			if ( document[docLength - 3] == '\r' && document[docLength - 2] == '\n' && 
			  document[docLength - 1] == '\r' && newChar == '\n' ) {
				docLength -= 3;
				break;
			}
		}

		document[docLength] = newChar;
		docLength++;
	
	}

	// Add a null terminator to document
	document[docLength] = '\0';

	// Declare relevant strings to hold request data
	std::string documentRequested;
	std::vector<std::string> headerVector;
	std::string queryString;

	// Parse document request and initialize relevant variables
	char * saveptr;
	std::string delimitor = " \t";
	std::string flag = "none";
	// Strtok to parse token, note the thread save version is used with the anchor 
	// pointer save ptr.
	char * token = strtok_r(document, delimitor.c_str(), &saveptr);
	int tokenCount = 0;
	int error = 0;
	
	// Check if user is authenticated
	int auth = 0;
	
	// Check if command is a cgi-bin command/loadable module
	int cgi_bin = 0;
	int loadable_module = 0;
	
	// Check for passed stat or log page request
	int stat_page = 0;
	int logs_page = 0;
	
	// Add requested document and vector of header information
	while (token) {
		// If this is the first token (space delimitor), check that the request is a GET request
		if (tokenCount == 0 && strcmp(token, "GET") != 0) {
			error = 1;
			break;
		// If this is the second token (space delimitor), get the document request
		} else if (tokenCount == 1) {
			// Create std::string of the unprocessed document request from the strtok output
			std::string unprocessedDR = std::string(token);
			// Check if the document request is a cgi-bin request, stat or logs request, or normal request
			// with or without a query flag. For all of the detailed options, adjust the unprocessedDoc string
			// as necessary to remove queries or place them into query strings. Additionally, set any relevant
			// boolean (int type 0/1) flags depending on the type of request.
			if(unprocessedDR.size() > 9 && unprocessedDR.substr(0, 9) == "/cgi-bin/") {
				cgi_bin = 1;
				size_t query_index = unprocessedDR.find('?', 0);
				if(query_index != std::string::npos && unprocessedDR.size() > query_index + 1) {
					queryString = unprocessedDR.substr(query_index + 1, std::string::npos);
					unprocessedDR = unprocessedDR.substr(0, query_index);	
				} else {
					queryString = "";
				}

				if(strstr(unprocessedDR.c_str(), ".so")) {loadable_module = 1;}
			} else if ((unprocessedDR.size() == 5 && unprocessedDR == "/stat") || 
				       (unprocessedDR.size() == 6 && unprocessedDR == "/stat/")) {	
				stat_page = 1;
			} else if ((unprocessedDR.size() == 5 && unprocessedDR == "/logs") || 
				       (unprocessedDR.size() == 6 && unprocessedDR == "/logs/")) {	
				logs_page = 1;
			} else {
				// Will catch the sorting queries that come with the normal directory request and set 
				// accordingly for later use in the listDirectory function/
				size_t query_index = unprocessedDR.find('?', 0);
				if (query_index != std::string::npos && unprocessedDR.size() > query_index + 1) {
					std::string query = unprocessedDR.substr(query_index + 1, std::string::npos);
					if (query == "C=N;O=A") flag = "name-A";
					else if (query == "C=N;O=D") flag = "name-D";
					else if (query == "C=M;O=A") flag = "moddate-A";
					else if (query == "C=M;O=D") flag = "moddate-D";
					else if (query == "C=S;O=A") flag = "size-A";
					else if (query == "C=S;O=D") flag = "size-D";
					unprocessedDR = unprocessedDR.substr(0, query_index);	
				}
			}
			
			// After document request has been sorted into its request type and queries have been
			// removed from the request and processed, update the function-wide document request variable
			// and change the strtok delimitor to catch the header information indivdually.	
			documentRequested = std::string(unprocessedDR);
			delimitor = "\r\n";
		} else if (tokenCount > 2) {
			// For each token after HTTP1/1 (\r\n delimitor), add each header information section
			// to the headerVector.
			std::string str_token = std::string(token);
			// Before adding, check if the header information field is the correct authentication field
			if (str_token == "Authorization: Basic ZWxpamFoOmhlbGxvd29ybGQ=") {
				auth = 1;
			}
			headerVector.push_back(str_token + "\r\n");
		}
		// Set token to the next instance in strtok, and increment token counter variable
		token = strtok_r(NULL, delimitor.c_str(), &saveptr);
		tokenCount++;
	}

	// Create log string using the client IP address global variable set in the main function in the
	// while loop after the accept call, and the document requested.
	std::string newLog = "Host: " + std::string(clientIP) + " Request: " + documentRequested + "\n";

	// Open the logs file that is preserved across runs of the server and 
	// write the new log entry into the file, closing the file descriptor
	// afterward.
	int logs_fd = open(logs_path, O_WRONLY | O_APPEND, 0666);
	write(logs_fd, newLog.c_str(), newLog.size());
	close(logs_fd);

	// If authenticated and the request type is a loadable module, call the handler function
	if (loadable_module && auth) {
		loadableModule(socket, queryString, documentRequested);
	}

	// If authenticated and the request type is a stat page request, call the handler function
	if (stat_page && auth) {
		statPage(socket);
	}	

	// Check for invlaid request or completed special request type from above
	if (error || !auth || loadable_module || stat_page) {

		// If not authenticated, write the appropriate response to the client
		if (!auth) write(socket, authMessage.c_str(), authMessage.size());
		
		// Update end time for this specific request to update min and max if 
		// possible through updateTime funtion.
		time_t end_time;
		time(&end_time);
		double request_time = difftime(end_time, start_time);
		updateTime(request_time, documentRequested);

		// Close Socket
		shutdown(socket, SHUT_RDWR);
		close( socket );
		headerVector.clear();

		return;
	}

	// If request type is a log type, set to the specific file storing log information
	if (logs_page) {
		documentRequested = "/logs.txt";
	}



	// Initialize variables to handle opening requested document
	int found = 1;
	int directory = 0;
	char pwd[256];
	getcwd(pwd, 256);
	std::string finalPath;
	std::string docType = "text/plain\r\n\r\n";

	// Store correct document path and file type
	if (documentRequested == "/" || documentRequested.size() == 0) {
		finalPath = std::string(pwd) +  "/http-root-dir/htdocs/index.html";
		docType = "text/html\r\n"; 
	} else {
		int temp_fd;

		// Convert passed path into real final path
		if (documentRequested.size() > 7 && documentRequested.substr(0, 7) == "/icons/" || cgi_bin) {
			finalPath = std::string(pwd) + "/http-root-dir" + documentRequested;
		} else {
			finalPath = std::string(pwd) + "/http-root-dir/htdocs" + documentRequested;	
		}

		// Convert finalPath to an absolute path by calling realpath, which resolves anything like /../ in a path
		char * absolute = realpath(finalPath.c_str(), NULL);
		if (absolute) {
			finalPath = std::string(absolute);
			free(absolute);
		}	
			
		// Check that file requested is not above /http-root-dir/ and set the found variable to false if so
		// to write a 404.
		if (finalPath.substr(0, strlen(pwd) + 15) == (std::string(pwd) + "/http-root-dir/")) {
			// Create a directory handle to test if the file is a directory, closing the handler
			// if it is.
			DIR * directoryTest = opendir(finalPath.c_str());
			if (directoryTest) {
				directory = 1;
				docType = "text/html\r\n";
				closedir(directoryTest);
			} else if ((temp_fd = open(finalPath.c_str(), O_RDONLY)) != -1) {
				// If a matching file for the document request was found, set the appropriate
				// MIME type based on the suffix of the document. Server supports a handful of 
				// types as shown in the following if statments.
				size_t dot_idx = documentRequested.find_last_of('.');
				if (documentRequested.substr(dot_idx + 1) == "html") {
					docType = "text/html\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "gif") {
					docType = "image/gif\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "jpeg") {
					docType = "image/jpeg\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "ico") {
					docType = "image/x-icon\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "png") {
					docType = "image/png\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "xbm") {	
					docType = "image/x-bitmap\r\n";
				} else if (documentRequested.substr(dot_idx + 1) == "svg") { 
					docType = "image/svg+xml\r\n";
				} else {
					docType = "text/plain\r\n";
				}
				// If a file was found, close the created file descriptor
				close(temp_fd);
			} else {found = 0;}
		} else {found = 0;}
	}

	// Write a response to the client depending on if a file was found or not 
	if (found) {
		// If the file was found and not a cgi-bin request tpye,e write the document MIME
		// type set above and appropriate header response to the client. Iterate and
		// write the contents of the headerVector to the client and an ending \r\n.	
		if (!cgi_bin) {
			write(socket, foundResponse.c_str(), foundResponse.size());
			write(socket, docType.c_str(), docType.size());
		
			for (std::string entry : headerVector) {
				write(socket, entry.c_str(), entry.size());
			}

			write(socket, "\r\n", 2);
		}
		
		// If the file was a normal request type, read the file into a 256 byte
		// buffer in chuncks and write all bytes in the document to the client.
		if (!directory && !cgi_bin) {
			char fileBuffer[256];
			FILE* file = fopen(finalPath.c_str(), "rb");
			int read = 0;
			while (read = fread(fileBuffer, sizeof(char), 256, file)) {
				if (read < 1) break;
				write(socket, fileBuffer, read);
			}
			// Close the opened file
			fclose(file);
		} else if (directory && !cgi_bin) {
			// If the document is a directory, call the handler function
			listDirectory(socket, finalPath, documentRequested, flag);
		} else if (cgi_bin) {
			// If the document is a cgi-bin request type, call the handler function
			cgiBin(socket, finalPath, queryString);
		}

	} else {
		// If a matching document was not found or a related error was indicated (such
		// as the request being above http-root-dir), write the 404 response to the client.
		// Doctype will be set to the default text/plain\r\n\r\n and contain the header ending.
		write(socket, notFoundResponse.c_str(), notFoundResponse.size());
		write(socket, docType.c_str(), docType.size());
		write(socket, errorMessage.c_str(), errorMessage.size());
	}

	// Close Socket
	headerVector.clear();
	shutdown(socket, SHUT_RDWR);
	close( socket );

	// Update the end time of the request and sent time to updateTime
	// to update the min and max response time, if possible.
	time_t end_time;
	time(&end_time);
	double request_time = difftime(end_time, start_time);
	updateTime(request_time, documentRequested);
	
}
