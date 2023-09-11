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
#include <dirent.h>
#include <vector>
#include <sys/stat.h>
#include <queue>
#include <map>
#include <fcntl.h>

using namespace std;

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

int port;
int thread_pool_size;
int queue_size;
int block_size;

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

long main_thread_id;
long server_socket;
queue<dir_path_name> task_queue;
vector<pthread_t> worker_threads_ids;
vector<pthread_t> comm_threads_ids;
vector<pthread_mutex_t> mtx_client;
map<int, int> sock_to_mtx;

pthread_mutex_t mtx_task_queue;
pthread_cond_t cond_task_queue_nonfull;
pthread_cond_t cond_task_queue_nonempty;

// helper function to assign above values
void get_arguments(int argc, char *argv[]){
    if(argc != 9){
        perror("Invalid arguments\n");
        exit(0);
    }

    for(int i=1; i<=7; i+=2){
        if(argv[i][1] == 'p') port = atoi(argv[i + 1]);
        else if(argv[i][1] == 's') thread_pool_size = atoi(argv[i + 1]);
        else if(argv[i][1] == 'q') queue_size = atoi(argv[i + 1]);
        else if(argv[i][1] == 'b') block_size = atoi(argv[i + 1]);
    }
}

// my code from a project with mr. Delis, implementation of ls 

char** LS(char* dir){
    struct dirent **names;
    int sz = scandir(dir, &names, NULL, alphasort);
    if(sz == -1) return NULL;
    char **namesl = (char**)malloc(sizeof(char*) * sz);
    int c = 0;

    namesl[0] = (char*)malloc(sizeof(char) * 20);
    memset(namesl[0], '\0', 20);

    for(int i=0; i<sz; ++i){
        if(names[i]->d_name[0] == '.'){
            free(names[i]);
            continue;
        }
        c++;
        namesl[c] = (char*)malloc(strlen(names[i]->d_name) * sizeof(char) + 5);
        memset(namesl[c], '\0', strlen(names[i]->d_name) * sizeof(char) + 5);
        strcpy(namesl[c], names[i]->d_name);
        free(names[i]);
    }
    free(names);

    sprintf(namesl[0], "%d", c);
    return namesl;
}

char* concPath(char *s1, char *s2){
    char *s3 = (char*)malloc((strlen(s1) + strlen(s2) + 5) * sizeof(char));
    memset(s3, '\0', (strlen(s1) + strlen(s2) + 5) * sizeof(char));
    sprintf(s3, "%s/%s", s1, s2);
    return s3;
}

int is_directory(char *path){
    struct stat info;
    stat(path, &info);

    return S_ISDIR(info.st_mode);
}

// gets all names of files from a directory recursively

void get_all_names(char *path, vector<struct dir_path_name> &result){
    char **names = LS(path);
    int numofFiles = atoi(names[0]);
    free(names[0]);

    for(int i=1; i<=numofFiles; ++i){
        if(names[i] == NULL) continue;
        char *newpath = concPath(path, names[i]);
        if(is_directory(newpath)) get_all_names(newpath, result);
        else{
            struct dir_path_name curs;
            memset(curs.dir_path, '\0', MAX_BUF);
            memset(curs.fullpath, '\0', MAX_BUF);
            memset(curs.name, '\0', MAX_BUF);

            strcpy(curs.dir_path, path);
            strcpy(curs.name, names[i]);
            strcpy(curs.fullpath, newpath);
            result.push_back(curs);
        }
        free(newpath);
        free(names[i]);
    }
    free(names);
}           

