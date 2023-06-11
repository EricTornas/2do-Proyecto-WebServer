#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<stdio.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/sendfile.h>
#include<errno.h>
#include <sys/mman.h>
#include <time.h>
#include<stdbool.h>
#include <dirent.h>

#define MAXLINE 25000
#define MAXBUF 25000
#define LISTENQ 1024
#define RIO_BUFSIZE 8192

typedef struct sockaddr SA;

char * home_path;

typedef struct {
    int rio_fd;/* Descriptor for this internal buf */
    int rio_cnt;/* Unread bytes in internal buf */
    char *rio_bufptr;/* Next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE];/* Internal buffer */
    } rio_struct;


void rio_read_initb(rio_struct *rp, int fd);

ssize_t rio_writen(int fd, void *usrbuf, size_t n);

static ssize_t rio_read(rio_struct *rp, char *usrbuf, size_t n);

ssize_t rio_readnb(rio_struct *rp, void *usrbuf, size_t n);

void read_request(rio_struct *rp);

ssize_t rio_read_line_first(rio_struct *rp, void *usrbuf, size_t maxlen);

int parse_uri(char *uri, char *filename, char *d_args);

void get_filetype(char *filename, char *filetype);

int open_listenfd(int port);

void cliente_error(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg);

void serve_static(int fd, char *filename, int filesize, bool is_directory, struct stat sbuf);

void doit(int fd);



void rio_read_initb(rio_struct *rp, int fd) /*Inicializador de buffer*/
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

int main(int argc, char **argv){

    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;
    home_path = strdup("/home");

    if (argc <= 1) {
        fprintf(stderr, "usage: %s <port> <folder>\n", argv[0]);
        exit(1);
    }
    
    port = atoi(argv[1]);

    if(argc == 3){
        home_path = argv[2];
    }
    listenfd = open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        doit(connfd);
        close(connfd);
    }
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
 { 
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;
    while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
    if (errno == EINTR) 
    nwritten = 0; 
    return -1; 
    }
    nleft -= nwritten;
    bufp += nwritten;
 }
 return n;
}

static ssize_t rio_read(rio_struct *rp, char *usrbuf, size_t n)
{
    int cnt;
    
    while (rp->rio_cnt <= 0) {
        /* Refill if buf is empty */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
        sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR)/* Interrupted by sig handler return */
            return -1;
            }
            else if (rp->rio_cnt == 0)/* EOF */
            return 0;
            else
            rp->rio_bufptr = rp->rio_buf;/* Reset buffer ptr */
            }
            
        
            cnt=n;
            if (rp->rio_cnt < n)
            cnt = rp->rio_cnt;
            memcpy(usrbuf, rp->rio_bufptr, cnt);
            rp->rio_bufptr += cnt;
            rp->rio_cnt -= cnt;
            return cnt;
            }
    
ssize_t rio_readnb(rio_struct *rp, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    
    while (nleft > 0) {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) {
            if (errno == EINTR)/* Interrumpe */
            nread = 0;/* Llama a read() */
            else
            return -1;/* errno set by read() */
            }
            else if (nread == 0)
            break;
            nleft -= nread;
            bufp += nread;
            }
            return (n - nleft);/* Return >= 0 */
}

ssize_t rio_read_line_first(rio_struct *rp, void *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = usrbuf;
    
    for (n = 1; n < maxlen; n++) {
        if ((rc = rio_read(rp, &c, 1)) == 1) {
            *bufp++ = c;
            if (c == '\n')
            break;
            } else if (rc == 0) {
                if (n == 1)
                return 0;
                else
                break;
                } else
                return -1;/* Error */
                }
                *bufp = 0;
                return n;
}

void read_request(rio_struct *rp){  
    char buf[MAXLINE];
    rio_read_line_first(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
        rio_read_line_first(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int open_listenfd(int port)
{
    int listenfd, optval=1;
    
    struct sockaddr_in serveraddr;
    
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    
    /* Eliminates "Address already in use" error from bind */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
        (const void *)&optval , sizeof(int)) < 0)
        return -1;
    
    /* Listenfd will be an end point for all requests  for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    
    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
}

void create_html_code(char * filename, char * output){
    char * temp = (char *)calloc(sizeof(char), 25000);
    struct stat sbuf;
    DIR * dirp = opendir(filename);
    if (dirp == NULL){
        printf("Error: Cannot open dir\n");
        exit(2);
    }

    strcpy(output, "<html><h1>My WebServer</h1>"); 
    strcat(output, "<head>Directorio "); 
    strcat(output, filename); // Ruta completa (absolute path) del directorio o archivo por el que se esta buscando
    strcat(output, "</head><body><table><tr><th>Name</th><th>Size</th><th>Date</th></tr>"); // Comenzando a construir la tabla con nombre, tamanno y fecha

    // Comenzando a agregar cada directorio y archivo
    struct dirent *direntp;

    while ((direntp = readdir(dirp)) != NULL) {
        if(strcmp(direntp->d_name, ".") == 0 || strcmp(direntp->d_name, "..") == 0)
            continue;
        strcpy(temp, filename);
        strcat(temp, "/");
        strcat(temp, direntp->d_name);

        if (stat(temp, &sbuf) < 0)
            continue;

        strcat(output, "<tr><td><a href=\"");
        strcat(output, filename);

        struct stat time;
      
        
        char*actual_filename=calloc(500,sizeof(char));
        strcpy(actual_filename,filename);
        strcat(actual_filename,"/");
        strcat(actual_filename,direntp->d_name);
        stat(actual_filename, &time);
        char* temp_time=calloc(100,sizeof(char));
        strcpy(temp_time, ctime(&time.st_mtime));

        sprintf(output, "%s/%s\">%s</a></td><td>%ld</td><td>%s</td></tr>", output, direntp->d_name, direntp->d_name, sbuf.st_size,temp_time);

        free(temp_time);
    }
    
    strcat(output, "</table></body></html>"); // Copiando el contenido del final
    
    /* Cerramos el directorio */
    closedir(dirp);
    free(temp);

}

char *str_replace(char *orig, char *rep, char *with) {
    
    char *result; 
    char *ins;     
    char *tmp;

    int len_rep;  
    int len_with; 
    int len_front; 
    int count;    

    
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; 
    if (!with)
        with = "";
    len_with = strlen(with);
    ins = orig;
    for (count = 0; tmp = strstr(ins, rep); ++count) {
        ins = tmp + len_rep;
    }
    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; 
    }
    strcpy(tmp, orig);
    return result;
}

