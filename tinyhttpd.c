/*#!/usr/bin/env
* ******************************************************
* Author       : Gery
* Last modified: 2018-05-06 16:27
* Email        : 2458314507@qq.com
* Filename     : tinyhttpd.c
* Description  :
* ********************************************************/
#include<stdio.h>
#include<sys/wait.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/types.h>
#include<stdlib.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/select.h>
#include<ctype.h>
#include<pthread.h>
#include<stdint.h>

#define ISspace(x)  isspace((int)(x))

#define SERVER_STRING "Server:jdbhttpd/0.1.0\r\n"

#define STDIN 0
#define STDOUT 1
#define STDERR 2


void execute_cgi(int ,const char *,const char *,const char *);
void bad_requests(int );
void accept_request(void *);
int startup(u_short *);
void error_die(const char *);
void unimplemented(int );
void not_found(int );
void serve_file(int,const char*);
void headers(int,const char*);
void cat(int,FILE*);
void cannot_execute(int);
int get_line(int,char*,int );

void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}


void accept_request(void *arg)
{
    int client = (intptr_t)arg;
    char buf[1024];
    size_t numchars;
    //保存请求方式
    char method[255];
    //保存请求url
    char url[255];
   //保存请求文件路径
    char path[512];
    size_t i,j;
    struct stat st;
    //如果cgi=1  则执行cgi程序
    int cgi = 0;

    char *query_string =NULL;
    numchars = get_line(client,buf,sizeof(buf));
    i = 0,j = 0;
    while(!ISspace(buf[i])&&(i<sizeof(method)-1))
    {
        //请求报文第一行就会说明请求方式，是否为GET或者POST
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';
    //tinyhttpd只实现了GET和POST
    if(strcasecmp(method,"GET")&&strcasecmp(method,"POST"))
    {
        //tinyhttpd只实现了GET和POST
        unimplemented(client);
        return;
    }
    if(strcasecmp(method,"POST")==0)  cgi = 1;
    i = 0;
    //跨过空格
    while(ISspace(buf[j])&&(j<numchars))   j++;
    //得到请求的url
    while(!ISspace(buf[j])&&(i<sizeof(url)-1)&&(j<numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';
    //对于GET请求，如果有携带参数，则query_string指针指向url中?后面的GET参数
    if(strcasecmp(method,"GET")==0)
    {
        query_string = url;
        while((*query_string!='?')&&(*query_string!='\0'))
            query_string++;
        if(*query_string=='?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    sprintf(path,"htdocs%s",url);
    //如果path是一个目录,默认设置为首页index.html
    if(path[strlen(path)-1]=='/')  strcat(path,"index.html");
    if(stat(path,&st)==-1)
    {
        while((numchars>0)&&strcmp("\n",buf))  numchars = get_line(client,buf,sizeof(buf));
        not_found(client);
    }
    else
    {
        //如果为目录
        if((st.st_mode&S_IFMT)==S_IFDIR) strcat(path,"/index.html");
        if((st.st_mode&S_IXUSR)||(st.st_mode&S_IXGRP)||(st.st_mode&S_IXOTH))  cgi = 1;
        //返回静态文件
        if(!cgi) serve_file(client,path);
        else
            execute_cgi(client,path,method,query_string);
    }
    close(client);
}

void execute_cgi(int client,const char *path,const char *method,const char *query_string)
{
    char buf[1024];
    int cgi_input[2];
    int cgi_output[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;
    buf[0] = 'A';
    buf[1] = '\0';
    //读取并且舍弃头部信息
    if(strcasecmp(method,"GET")==0)
    {
        while((numchars>0)&&strcmp("\n",buf))
            numchars = get_line(client,buf,sizeof(buf));
    }
    else
    {
        numchars = get_line(client,buf,sizeof(buf));
        //15是因为Content-Length长度为15
        while((numchars>0)&&strcmp("\n",buf))
        {
            buf[15] = '\0';
        //求得Content-content_length的长度
            if(strcasecmp(buf,"Content-Length:")==0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client,buf,sizeof(buf));
        }
        if(content_length==-1)
        {
            bad_requests(client);
            return;
        }
    }
    if(pipe(cgi_output)<0)
    {
        cannot_execute(client);
        return;
    }
    if(pipe(cgi_input)<0)
    {
        cannot_execute(client);
        return;
    }
    //fork一个子进程
    if((pid = fork())<0)
    {
        cannot_execute(client);
        return;
    }
    sprintf(buf,"HTTP/1.0 200 OK\r\n");
    send(client,buf,strlen(buf),0);
    //子进程执行cgi，并将输出传到cgi_output[1]
    if(pid==0)
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];
        //对标准输入和标准输出进行重定向
        dup2(cgi_output[1],STDOUT);
        dup2(cgi_input[0],STDIN);

        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env,"REQUEST_METHOD=%s",method);
        putenv(meth_env);
        if(strcasecmp(method,"GET")==0)
        {
            sprintf(query_env,"QUERY_STRING=%s",query_string);
            putenv(query_env);
        }
        else
        {
            sprintf(length_env,"CONTENT_LENGTH=%d",content_length);
            putenv(length_env);
        }
        execl(path,path,NULL);
        exit(0);
    }
    //父进程传输数据和把数据发送到浏览器
    else
    {
        close(cgi_input[0]);
        close(cgi_output[1]);
        if(strcasecmp(method,"POST")==0)
        {
            for(i = 0;i<content_length;i++)
            {
                recv(client,&c,1,0);
                write(cgi_input[1],&c,1);
            }
        }
        while(read(cgi_output[0],&c,1)>0) send(client,&c,1,0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid,&status,0);
    }
}

void cannot_execute(int client)
{
    char buf[1024];
    sprintf(buf,"HTTP/1.0 500 Internal Server Error\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"Content-type: text/html\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"<P>Error prohibited CGI execution. \r\n ");
    send(client,buf,sizeof(buf),0);
}

void bad_requests(int client)
{
    char buf[1024];
    sprintf(buf,"HTTP/1.0 400 BAD REQUEST\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"Content-type: text/html\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"\r\n");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"<P>your browser senta bad request, ");
    send(client,buf,sizeof(buf),0);
    sprintf(buf,"such as a POST without a Content-Length.\r\n");
    send(client,buf,sizeof(buf),0);
}


void headers(int client,const char* filename)
{
    char buf[1024];
    (void)filename;

    strcpy(buf,"HTTP/1.0 200 OK\r\n");
    send(client,buf,strlen(buf),0);
    strcpy(buf,SERVER_STRING);
    send(client,buf,strlen(buf),0);
    sprintf(buf,"Content-Type: text/html\r\n");
    send(client,buf,strlen(buf),0);
    strcpy(buf,"\r\n");
    send(client,buf,strlen(buf),0);
}

void cat(int client,FILE *resource)
{
    char buf[1024];
    fgets(buf,sizeof(buf),resource);
    while(!feof(resource))
    {
        send(client,buf,strlen(buf),0);
        fgets(buf,sizeof(buf),resource);
    }
}


void serve_file(int client,const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while((numchars>0)&&strcmp("\n",buf))  numchars = get_line(client,buf,sizeof(buf));
    resource = fopen(filename,"r");
    if(resource==NULL)  not_found(client);
    else
    {
        //添加http头部
        headers(client,filename);
        //并发送文件内容
        cat(client,resource);
    }
    fclose(resource);
}

void not_found(int client)
{
    char buf[1024];
    sprintf(buf,"HTTP/1.0 404 NOT FOUND\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,SERVER_STRING);
    send(client,buf,strlen(buf),0);
    sprintf(buf,"Content-Type: text/html\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"<BODY><P>The server could not fulfill\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"your request because the resource specified\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"is unavailable or nonexistent.\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"</BODY></HTML>\r\n");
    send(client,buf,strlen(buf),0);

}

void unimplemented(int client)
{
    char buf[1024];
    sprintf(buf,"HTTP/1.0 501 Method Not implemented\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,SERVER_STRING);
    send(client,buf,strlen(buf),0);
    sprintf(buf,"Content-Type: text/html\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"</TITLE></HEAD>\r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"<BODY><P>HTTP request method not supported. \r\n");
    send(client,buf,strlen(buf),0);
    sprintf(buf,"</BODY></HTML>\r\n");
    send(client,buf,strlen(buf),0);
}


//解析一行http报文
int get_line(int sock,char *buf,int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while((i<size-1)&&(c!='\n'))
    {
        //先取一个字节
        n = recv(sock,&c,1,0);
        if(n>0)
        {
            //因为请求报文末尾是以\r\n结束的
            if(c=='\r')
            {
                //偷窥一个字节,判断是否为\n,是则结束
                n = recv(sock,&c,1,MSG_PEEK);
                if(n>0&&(c=='\n'))  recv(sock,&c,1,0);
                else c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else c = '\n';
    }
    buf[i] = '\0';
    return i;
}

int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;
    httpd = socket(PF_INET,SOCK_STREAM,0);//新建一个服务器端socket
    if(httpd == -1) error_die("socket");
    memset(&name,0,sizeof(name));
    //对socket的基本属性进行赋值
    name.sin_family = AF_INET;
    name.sin_port = htons(*port); //htons从主机字节序转换为网络字节序
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    //setsockopt原型  int setsockopt(int sockfd,int level,int optname,const void *optval,socklen_t optlen)
    if((setsockopt(httpd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)//
    {
        error_die("setsockopt failed");
    }
    //对端口进行绑定
    if(bind(httpd,(struct sockaddr*)&name,sizeof(name))<0)  error_die("bind");
    //动态分配一个端口
    if(*port==0)
    {
        socklen_t namelen = sizeof(name);
        if(getsockname(httpd,(struct sockaddr*)&name,&namelen)==-1)
        {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }
    //对socket进行监听
    //5代表内核维护一个队列跟踪这些完成的连接但服务器还没有接受处理
    //或者正在进行的连接,代表大小的上限
    if(listen(httpd,5)<0)  error_die("listen");
    else printf("listen success\n");
    return httpd;
}


int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in  client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;
    //对服务器端口号进行绑定
    server_sock = startup(&port);
    printf("httpd running on the port %d\n",port);
    while(1)
    {
        //接受来自客户端的连接
        client_sock =  accept(server_sock,(struct sockaddr *)&client_name,&client_name_len);
        if(client_sock==-1)  error_die("accept");
        if(pthread_create(&newthread,NULL,(void*)accept_request,(void*)(intptr_t)client_sock)!=0)  //每次收到请求，创建一个线程来处理接受到的请求，把client_sock转成地址参数
            //传入pthread_create
            perror("pthread_create");
    }
    //关掉服务器端socket
    close(server_sock);
    return 0;
}

