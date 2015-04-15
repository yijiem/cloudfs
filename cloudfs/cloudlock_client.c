//
//  cloudlock_client.c
//  
//
//  Created by Yijie Ma on 4/15/15.
//
//
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include "cloudlock_client.h"
#include "cloudfs.h"


int cloudlock_connect(const char *host_ip, int host_port) {
    int sockfd = 0, n = 0;
    char recvBuff[1024];
    struct sockaddr_in serv_addr;

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        write_log("cloudlock: Error : Could not create socket \n");
        return -errno;
    } 

    memset(&serv_addr, '0', sizeof(serv_addr)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(host_port);

    if(inet_pton(AF_INET, host_ip, &serv_addr.sin_addr)<=0)
    {
        write_log("cloudlock: inet_pton error occured\n");
        return -errno;
    } 

    if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       write_log("cloudlock: Error : Connect Failed \n");
       return -errno;
    }

    while ((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
        recvBuff[n] = 0;
        if(fputs(recvBuff, stdout) == EOF)
        {
            write_log("cloudlock: Error : Fputs error\n");
            return -errno;
        }
    } 

    if(n < 0)
    {
        write_log("cloudlock: Read error, n < 0\n");
    }

    write_log("cloudlock: read from server: %s\n", recvBuff);

    return 0;
}

