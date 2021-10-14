#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "threadpool.h"


#define MAX_SIZE 4000
#define OK 200
#define OK_FILE 201
#define OK_FOLDER 202
#define FOUND 302
#define BAD_REQUEST 400
#define FORBIDDEN 403
#define NOT_FOUND 404
#define INTERNAL_ERROR 500
#define NOT_SUPPORTED 501
#define WRITE_ERROR 0
#define DEFAULT_PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
char *get_mime_type(char *name)
{
char *ext = strrchr(name, '.');
if (!ext) return NULL;
if (!strcmp(ext, ".html") || !strcmp(ext, ".htm") || !strcmp(ext, ".txt")) 
		return "text/html";
if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
if (strcmp(ext, ".gif") == 0) return "image/gif";
if (strcmp(ext, ".png") == 0) return "image/png";
if (strcmp(ext, ".css") == 0) return "text/css";
if (strcmp(ext, ".au") == 0) return "audio/basic";
if (strcmp(ext, ".wav") == 0) return "audio/wav";
if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
return NULL;
}
int insertArguments(const char** argv,int* pool_size, int* port, int* max_requests)
{
	if(strcmp(argv[2], "0")==0)		
		return -1;
    *pool_size=atoi(argv[2]);
	if(strcmp(argv[1], "0")==0)
		return -1;
    *port=atoi(argv[1]);
	if(strcmp(argv[3], "0")==0)
		return -1;
    *max_requests=atoi(argv[3]);
    if(*pool_size<0 || *pool_size > MAXT_IN_POOL || *port<0 || *max_requests<1)
        return -1;
    return 0;
}
int write_to_socket(int socket_fd, char *msg_to_send, int bytes_to_write)
{
	int bytes_written = 1;
	char *yet_to_send = msg_to_send;
	while (bytes_to_write > 0)
	{
		bytes_written = write(socket_fd, yet_to_send, bytes_to_write);
		if (bytes_written < 0) 
			return -1;
			
		bytes_to_write -= bytes_written;
		yet_to_send += bytes_written;
	}
	return 0;
}