int parse_uri(char *uri, char *filename, char *d_args)
{
    char *ptr;
    char * temp_uri = str_replace(uri, strdup("%20"), strdup(" ")); /*Elimina los espacios en blanco*/
    if(temp_uri == NULL){
        temp_uri = uri;
    }
    if (!strstr(temp_uri, "cgi-bin")) {/* Static content */
        strcpy(d_args, "");
        strcpy(filename, temp_uri);// 
    if (uri[strlen(temp_uri)-1] == '/')
        if(strlen(temp_uri)<=1){
            strcpy(filename, home_path);
        }
    free(temp_uri);
    return 1;
    }
    else {                            /* Dynamic content */
    ptr = index(temp_uri, '?');
    if (ptr) {
        strcpy(d_args, ptr+1);
        *ptr = '\0';
        }
        else
        strcpy(d_args, "");
        strcpy(filename, ".");
        strcat(filename, temp_uri);
        free(temp_uri);
        return 0;
        }
    }

void cliente_error(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg){  
    
    char *buf=(char *)calloc(sizeof(char), MAXLINE); 
    char* body=(char *)calloc(sizeof(char), MAXBUF);
    
    sprintf(body, "<html><title>My WebServer Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>My WebServer</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html; charset=utf-8\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));

    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
    free(buf);
    free(body);
}

void get_filetype(char *filename, char *filetype){
    if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".zip"))
    strcpy(filetype, "application/zip");  
    else if (strstr(filename, ".pdf"))
    strcpy(filetype, "application/pdf");     
    else
    strcpy(filetype, "text/plain");
}

void serve_static(int fd, char *filename, int filesize, bool is_directory, struct stat sbuf){
    int srcfd;

    char * filetype=(char *)calloc(sizeof(char), 1000);
    char * buf=(char *)calloc(sizeof(char), 25000);
    
    if(is_directory){ 
         
            
        char * output = (char *)calloc(sizeof(char), 100000);//Total a devolver
            
        create_html_code(filename, output);


        filesize = strlen(output);
        sprintf(buf, "HTTP/1.0  OK\r\n");
        sprintf(buf, "%sServer: My WebServer\r\n", buf);
        sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
        sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, "text/html; charset=utf-8");
        
        rio_writen(fd, buf, strlen(buf));
        rio_writen(fd, output, filesize);
        free(output);
    }


    
    else{ // Si es un archivo
        get_filetype(filename, filetype);
        sprintf(buf, "HTTP/1.0 OK\r\n");
        sprintf(buf, "%sServer: My WebServer\r\n", buf);
        sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
        sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
        rio_writen(fd, buf, strlen(buf));

        srcfd = open(filename, O_RDONLY, 0);
        if(fd < 0)
            cliente_error(fd, filename, "404", "Not found",
                    "Couldn't find this file");
        else{
            while(sendfile(fd, srcfd, NULL, sbuf.st_blksize) > 0){
            }
        }
        close(srcfd);
    }
    free(filetype);
    free(buf);
}
 
void doit(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], d_args[MAXLINE];
    rio_struct rio;

    rio_read_initb(&rio, fd);
    rio_read_line_first(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        cliente_error(fd, method, "501", "Not Implemented",
        "My WebServer does not implement this method");
        return;
        }
        read_request(&rio);
        is_static = parse_uri(uri, filename, d_args);
        if (stat(filename, &sbuf) < 0) {
            cliente_error(fd, filename, "404", "Not found",
            "My WebServer couldn't find this file");
            return;
            }
            
        if (is_static) {
        bool is_directory = (S_ISDIR(sbuf.st_mode));
            
            serve_static(fd, filename, sbuf.st_size, is_directory, sbuf);
            }
            else {
            if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
                cliente_error(fd, filename, "403", "Forbidden",
                "My WebServer couldn't run the CGI program"
                );
                return;
                }
                return;
                }
}

