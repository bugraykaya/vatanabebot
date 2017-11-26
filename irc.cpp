//TODO: Biggest issue right now is file management.

#include "STCPSocket.h"
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <ctime>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <thread>
#include <queue>
#include <condition_variable>
#include <mutex>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


#define MESSAGE_SAVE_SUCCESS 0
#define MESSAGE_SAVE_NO_USER 1         
#define MESSAGE_SAVE_USER_ACTIVE 2 
struct Message
{
    std::string username;
    std::string messageBody;
    time_t time;
};
struct UserEntry 
{
    std::vector<Message> messages;
    time_t lastSeen;
};

void saveEntriesToFile( const char *fileName , std::map<std::string, UserEntry> &entries );
bool loadEntries(const char *fileName , std::map<std::string, UserEntry> &entries );
void parseMessage(char *, Message & );
int tryToSaveMessage(Message& , std::map<std::string, UserEntry> & entries , std::stringstream &outputString);
void sendMessage(SOCKET *s, const char* message);
BOOL WINAPI sigintFunc(DWORD signal);

//globals
const uint8_t envelope[3]= {0xE2, 0x9C, 0x89}; //utf-8 envelope emoji
char gMessagePrefix[512];
char gChannel[50];
bool running = true;
std::queue<std::string> gMessageQueue;
std::condition_variable gCV;
std::mutex gCVMutex;
std::unique_lock<std::mutex> gSenderThreadLock(gCVMutex);
std::mutex gQueueLock;
STCPSocket gSocket;

void flushMessageQueue()
{
    std::stringstream ss ;
    ss << gMessagePrefix;
    
    gQueueLock.lock();
    
    if(!gMessageQueue.empty())
    {
        ss << gMessageQueue.front();
        gMessageQueue.pop();
        if(gMessageQueue.empty())
        {
            ss<<"\r\n";
            gSocket.STsend(ss.str().c_str(), ss.str().size()+1);
        }
    }
    while( !gMessageQueue.empty())
    {
        while(ss.str().size()<=100 && !gMessageQueue.empty())
        {
            std::string front =gMessageQueue.front(); 
            ss<<'#'<< front;
            gMessageQueue.pop();
            
        }
        ss<<"\r\n";
        gSocket.STsend(ss.str().c_str(), ss.str().size()+1);
        
    }
    
    gQueueLock.unlock();
}

void senderThread()
{
    int msec = GetTickCount();
    while(running)
    {
        gCV.wait(gSenderThreadLock);
        int newMsec = GetTickCount();
        int diff = newMsec - msec;
        msec = newMsec;
        Sleep( (diff < 1500 ) * ( 1500 - diff) );
        flushMessageQueue();
        
    }
}

void inputThread()
{
    while(getchar() != 'q')
    {
        printf("not q\n");
    }
    printf("is q\n");
    running = false;
}

