/*
 Very incomplete Cross-platform TCP socket wrapper.
author: me (github.com/stc4543)
*/


#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#elif __linux__ 
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define DEFAULT_BUFLEN 4096

class STCPSocket
{
    public:
    STCPSocket()
    {
#ifdef _WIN32
        
#elif __linux__
        
#endif
        
    }
    
    bool STConnect(const char *address , const char *port)
    {
#ifdef _WIN32
        int iResult = 0;
        iResult = WSAStartup(MAKEWORD(2,2), &this -> _wsaData);
        if (iResult != 0) {
            return false;
        }
        struct addrinfo *result = NULL, *ptr = NULL , hints;
        ZeroMemory( &hints, sizeof(hints) );
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        
        iResult = getaddrinfo(address , port, &hints, &result);
        if ( iResult != 0 ) {
            WSACleanup();
            return false;
        }
        
        for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {
            
            // Create a SOCKET for connecting to server
            this -> _socket = socket(ptr->ai_family, ptr->ai_socktype,  ptr->ai_protocol);
            if (this -> _socket == INVALID_SOCKET) {
                WSACleanup();
                return false;
            }
            
            // Connect to server.
            iResult = connect( this -> _socket, ptr->ai_addr, (int)ptr->ai_addrlen);
            if (iResult == SOCKET_ERROR) {
                closesocket(this -> _socket);
                this -> _socket = INVALID_SOCKET;
                continue;
            }
            break;
        }
        
        freeaddrinfo(result);
        
        if (this -> _socket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }
        return true;
#elif __linux__
        
        struct addrinfo hints, *servinfo;
        bool setup = true;
        memset(&hints, 0, sizeof hints);
        
        hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
        
        //Setup the structs if error print why
        int res;
        if ((res = getaddrinfo(server_address,port,&hints,&servinfo)) != 0)
        {
            setup = false;
            fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(res));
        }
        
        //setup the socket
        if ((_socket = socket(servinfo->ai_family,servinfo->ai_socktype,servinfo->ai_protocol)) == -1)
        {
            perror("client: socket");
            freeaddrinfo(servinfo);
            return false;
        }
        
        struct timeval time;
        time.tv_sec= 5;
        
        //Connect
        if (connect(s,servinfo->ai_addr, servinfo->ai_addrlen) == -1)
        {
            close (_socket);
            freeaddrinfo(servinfo);
            return false;
        }
        
        freeaddrinfo(servinfo);
        return true;
        
        
#endif
        
    }
    
    ~STCPSocket()
    {
#ifdef _WIN32
        closesocket(this -> _socket);
        WSACleanup();
#elif __linux__
        close(_socket);
#endif
        
    }
    
    int STsend(const char *data, const int length)
    {
#ifdef _WIN32
        return send( this -> _socket, data , length ,0);
#elif __linux__
        return write(this -> _socket  ,  data, length);
#endif
        
    }
    
    int STrecv(char *data,  int length)
    {
#ifdef _WIN32
        return recv( this -> _socket, data , length ,0);
#elif __linux__
        return read(this -> _socket  ,  data, length );
        
#endif
        
    }
    
    
    private:
    
#ifdef _WIN32
    SOCKET _socket = INVALID_SOCKET;
    WSADATA _wsaData;
#elif __linux__
    int _socket;
#endif
};
