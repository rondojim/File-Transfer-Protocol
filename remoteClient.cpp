#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

// function for reading N bytes exectly

int myread_s(int fd, char *buf, int count){
    int totalbytes = 0;
    while(totalbytes < count){
        int read_bytes = read(fd, buf + totalbytes, (count - totalbytes));
        if(read_bytes == 0) return 0;
        totalbytes += read_bytes;
        if(read_bytes == -1) return -1;
    }
    return 0;
}

// function for writing N bytes exactly

int mywrite_s(int fd, char *buf, int count){
    int totalbytes = 0;
    while(totalbytes < count){
        int read_bytes = write(fd, buf + totalbytes, (count - totalbytes));
        totalbytes += read_bytes;
        if(read_bytes == -1) return -1;
    }
    return 0;
}

#define MAX_BUF 4096

struct dir_path_name{
    char dir_path[MAX_BUF];
    char name[MAX_BUF];
    char fullpath[MAX_BUF];
    uint32_t client_socket;
    uint32_t filebytes;
    uint32_t numofFiles;
    uint32_t send_block_size;

    dir_path_name& operator=(const dir_path_name& rhs){
        strcpy(dir_path, rhs.dir_path);
        strcpy(name, rhs.name);
        client_socket = rhs.client_socket;
        return *this;
    }
};

char *ip;
int server_port;
char directory[MAX_BUF];

// helper function to assign above values
void get_arguments(int argc, char *argv[]){
    if(argc != 7){
        perror("Invalid arguments\n");
        exit(0);
    }

    for(int i=1; i<=5; i+=2){
        if(argv[i][1] == 'i') ip = argv[i + 1];
        else if(argv[i][1] == 'p') server_port = atoi(argv[i + 1]);
        else if(argv[i][1] == 'd'){
            memset(directory, '\0', MAX_BUF);
            strcpy(directory, argv[i + 1]);
        }
    }
}

int mymin(int a, int b){
    if(a < b) return a;
    return b;
}

int main(int argc, char *argv[]){

    // function to get arguments from input
    get_arguments(argc, argv);
    
    printf("Client's parameters are:\nip: %s\nserver_port: %d\ndirectory: %s\n\n\n",
            ip, server_port, directory);
    
    // make socket reusable
    long client_socket = socket(AF_INET, SOCK_STREAM, 0), opt = 1;
    setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
    struct sockaddr_in server;  
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);
    inet_aton(ip, &server.sin_addr);

    // connect to socket
    if(connect(client_socket, (struct sockaddr*) &server, sizeof(server)) != 0){
        perror("Cannot connect\n");
        exit(0);
    }

    printf("Connecting to %s on port %d\n", ip, server_port);
    mywrite_s(client_socket, directory, MAX_BUF);

    struct dir_path_name info;
    int countfiles = 0; 
    int readb = 0;
    while(true){
        memset(info.dir_path, '\0', MAX_BUF);
        memset(info.name, '\0', MAX_BUF);
        memset(info.fullpath, '\0', MAX_BUF);
        
        // read info struct contents        
        myread_s(client_socket, info.dir_path, MAX_BUF);
        myread_s(client_socket, info.name, MAX_BUF);
        myread_s(client_socket, info.fullpath, MAX_BUF);
        
        uint32_t tmp;
        read(client_socket, &tmp, sizeof(tmp));
        info.filebytes = ntohl(tmp);    
        read(client_socket, &tmp, sizeof(tmp));
        info.numofFiles = ntohl(tmp);
        read(client_socket, &tmp, sizeof(tmp));
        info.send_block_size = ntohl(tmp);
        
        int pid = fork();
        // fork to make folder
        if(pid == 0){
            execl("/bin/mkdir", "mkdir", "-p", info.dir_path, (char*)NULL);
            exit(0);
        }
        else{
            // wait for mkdir of child
            wait(NULL);

            // remove if file exists
            int status = remove(info.fullpath);

            // open creates the file with all permissions
            int fd = open(info.fullpath, O_WRONLY | O_CREAT, 0777);
            countfiles++;

            int sz = info.filebytes;    
            char buf[info.send_block_size];

            while(sz > 0){
                // read file
                memset(buf, '\0', info.send_block_size);
                myread_s(client_socket, buf, info.send_block_size);
                // if the remaining bytes are less than block_size read less bytes than block_size
                mywrite_s(fd, buf, mymin(info.send_block_size, sz));
                sz -= mymin(info.send_block_size, sz);
            }
            printf("Received: %s\n", info.fullpath);
            
            if(countfiles == info.numofFiles){
                printf("Received all files, exiting client\n");
                break;
            }
        }
    }

    return 0;
}