char *code_to_string(int code)
{
	if (code == 200) //no error
		return "200 OK";
	else if (code == 302)
		return "302 Found";
	else if (code == 400)
		return "400 Bad Request";
	else if (code == 403)
		return "403 Forbidden";
	else if (code == 404)
		return "404 Not Found";
	else if (code == 500)
		return "500 Internal Server Error";
	else //equal 501
		return "501 Not supported";
}
int chopHeaderIntoBits(char* msg, char* protocol, char* path, int* pathLen, int* codeType)
{

    if(msg==NULL)
    {
        *codeType = BAD_REQUEST;
		return -1;
    }
	
	int j=0;
	while(msg[j]!='\0' && msg[j]==' ')
		j++;
	int startInd=j;
	int num_of_tokens=0; //GET    /      HTTP/1.1
	while(msg[j]!='\0')
	{
		while(msg[j]!='\0'  && msg[j]!=' ')
		{
			j++;
		}
		num_of_tokens++;
		if(msg[j]==' ')
		{
			while(msg[j]!='\0' && msg[j]==' ')
				j++;
		}
	}
	if(num_of_tokens!=3)
	{
		*codeType=BAD_REQUEST;
		return -1;
	}
	if(msg[startInd]!='G' || msg[startInd+1]!='E' || msg[startInd+2]!='T' || msg[startInd+3]!=' ')
    {
        *codeType = NOT_SUPPORTED;
        return -1;
    }
	char *protocol1 = strstr(msg, "HTTP/1.1");
	char *protocol0 = strstr(msg, "HTTP/1.0");
	
	if ((protocol0==NULL && protocol1==NULL) || (protocol0!=NULL && protocol1!=NULL))
	{
		*codeType = BAD_REQUEST;
		return -1;
	}

    if(protocol1!=NULL)
    {
        strcpy(protocol, protocol1);
        if (protocol1[8] != '\0') //not the last argument
		{
			*codeType = BAD_REQUEST;
			return -1;
		}
    }

    if(protocol0!=NULL)
    {
        strcpy(protocol, protocol0);
        if (protocol0[8] != '\0') //not the last argument
		{
			*codeType = BAD_REQUEST;
			return -1;
		}
    }

	

    //parse the path
    int i=startInd+3;
    *pathLen=0;
    while(msg[i]==' ') //get to the path token
	{
        i++;
	}
	if(msg[i]!='/')
	{
		*codeType = BAD_REQUEST;
		return -1;
	}
    while(msg[i]!=' ')
    {
        path[*pathLen]=msg[i];
        (*pathLen)++;
        i++;
    }
    int n = strlen(msg) - (strlen(protocol) + 1);
    char path_buff[4000] = { 0 };
	
	if (strstr(path, "%20")) 
	{
		int i, j;
		for (i = 0, j = 0; i < n; i++, j++)
		{	//compare the addresses
			if (!strncmp(&(path[i]), "%20", 3))
			{
				i += 2;
				path_buff[j] = ' ';
			}
			else
				path_buff[j] = path[i];
		}
		bzero(path, strlen(path));
		strcpy(path, path_buff);
		bzero(path_buff, strlen(path_buff));
	}
	
	//setting up the current folder as the root directory
	sprintf(path_buff, ".%s", path);
	sprintf(path, "%s", path_buff);
    
    return 0;

}
void ErrorMsg(int socket_fd,char*  path,char* protocol,char* timebuf, int errorCode)
{
	char response[MAX_SIZE];
	char header[MAX_SIZE];
	char html[MAX_SIZE];
	memset(response, '\0', MAX_SIZE);
	memset(header, '\0', MAX_SIZE);
	memset(html, '\0', MAX_SIZE);
	char* code =code_to_string(errorCode);
	sprintf(header, "%s %s\r\nServer: webserver/1.", protocol, code);
	if(protocol[7]=='0')
		strcat(header, "0");
	else 
		strcat(header, "1");
	sprintf(header+strlen(header), "\r\nDate: %s\r\n", timebuf);
	sprintf(header+strlen(header), "%s%s%sContent-Type: %s\r\n", errorCode == FOUND? "Location: " : "", errorCode == FOUND? (path + 1) : "",
		errorCode == FOUND? "/\r\n" : "", "text/html");

	strcat(html, "<HTML><HEAD><TITLE>");
	strcat(html, code);
	strcat(html, "</TITLE></HEAD>\r\n<BODY><H4>");
	if(errorCode==INTERNAL_ERROR)
		strcat(html, "500 Internal Server Error");
	else
		strcat(html, code);
	strcat(html, "</H4>\r\n");
	if(errorCode==BAD_REQUEST)
		strcat(html, "Bad Request.");
	else if(errorCode==FORBIDDEN)
		strcat(html, "Access denied.");
	else if(errorCode==FOUND)
		strcat(html, "Directories must end with a slash.");
	else if(errorCode==INTERNAL_ERROR)
		strcat(html, "Some server side error.");
	else if(errorCode==NOT_FOUND)
		strcat(html, "File not found.");
	else if(errorCode==NOT_SUPPORTED)
		strcat(html, "Method is not supported.");
	strcat(html, "\r\n</BODY></HTML>\r\n\r\n");

	sprintf(header + strlen(header),
		"Content-Length: %lu\r\nConnection: close\r\n\r\n", strlen(html));
	sprintf(response, "%s%s", header, html);
	
	if (write_to_socket(socket_fd, response, strlen(response)) < 0)
		perror("ERROR on write");
	
	
}
void setError1(int* codeType)
{
	if (errno == EACCES)
		*codeType = FORBIDDEN;
	if (errno == ENOENT) 
		*codeType = NOT_FOUND;
	else
		*codeType = INTERNAL_ERROR;
}
void setError2(int* codeType)
{
	if (errno == EACCES) 
		*codeType = FORBIDDEN;
	else //other errors will be treated as syetem errors
		*codeType = INTERNAL_ERROR;
}
int folderResponse(char *path, int* pathLen, char *protocol, int *codeType, char *timebuf, int socket_fd)
{
	DIR* folder= opendir(path);
	if (folder==NULL)
	{ 
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	
	struct dirent *file = readdir(folder);
	int counter = 0;
	while (file!=NULL) 
	{
		if (strcmp (file->d_name, ".")) 
			counter++;
		file = readdir(folder);
	}
	closedir(folder);
	
	struct stat fs;
	if (lstat(path, &fs) < 0)
	{
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	
	folder = opendir(path); 
	if (folder==NULL)
	{ 
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	char header[MAX_SIZE];
	memset(header, '\0', MAX_SIZE);
	
	char folder_time_buf[128];
	memset(folder_time_buf, '\0', 128);
	strftime(folder_time_buf, sizeof(folder_time_buf), RFC1123FMT, gmtime(&fs.st_mtime));
	
	char *html_code = (char*)calloc(counter * 500, sizeof(char));
	//memset(html_code, '\0', MAX_SIZE);
	strcat(html_code, "<HTML>\r\n<HEAD><TITLE> Index of ");
	strcat(html_code, path+1);
	strcat(html_code, "</TITLE></HEAD>\r\n<BODY>\r\n<H4>Index of ");
	strcat(html_code, path+1);
	strcat(html_code, "</H4>\r\n");
	strcat(html_code, "<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n");
	 // finished prepearing headers, now we go thorugh folder
	
	file=readdir(folder);
	char file_time_buf[128];
	memset(file_time_buf, '\0', 128);
	while(file!=NULL)
	{
		
		if (strcmp (file->d_name, ".")==0) //ignore the "." 
		{
			file = readdir(folder);
			continue;
		}
		
		strftime(file_time_buf, sizeof(file_time_buf), RFC1123FMT, gmtime(&fs.st_mtime));
		
		strcat(path, file->d_name);
		//get the current file information
		if (lstat(path, &fs) < 0)
		{
			setError2(codeType);
			return -1;
		}
		
		strcat(html_code, "<tr><td><A HREF=\"");
		strcat(html_code, file->d_name);
		strcat(html_code, "\">");
		strcat(html_code, file->d_name);
		strcat(html_code, "</A></td><td>");
		strcat(html_code, file_time_buf);
		strcat(html_code, "</td><td>");
		
		if(S_ISREG(fs.st_mode))
			sprintf(html_code + strlen(html_code), "%lu", fs.st_size);
		
		
		path[strlen(path) - strlen(file->d_name)] = '\0';
		
		file = readdir(folder);
		
		strcat(html_code, "</td></tr>\r\n");
		
	}
	strcat(html_code, "</table>\r\n<HR>\r\n<ADDRESS>webserver/1.");
	if(protocol[7]=='1')
		strcat(html_code, "1");
	if(protocol[7]=='0')
		strcat(html_code, "0");
	strcat(html_code, "</ADDRESS>\r\n</HR>\r\n</BODY></HTML>\r\n\r\n");
	
	sprintf(header+strlen(header), "%s %s\r\nServer: webserver/1.", protocol, code_to_string(OK));
	if(protocol[7]=='0')
		strcat(header, "0");
	else 
		strcat(header, "1");
	sprintf(header+strlen(header), "\r\nDate: %s\r\n", timebuf);
	strcat(header, "Content-Type: text/html\r\n");
	

	
	
	sprintf(header+strlen(header), "Content-Length: %lu", strlen(html_code));
	
	strcat(header, "\r\nLast-Modified: ");
	strcat(header, folder_time_buf);
	strcat(header, "\r\nConnection: close\r\n\r\n");
	
	char *response = (char*)malloc(strlen(html_code) * 500* sizeof(char));
	if (response==NULL)
	{
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	memset(response, '\0', strlen(html_code) * 500);
	
	//sprintf(response, "%s%s", header, html_code);
	strcat(response, header);
	strcat(response, html_code);
	if (write_to_socket(socket_fd, response, strlen(response)) < 0)
	{
		closedir(folder);
		free(response);
		free(html_code);
		*codeType = WRITE_ERROR;
		return -1; 
	}
	closedir(folder);
	free(html_code);
	free(response);
	return 0;
}
int fileResponse(char *path, int* pathLen, char *protocol, int *codeType,  char *timebuf, int socket_fd)
{
	//open the file with read operation
	int file_fd = open(path, O_RDONLY, S_IRUSR);
	if(file_fd<0)
	{
		setError2(codeType);
		return -1;
	}
	struct stat fs;
	if (lstat(path, &fs) < 0)
	{
		close(file_fd);
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	
	*pathLen=strlen(path);
	int i=*pathLen;
    char* fileName =NULL;
    while(path[i]!='/' && i>=0)
    {
        fileName = path+i;
        i--;
    }
	/*
	path[i] = '\0'; //cut the file name from the path
	DIR* folder = opendir(path); //we already validated
	if (folder==NULL)
	{ 
		close(file_fd);
		*codeType = INTERNAL_ERROR;
		return -1;
	}
	path[i] = '/'; //get the file back to the path
	*/
	char *type = get_mime_type(fileName);
	int unkownEnd=0;
	if (type==NULL)
		unkownEnd=1;


	char response[MAX_SIZE];
	memset(response, '\0', MAX_SIZE);
	sprintf(response, "%s %s\r\nServer: webserver/1.", protocol, code_to_string(OK));
	if(protocol[7]=='0')
		strcat(response, "0");
	else 
		strcat(response, "1");
	sprintf(response+strlen(response), "\r\nDate: %s\r\n", timebuf);
	if(unkownEnd==0)
	{
		strcat(response, "Content-Type: ");
	
		strcat(response, type);
	}
	char file_time_buf[128];
	memset(file_time_buf, '\0', 128);
	strftime(file_time_buf, sizeof(file_time_buf), RFC1123FMT, gmtime(&fs.st_mtime));
	if(unkownEnd==0)
		sprintf(response+strlen(response), "\r\nContent-length: %lu", fs.st_size);
	
	strcat(response, "\r\nLast-Modified: ");
	strcat(response, timebuf);
	strcat(response, "\r\nConnection: close\r\n\r\n");
	
	if (write_to_socket(socket_fd, response, strlen(response)) < 0)
	{
		close(file_fd);
		*codeType = WRITE_ERROR;
		perror("ERROR on write");		
		return -1; 
	}
	char buf[MAX_SIZE];
	memset(buf, '\0', MAX_SIZE);
	while (1)
	{
		
		int rc = read(file_fd, buf, sizeof(buf));
		
		if (rc > 0) 
		{	
			if (write_to_socket(socket_fd, buf, rc) < 0)
			{
				*codeType = WRITE_ERROR;
				close(file_fd);
				perror("ERROR on write");
				return -1; 
			}
		}
		else if (rc == 0) 
			break;
		else
		{
			close(file_fd); 
			*codeType = INTERNAL_ERROR; 
			perror("ERROR on read");			
			return -1;
		}
	}
	close(file_fd);
	write (socket_fd, "\r\n\r\n", 4);
	return 0;
	
}
int validatePath(int* codeType, char* path, int* pathLen)
{
	if(path==NULL)
	{
		*codeType = BAD_REQUEST;
		return -1;
	}
	struct stat fs ;
	if(lstat(path, &fs)<0) // file not valid
	{
		setError1(codeType);
		return -1;
	}
	if (S_ISREG(fs.st_mode))
	{
		*codeType = OK_FILE;
		return 0;
	}
	else if(S_ISDIR(fs.st_mode))
	{
		int pathLength=strlen(path);
		if(path[pathLength-1] != '/') // directory must end with /
		{
			*codeType = FOUND;
			return -1;
		}
		else // check if index.html is in folder
		{
			int found=0;
			DIR* dir=opendir(path);
			if(dir==NULL)
			{
				setError2(codeType);
				return -1;
			}
			struct dirent* file = readdir(dir);
			while(file!=NULL)
			{
				if(strcmp("index.html", file->d_name)==0)
				{
					strcat(path, "index.html");
					if(lstat(path, &fs)<0)
					{
						setError2(codeType);
						return -1;
					}
					*codeType = OK_FILE;
					closedir(dir);
					found=1;
					return 0;
				}
				file = readdir(dir);
			}
			if(found==0)
			{
				closedir(dir);
				*codeType = OK_FOLDER;
			}
		}
		
		
	}
	else //the path exists but it is not a dir nor regualr file
	{
			*codeType = FORBIDDEN;
			return -1;
	}
	return 0;
}
int dispatch_function(void* arg)
{
    int socket_fd = *((int*)(arg));
    int codeType=0;
    char msg[MAX_SIZE];
    memset(msg, '\0', MAX_SIZE);
    char path[MAX_SIZE];
    int pathLen=0;
    memset(path, '\0', MAX_SIZE);
    char protocol[9] = "HTTP/1.0"; // HTTP/1.1 + /0
    //memset(protocol, '\0', 9);

    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));   

    int rc =read (socket_fd, msg, MAX_SIZE); // read message
    if(rc<0)
    {
        perror("ERROR on read");
        close(socket_fd);
        return -1;
    }
    int msgLen=strlen(msg);
    if(msgLen==0)
    {
        close(socket_fd);
        return -1;
    }

    char* checkHeader = strstr(msg, "\r\n");
    if(checkHeader==NULL) // no occurences were found
    {
        ErrorMsg(socket_fd, NULL, protocol, timebuf, BAD_REQUEST);
		close(socket_fd);
		return -1;
    }

    checkHeader[0]=0; // marks the end of the request

    int suc=chopHeaderIntoBits(msg, protocol, path,&pathLen , &codeType); // parse into protocol, path, and put code type in
    if(suc<0)
    {
        ErrorMsg(socket_fd, path, protocol, timebuf, codeType);
		close(socket_fd);
		return -1;
    }
    suc= validatePath(&codeType, path, &pathLen);
    if(suc<0)
    {
        ErrorMsg(socket_fd, path, protocol, timebuf, codeType);
		close(socket_fd);
		return -1;
    }
    if(codeType==OK_FILE)
    {
        if(fileResponse(path, &pathLen, protocol, &codeType,  timebuf, socket_fd)<0)
		{
        //if a perror accoured we don't want 
		if (codeType != 0)
			ErrorMsg(socket_fd, path, protocol,timebuf, codeType);
		close(socket_fd);
		return -1;
		}
    }
    if(codeType==OK_FOLDER)
    {
        if(folderResponse(path,&pathLen, protocol, &codeType , timebuf, socket_fd)<0)
		{
        //if a perror accoured we don't want 
		if (codeType != 0)
			ErrorMsg(socket_fd, path, protocol,timebuf, codeType);
		close(socket_fd);
		return -1;
		}
    }

    close(socket_fd);
    
    return 0;

}




int main(int argc, char const *argv[])
{
	struct sockaddr_in serv_addr, client_addr;
    int* client_socket;
    int sockfd, new_sockfd;
    int port, pool_size, max_requests;
    int count=0;
    
    
    if(argc!=4) // not enough parameters
    {
        printf("Usage: server <port> <pool-size> <max-requests-number>\n");
		exit(1);
    }

	if (insertArguments(argv,&pool_size,  &port, &max_requests) < 0)
	{
		printf("Usage: server <port> <poolsize>\n");
		exit(EXIT_FAILURE);
	}
    
    

    serv_addr.sin_family=PF_INET;
    serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_addr.sin_port=htons(port);
	socklen_t socklen = sizeof(struct sockaddr_in);
        
	

    threadpool *pool = create_threadpool(pool_size);
	if (pool==NULL) 
	{
		perror("pool");
		exit(EXIT_FAILURE);
	}

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
    {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }
    
    if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))<0)
    {
        perror("ERROR on binding");
		destroy_threadpool(pool);
        close(sockfd);
        exit(EXIT_FAILURE);
    };
    
    listen(sockfd, 5);
    
    while(count<max_requests)
    {
        bzero((char*)&client_addr, sizeof(struct sockaddr_in)); 
        new_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &socklen);
        if(new_sockfd < 0)
            perror("ERROR opening new socket\n");
        else
        {
            client_socket = (int*)malloc(1* sizeof(int));
			if (client_socket==NULL)
				perror("ERROR allocating memory");

			else
			{
				memset(client_socket, '\0', 1* sizeof(int));
			    *client_socket = new_sockfd;
			    dispatch(pool, dispatch_function, (void*)client_socket);
			    
				free(client_socket);
				count++;
            }
            
                
        }
    }
	destroy_threadpool(pool);
    close(sockfd);
    return 0;
}
