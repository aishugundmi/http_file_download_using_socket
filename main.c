#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netdb.h>

#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

//return errors
#define	DNS_RESOLVE_ERROR				-1
#define	SOCKET_CREATION_ERROR			-2
#define	SOCKET_CONNECTION_ERROR			-3
#define	SEND_ERROR						-4
#define MAX_REDIRECTION_ATTEMPT_ERROR 	-5

//switch case states
#define	EXTRACT_HOST_PATH				0
#define	SOCKET_CREATION					1
#define	GET_REQUEST						2
#define	RECEIVE_HEADER					3
#define	GET_STATUS_CONTENTLENGTH		4
#define	RECEIVE_DATA					5
#define	SAVE_TO_FILE					6


int download_url_to_file(char *url, char* file_name)
{
	char url_buf[250];
	uint8_t state = 0;
	char hostname[250], path[250];
	char *sub;
	char ip[100];
	struct hostent *he;
	struct in_addr **addr_list;
	int socket_desc;
	struct sockaddr_in server;
	char tx_buf[1024];
	char* server_reply;
	int received = 0;
	char header_buf[4096];
	int response = 0;
	int content_length = 0;
	char redirected_url[250];
	int written_bytes = 0;
	int redirection_attempt_count = 0;

	strcpy(url_buf, url);

	while(1){
		switch(state){
			/*************************************** extract host & path ******************************************/
			case EXTRACT_HOST_PATH:
				if(redirection_attempt_count > 3){
					printf("max redirection attempts reached!\n");
					return MAX_REDIRECTION_ATTEMPT_ERROR;
				}
				sub = strstr(url_buf, "://");
				sub += 3;
				strcpy(hostname, sub);
				sub = strstr(sub, "/");
				if(sub){
					int len = strlen(hostname)- strlen(sub);
					hostname[len] = '\0';
					sub++;
					strcpy(path, sub);
				}else{
					strcpy(path, "/");
				}

				/*************************************** get address ****************************************/
				if ( (he = gethostbyname( hostname ) ) == NULL)
				{
					printf("unable to do dns resolve!\n");
					return DNS_RESOLVE_ERROR;
				}
				//Cast the h_addr_list to in_addr , since h_addr_list also has the ip address in long format only
				addr_list = (struct in_addr **) he->h_addr_list;

				for(int i = 0; addr_list[i] != NULL; i++)
				{
					strcpy(ip , inet_ntoa(*addr_list[i]) );
				}
				printf("%s resolved to : %s\n" , hostname , ip);
				state = 1;
			break;

			/************************************ socket creation *********************************************/
			case SOCKET_CREATION:
				memset(tx_buf, 0, sizeof(tx_buf));
				//Create socket
				socket_desc = socket(AF_INET , SOCK_STREAM , 0);
				if (socket_desc == -1)
				{
					printf("Could not create socket!\n");
					return SOCKET_CREATION_ERROR;
				}
				server.sin_addr.s_addr = inet_addr(ip);
				server.sin_family = AF_INET;
				server.sin_port = htons( 80 );
				bzero(&(server.sin_zero), 8);

				//Connect to remote server
				if (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0)
				{
					printf("connect error!\n");
					return SOCKET_CONNECTION_ERROR;
				}
				puts("Connected\n");
				state = 2;
			break;

			/******************************************** get request ********************************************/
			case GET_REQUEST:
				sprintf(tx_buf, "GET /%s HTTP/1.1\r\nHost: %s\r\n", path, hostname);
			//	strcat(message, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36\r\n");
			//	strcat(message, "Accept: text/html\r\n");
			//	strcat(message, "Accept-Language: en-US,en;q=0.9\r\n");
				strcat(tx_buf, "\r\n");

				if( send(socket_desc , tx_buf , strlen(tx_buf) , 0) < 0)
				{
					puts("Send failed!\n");
					return MAX_REDIRECTION_ATTEMPT_ERROR;
				}
				printf("\n request = %s\n", tx_buf);
				state = 3;
			break;

			/******************************************* receive header *******************************************/
			case RECEIVE_HEADER:
				received = 0;
				memset(header_buf, 0, sizeof(header_buf));
				while(1) {
					int ret = recv(socket_desc, header_buf + received, 1, 0);
					if(strstr(header_buf, "\r\n\r\n"))
						break;
					if(ret <= 0)
						break;
					received += ret;
				}
			    printf("header received = \n %s\n", header_buf);
			    state = 4;
			break;

			/************************************ get status and content length ***********************************/
			case GET_STATUS_CONTENTLENGTH:
				response = 0;
				if(strncmp(header_buf, "HTTP", strlen("HTTP")) == 0){
					char *s = strstr(header_buf, " ");
					if(s){
						s++;
						char *e = strstr(s, " ");
						if(e){
							char resp_str[5];
							memset(resp_str, 0, sizeof(resp_str));
							if(e-s < sizeof(resp_str)){
								memcpy(resp_str, s, e-s);
								response = atoi(resp_str);
							}
						}
					}
				}

				char *c = strstr(header_buf, "\r\nContent-Length: ");
				if(c){
					sscanf(c, "\r\nContent-Length: %d\r\n", &content_length);
				}

				if(response == 200)
					state = 5;
				else if(response == 301){

					char *d = strstr(header_buf, "\r\nLOCATION: ");
					if(d){
						d += strlen("\r\nLOCATION: ");
						sscanf(d, "%s\r\n", redirected_url);
						printf("redirection = %s\n", redirected_url);
					}
					strcpy(url_buf, redirected_url);
					close(socket_desc);
					redirection_attempt_count++;
					state = 0;
				}
			break;

			/*************************************** receive data ********************************************/
			case RECEIVE_DATA:
			    received = 0;
			    printf("content_length = %d\n", content_length);
			    server_reply = malloc(content_length);
			    memset(server_reply, 0, sizeof(server_reply));

				while(received < content_length) {
					int ret = recv(socket_desc, server_reply + received, content_length - received, 0);
					if(ret <= 0)
						break;
					received += ret;
					printf("received bytes = %d, percentage = %d%%\n", received, (received*100)/content_length);
				}
				state = 6;
			break;

			/**************************************  save to file **********************************************/
			case SAVE_TO_FILE:
				written_bytes = 0;
				FILE* fd = fopen(file_name, "wb");
				while(written_bytes < content_length){
					int ret = fwrite(server_reply + written_bytes, 1, content_length - written_bytes, fd);
					written_bytes += ret;
					printf("Bytes written: %d from %d\n", written_bytes, content_length);
				}
				fclose(fd);

				close(socket_desc);
				free(server_reply);
				printf("\n\nDone.\n\n");
				return 0;
			break;

			default:
			break;
		}
	}
}


int main(int argc , char *argv[])
{
	char url_hp[250], file_name[50];
	char hostname[250], path[250];
	char *sub;

	if(argc == 1){
		printf("\nerror! no url passed!\n");
		printf("usage:\n%s url\n", argv[0]);
		return 0;
	}
	if(argc >=2 )
	{
		printf("\nThe command line url passed: %s\n", argv[1]);
		strcpy(url_hp, argv[1]);
		if(argc == 3)
			strcpy(file_name, argv[2]);
		else
			strcpy(file_name, "file.txt");
	}

	download_url_to_file(url_hp, file_name);

	return 0;
}