void *communication(void *argp){
    int client_socket = (long) argp;
    char *pathname = (char*)malloc(MAX_BUF * sizeof(char));
    memset(pathname, '\0', MAX_BUF);
    pthread_mutex_lock(&mtx_client[sock_to_mtx[client_socket]]);
    myread_s(client_socket, pathname, MAX_BUF);
    pthread_mutex_unlock(&mtx_client[sock_to_mtx[client_socket]]);

    printf("[Communication Thread: %ld]: About to scan directory [%s] in socket %d\n", pthread_self(), pathname, client_socket);
    vector<struct dir_path_name> file_dir_and_names;
    get_all_names(pathname, file_dir_and_names);

    int size = file_dir_and_names.size();
    for(int i=0; i<size; ++i){
        file_dir_and_names[i].client_socket = (uint32_t)client_socket;
        file_dir_and_names[i].numofFiles = (uint32_t)size;
        struct stat bufs;
        int fd = open(file_dir_and_names[i].fullpath, O_RDONLY);
        // reads how many bytes is the file
        fstat(fd, &bufs);
        file_dir_and_names[i].filebytes = (uint32_t)bufs.st_size;
        file_dir_and_names[i].send_block_size = (uint32_t)block_size;
        
        // producer consumer
        pthread_mutex_lock(&mtx_task_queue);
        printf("[Communication Thread: %ld]: locking mutex for task queue\n", pthread_self());
        while(task_queue.size() >= queue_size){
            printf("[Communication Thread: %ld]: Found task queue full\n", pthread_self());
            pthread_cond_wait(&cond_task_queue_nonfull, &mtx_task_queue);
        }
        printf("[Communication Thread: %ld]: Task queue size: %ld, inserting item [%s %s %d]\n", pthread_self(), task_queue.size(),
            file_dir_and_names[i].dir_path, file_dir_and_names[i].name, file_dir_and_names[i].client_socket);
        task_queue.push(file_dir_and_names[i]);
        printf("[Communication Thread: %ld]: unlocking mutex for task queue\n\n", pthread_self());
        pthread_mutex_unlock(&mtx_task_queue);
        pthread_cond_signal(&cond_task_queue_nonempty);
    }
    printf("Exiting communication thread with id [%ld]\n", pthread_self());

    free(pathname);
    pthread_exit(NULL);
}

int mymin(int a, int b){
    if(a < b) return a;
    return b;
}

void *worker(void *argp){
    printf("[Worker thread: %ld]: Just spawned\n", pthread_self());

    while(true){
        printf("[Worker thread: %ld]: locking mutex for task queue\n", pthread_self());
        pthread_mutex_lock(&mtx_task_queue);
        while(task_queue.size() <= 0){
            printf("[Worker thread: %ld]: Found task queue empty\n\n", pthread_self());
            pthread_cond_wait(&cond_task_queue_nonempty, &mtx_task_queue);
        } 
        dir_path_name info = task_queue.front();
        task_queue.pop();

        printf("[Worker thread: %ld]: Received task: [%s %s %d]\n", pthread_self(), info.dir_path, info.name, info.client_socket);
        printf("[Worker thread: %ld]: unlocking mutex for task queue\n\n", pthread_self());
        pthread_mutex_unlock(&mtx_task_queue);
        pthread_cond_signal(&cond_task_queue_nonfull);

        pthread_mutex_lock(&mtx_client[sock_to_mtx[info.client_socket]]);
        printf("[Worker thread: %ld]: Locking mutex for writing to socket\n", pthread_self());
        mywrite_s(info.client_socket, info.dir_path, MAX_BUF);
        mywrite_s(info.client_socket, info.name, MAX_BUF);
        mywrite_s(info.client_socket, info.fullpath, MAX_BUF);
        
        // send information about file

        uint32_t tmp = htonl(info.filebytes);
        write(info.client_socket, &tmp, sizeof(tmp));
        tmp = htonl(info.numofFiles);
        write(info.client_socket, &tmp, sizeof(tmp));
        tmp = htonl(info.send_block_size);
        write(info.client_socket, &tmp, sizeof(tmp));

        int fd = open(info.fullpath, O_RDONLY);
        uint32_t sz = info.filebytes;
        char buf[block_size];
        
        while(sz > 0){
            memset(buf, '\0', block_size);
            myread_s(fd, buf, mymin(block_size, sz));
            mywrite_s(info.client_socket, buf, block_size);
            sz -= mymin(block_size, sz);
        }

        printf("[Worker thread: %ld]: Unlocking mutex\n", pthread_self());
        pthread_mutex_unlock(&mtx_client[sock_to_mtx[info.client_socket]]);
        // first we will send the 
    }

}
 