int  main(int argc, char **argv) 
{
    
    if(argc < 2)
    {
        printf("Need a text file containing usename, oauth token and channel name as argument.\n");
        return -1;
    }
    char loginName[30];
    char password[50];
    std::ifstream loginFile;
    loginFile.open(argv[1]);
    loginFile >> loginName >> password >> gChannel;
    printf("name: %s\npass: %s\nchannel: %s\n", loginName, password, gChannel );
    
    if (!SetConsoleCtrlHandler(sigintFunc, TRUE)) {
        printf("ERROR: Could not set control handler"); 
        return -1;
    }
    
    bool result = gSocket.STConnect("irc.twitch.tv" , "6667");
    if(!result)
    {
        printf("Can not connect\n");
        exit(0);
    }
    std::string name , pass, channel, prefix;
    name += "NICK ";name += loginName; name+= "\r\n";
    pass += "PASS "; pass += password; pass +=  "\r\n";
    channel += "JOIN #" ;channel += gChannel;channel += "\r\n";
    prefix+= "PRIVMSG #"; prefix+= gChannel; prefix+= " :";
    
    int i;
    for(i = 0; i < prefix.size() ; i++)
        gMessagePrefix[i] = prefix[i];
    gMessagePrefix[i]=0;
    
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;
    iResult = gSocket.STsend( pass.c_str() , pass.size()+1 );
    iResult = gSocket.STsend( name.c_str() , name.size()+1 );
    iResult = gSocket.STsend(  channel.c_str(), channel.size()+1);
    
    
    
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    iResult = gSocket.STrecv( recvbuf, recvbuflen);
    printf("%s\n", recvbuf);
    
    fflush(stdout);
    Message msg;
    std::stringstream ss;
    ss << gMessagePrefix <<envelope<<"\r\n";
    gSocket.STsend(ss.str().c_str(), ss.str().size() +1 );
    iResult = gSocket.STrecv( recvbuf, recvbuflen);
    
    recvbuf[iResult] = 0;
    ZeroMemory(recvbuf,  recvbuflen);
    
    int errorCode = 0;
    
    std::thread cmdLineInputThread(inputThread);
    std::thread msgSenderThread(senderThread);
    
    std::map<std::string, UserEntry> entries;
    result = loadEntries("entries.txt" , entries);
    if(result)
        printf("Loaded %zd entries\n", entries.size());
    else 
        printf("Could not load entries\n");
    
    do{
        
        iResult = gSocket.STrecv(recvbuf, recvbuflen);
        recvbuf[iResult] = 0;
        
        if ( iResult > 0 )
        {
            if(recvbuf[0] == 'P' && recvbuf[1] == 'I' && recvbuf[2] == 'N' && recvbuf[3] == 'G')
            {
                recvbuf[1] = 'O';
                gSocket.STsend(recvbuf, (int)strlen(recvbuf));
                ZeroMemory(recvbuf, recvbuflen);
                continue;
            }
            
            parseMessage(recvbuf , msg);
            printf("%s%s : %s", ctime(&msg.time), msg.username.c_str(), msg.messageBody.c_str());
            
            auto iterator = entries.find(msg.username);
            if(iterator == entries.end())
            {
                UserEntry entry;
                entry.lastSeen = msg.time;
                
                iterator = entries.insert(std::pair<std::string, UserEntry>(msg.username, entry) ).first;
                printf("New user %s added.\n", msg.username.c_str());
                //continue;
            }
            else
            {
                int seconds = difftime( msg.time , iterator->second.lastSeen );
                iterator->second.lastSeen = msg.time;
                if(seconds > 7200)
                {
                    std::stringstream ss;
                    int numberOfMessages = iterator->second.messages.size();
                    ss<< gMessagePrefix << "Welcome back, "<<msg.username<<" . You have "<< numberOfMessages << " new messages. Type !list to see them.\r\n";
                    gSocket.STsend(ss.str().c_str() , ss.str().size()+1 );
                }
                
            }
            if(msg.messageBody[0] == '!' && msg.messageBody[1] == 'm' && msg.messageBody[2] == 's' && msg.messageBody[3] == 'g'
               && msg.messageBody[4] == ' ')
            {
                std::stringstream outputString;
                //outputString << gMessagePrefix;
                int result = tryToSaveMessage(msg, entries, outputString);
                gQueueLock.lock();
                gMessageQueue.push(outputString.str());
                gQueueLock.unlock();
                gCV.notify_one();
                
            }
            else if(msg.messageBody[0] == '!' && msg.messageBody[1] == 'l' && msg.messageBody[2] == 'i' && msg.messageBody[3] == 's'
                    && msg.messageBody[4] == 't')
            {
                
                int numberOfMessages = iterator->second.messages.size();
                if(numberOfMessages == 0)
                {
                    std::stringstream ss;
                    ss<< gMessagePrefix<< "You have no new messages.\r\n";
                    gSocket.STsend(ss.str().c_str(), ss.str().size()+1);
                }
                else
                {
                    
                    for(int i= numberOfMessages-1; i>=0 ; i--)
                    {
                        std::stringstream ss;
                        ss <<gMessagePrefix<< "From "<< iterator->second.messages[i].username<<" " << difftime( time(NULL) ,iterator->second.messages[i].time)<<" seconds ago: "<<iterator->second.messages[i].messageBody<<"\r\n" ;
                        gSocket.STsend( ss.str().c_str(), ss.str().size()+1);
                        iterator->second.messages.pop_back();
                        Sleep(1500);
                        
                    }
                }
            }
            else if(msg.messageBody[0] == '!' && msg.messageBody[1] == 'l' && msg.messageBody[2] == 'a' && msg.messageBody[3] == 's'
                    && msg.messageBody[4] == 't' && msg.messageBody[5] == 's' && msg.messageBody[6] == 'e' && msg.messageBody[7] == 'e' && msg.messageBody[8] == 'n' && msg.messageBody[9] == ' ')
            {
                char c = 'a';
                std::stringstream ss(msg.messageBody);
                std::string wantedName;
                
                while(c!=' ')
                    ss.get(c);
                ss>>wantedName;
                
                for(int i = 0; i<wantedName.size(); i++)
                    wantedName[i] = tolower(wantedName[i]);
                
                auto wantedUserIterator = entries.find(wantedName);
                if(wantedUserIterator != entries.end())
                {
                    printf("user is %s\n",wantedName.c_str());
                    std::stringstream ssToSend;
                    ssToSend <<gMessagePrefix<< wantedName <<" 's last message was "<< difftime( msg.time , wantedUserIterator->second.lastSeen )<< " seconds ago.\r\n";
                    gSocket.STsend( ssToSend.str().c_str() , ssToSend.str().size()+1 );
                }
                else printf("User %s not found\n", wantedName.c_str());
            }
            printf("\n");
            
            ZeroMemory(recvbuf,  iResult);
            
        }
        else if ( iResult == 0 )
        {
            printf("Connection closed\n");
            running = false;
        }
        else
        {
            errorCode = WSAGetLastError();
            printf("recv failed with error: %d\n",errorCode );
        }
        fflush(stdout);
    }while(running);
    
    gCV.notify_one();
    
    saveEntriesToFile("entries.txt" , entries);
    cmdLineInputThread.join();
    msgSenderThread.join();
    return 0;
}

