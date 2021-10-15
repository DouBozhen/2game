
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <string>

#include "udp_client.h"
using namespace std;

UdpClient::UdpClient(std::string ip, int port)
{
    memset(&addr_, 0, sizeof(addr_));

    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = inet_addr(ip.c_str());
    addr_.sin_port = htons(port);

    printf("ip: %s\n", ip.c_str());
    ip_ = ip;
    socket_ = -1;
}

UdpClient::~UdpClient()
{
    close(socket_);
}


bool UdpClient::inetSocket()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_ < 0)
    {
        printf("UdpClient: create socket failed,\n");
        return false;
    }

    if (inet_pton(AF_INET, ip_.c_str(), &addr_.sin_addr) < 0) {
        printf("UdpClient: inet_pon failed\n");
        return false;
    }
    int flag_ = 1;
    //setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (char *) &flag_, sizeof(int));
    int time_out = 1000;
    setsockopt(socket_ ,SOL_SOCKET, SO_REUSEADDR, &time_out, sizeof(time_out));
    return true;
}

bool UdpClient::inetSend(uint8_t* buf, int buf_len)
{
    unsigned int len = sizeof(addr_);
    if(sendto(socket_, buf, buf_len, 0, (struct sockaddr*)&addr_, len) < 0)
    {
        printf("UdpClient: send failed, %d, %s\n", errno, strerror(errno));
        return false;
    }

    return true;
}

int UdpClient::inetRecv(uint8_t* buf, int buf_len)
{
    unsigned int len = sizeof(addr_);
    int recv_bytes = recvfrom(socket_, buf, buf_len, 0, (struct sockaddr*)&addr_, &len);
    if( recv_bytes <= 0)
    {
        printf("UdpClient: recv failed, %d, %s\n", errno, strerror(errno));
    }

    return recv_bytes; 
}

void UdpClient::inetClose()
{
    close(socket_);
}