// handler for SIGINT, destroy mutexes and threads and close sockets

void handler_SIGINT(int signo){
    vector<pthread_mutex_t> mtx_client;
    pthread_mutex_t mtx_task_queue;

    printf("\n\nDestroying client mutexes\n");
    for(int i=0; i<mtx_client.size(); ++i){
        pthread_mutex_unlock(&mtx_client[i]);
        pthread_mutex_destroy(&mtx_client[i]);
    }
    
    printf("Destroying task queue mutex\n");
    pthread_mutex_unlock(&mtx_task_queue);
    pthread_mutex_destroy(&mtx_task_queue);

    for(map<int, int>::iterator it=sock_to_mtx.begin(); it!=sock_to_mtx.end(); ++it){
        printf("Closing socket %d\n", it->first);
        close(it->first);
    }
    
    printf("\nClosing server socket %ld\n\n", server_socket);
    close(server_socket);
    
    for(int i=0; i<worker_threads_ids.size(); ++i){
        if(pthread_self() == worker_threads_ids[i]) continue;
        printf("Cancelling worker thread with id [%ld] --> cancel return %d\n", 
            worker_threads_ids[i], pthread_cancel(worker_threads_ids[i]));
    }
       

    printf("Exiting main thread with id [%ld]\n", main_thread_id);
    if(main_thread_id == pthread_self()) exit(0);
    else pthread_cancel(main_thread_id);
    // in case signal was not received in main thread
    printf("Exiting worker with id [%ld] that received SIGINT signal\n", pthread_self());
    exit(0);  
}

int main(int argc, char *argv[]){
    get_arguments(argc, argv);
    
    main_thread_id = pthread_self();
    signal(SIGINT, handler_SIGINT);

    pthread_mutex_init(&mtx_task_queue, 0);
    printf("Server's parameters are:\nport: %d\nthread_pool_size: %d\nqueue_size: %d\nblock_size: %d\n\n\n",
            port, thread_pool_size, queue_size, block_size);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) != 0){
        perror("Failed to set socket fd option");
        exit(0);
    }

    // code to initialize server
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);
    int serverlen = sizeof(server);

    if(bind(server_socket, (struct sockaddr*) &server , sizeof(server)) < 0){
        perror("Failed to bind");
        exit(0);
    }

    printf("Server was successfully initialized...\n\n");
    printf("Listening for connections to port %d\n", port);

    printf("Creating worker threads:\n\n");

    for(int i=0; i<thread_pool_size; ++i){
        pthread_t workeri;
        pthread_create(&workeri, NULL, worker, NULL);
        worker_threads_ids.push_back(workeri);
    }

    if(listen(server_socket, 5) < 0){
        perror("Failed to prepare to accept connections");
        exit(0);
    }


    while(true){
        long c_socket;

        if((c_socket = accept(server_socket, (struct sockaddr*) &server, (socklen_t*)&serverlen)) < 0){
            perror("Failed to accept connection and open new socket");
            exit(0);
        }
        printf("Accepted connection in client socket %ld\n", c_socket);

        printf("Creating communication thread\n");

        sock_to_mtx[c_socket] = mtx_client.size();
        pthread_mutex_t cur_mtx;
        pthread_mutex_init(&cur_mtx, 0);
        mtx_client.push_back(cur_mtx);

        pthread_t communication_thread;
        pthread_create(&communication_thread, NULL, communication, (void *) c_socket);      
        pthread_detach(communication_thread);  
        comm_threads_ids.push_back(communication_thread);
    }

    return 0;
}