void saveEntriesToFile( const char *fileName , std::map<std::string, UserEntry> &entries )
{
    std::ofstream saveFile;
    saveFile.open(fileName, std::ios_base::out);
    saveFile<<entries.size()<<'\n';
    for(auto &entryPair : entries)
    {
        UserEntry *entry = &entryPair.second;
        
        saveFile << entryPair.first<<"\n#";
        saveFile.write( reinterpret_cast<char*>(&entryPair.second.lastSeen) , sizeof(time_t)  );
        saveFile << "#\n"<<entryPair.second.messages.size()<<'\n';
        
        for(Message &msg : entryPair.second.messages)
        {
            saveFile << msg.username<<"\n#";
            
            saveFile.write( reinterpret_cast<char*>(&msg.time) , sizeof(time_t)  );
            saveFile <<"#\n"<< msg.messageBody.size()<<'\n';
            saveFile.write(msg.messageBody.c_str(),msg.messageBody.size() )<<'\n';
            
        }
    }
    
    
    saveFile.close();
    
}
bool loadEntries( const char *fileName , std::map<std::string, UserEntry> &entries )
{
    std::ifstream file;
    file.open(fileName);
    if(file.fail())
        return false;
    
    int numberOfEntries;
    file >> numberOfEntries;
    
    for(int i = 0 ; i < numberOfEntries ; i++)
    {
        std::string newlyReadUsername;
        UserEntry newEntry;
        
        file >> newlyReadUsername;
        if(newlyReadUsername.size() == 0)
            return false;
        std::pair<std::string, UserEntry> p(newlyReadUsername , newEntry);
        auto newEntryIterator = entries.insert( p ).first;
        
        char tmp;
        do{
            file.get(tmp);
        }while(tmp!='#');
        
        
        file.read( reinterpret_cast<char*>( &(newEntryIterator->second.lastSeen) ) , sizeof(time_t) );
        file.get(tmp);
        
        int numberOfMessages;
        file >> numberOfMessages;
        
        for(int i = 0 ; i < numberOfMessages ; i++)
        {
            Message newMessage;
            file >> newMessage.username;
            file.get(tmp);file.get(tmp);
            file.read( reinterpret_cast<char*>( &newMessage.time ) , sizeof(time_t) );
            file.get(tmp);file.get(tmp);
            int messageSize;
            file >> messageSize;
            file.get(tmp);
            char block[2048];
            file.read(block, messageSize);
            
            block[messageSize] = 0;
            newMessage.messageBody += block;
            newEntryIterator->second.messages.push_back(newMessage);
        }
        
        
    }
    
    file.close();
    return true;
    
}

void parseMessage(char *buffer ,  Message& msg)
{
    int i = 0;
    while(buffer[i]  && buffer[i] != '!')
        i++;
    buffer[i] = 0;
    
    msg.username = std::string(buffer+1);
    for(int i = 0; i<msg.username.size() ; i++)
    {
        if(msg.username[i] == ' ' || msg.username[i]  =='\n')
        {
            msg.username.erase(i);
            i--;
        }
    }
    buffer[i] = '!';
    
    while(buffer[i]  && buffer[i] != ':')
        i++;
    msg.messageBody = std::string(buffer+i+1);
    time(&msg.time);
}

int tryToSaveMessage(Message& msg , std::map<std::string, UserEntry> & entries , std::stringstream &outputString)
{
    std::string targetName;
    std::stringstream ss(msg.messageBody);
    
    ss>>targetName>>targetName;
    for(char &c : targetName)
        c=tolower(c);
    
    auto iterator = entries.find(targetName);
    
    if(iterator == entries.end())
    {
        outputString << targetName << " not found.\r\n";
        return MESSAGE_SAVE_NO_USER;
    }
    else 
    {
        outputString<< envelope << " sent to "<<targetName;
        msg.messageBody.erase(0,5);
        printf("message to user \"%s\" saved\n", targetName.c_str());
        iterator->second.messages.push_back(msg);
        
        return MESSAGE_SAVE_SUCCESS;
    }
    
}

void sendMessage(STCPSocket  socket, const char* message)
{
    std::string str = gMessagePrefix;
    str += message;
    socket.STsend(str.c_str(), str.size()+1);
    
}

BOOL WINAPI sigintFunc(DWORD signal)
{  
    running =false;
    gCV.notify_one();
    return true;
    
}  

