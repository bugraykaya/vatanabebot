

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
#include <functional>
#include <iostream>



#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60
#define MESSAGE_DELAY 2500
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

typedef std::map<std::string, UserEntry>::iterator It;

void saveEntriesToFile( const char *fileName , std::map<std::string, UserEntry> &entries );
bool loadEntries(const char *fileName , std::map<std::string, UserEntry> &entries );

int  main(int argc, char **argv) 
{
    if(argc < 2)
    {
        printf("Need a text file containing usename, oauth token and channel name as argument.\n");
        return -1;
    }
    
    
    const uint8_t envelope[4]= {0xE2, 0x9C, 0x89, 0x00}; //utf-8 envelope emoji
    char messagePrefix[512];
    char channelName[50];
    bool unsaved = false;
    std::queue<std::string> messageQueue;
    std::mutex queueLock;
    std::mutex entriesLock;
    STCPSocket socket;
    bool result;
    char loginName[30];
    char password[50];
    std::ifstream loginFile;
    //loginFile.open("vatanabe.txt");
    loginFile.open(argv[1]);
    loginFile >> loginName >> password >> channelName;
    printf("name: %s\npass: %s\nchannel: %s\n", loginName, password, channelName );
    
    
    std::string name , pass, channel, prefix;
    name += "NICK ";name += loginName; name+= "\r\n";
    pass += "PASS "; pass += password; pass +=  "\r\n";
    channel += "JOIN #" ;channel += channelName;channel += "\r\n";
    prefix+= "PRIVMSG #"; prefix+= channelName; prefix+= " :";
    
    
    std::vector< std::function<void( std::stringstream& , int)> > insertFunctions;
    insertFunctions.emplace_back([&](std::stringstream &ss, const int seconds)
                                 {
                                 ss << seconds <<" second(s) ago: ";
                                 }
                                 );
    insertFunctions.emplace_back([&](std::stringstream &ss, const int seconds)
                                 {
                                 ss << seconds / SECONDS_PER_MINUTE << " minute(s) ago: ";
                                 }
                                 );
    insertFunctions.emplace_back([&](std::stringstream &ss, const int seconds)
                                 {
                                 ss << seconds / SECONDS_PER_HOUR <<" hour(s) ago: ";
                                 }
                                 );
    insertFunctions.emplace_back([&](std::stringstream &ss, const int seconds)
                                 {
                                 ss << seconds / SECONDS_PER_DAY <<" day(s) ago: ";
                                 }
                                 );
    
    
    
    std::map<std::string, UserEntry> entries;
    result = loadEntries("entries.txt",entries);
    
    if(result)
        printf("Loaded %zd entries\n", entries.size());
    else 
        printf("Could not load entries\n");
    
    
    
    auto parseMessage = [&](char *buffer ,  Message& msg)
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
    };
    
    auto tryToSaveMessage = [&](Message& msg , std::map<std::string, UserEntry> & entries , std::stringstream &outputString)
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
        
    };
    
    
    auto flushMessageQueue = [&]()
    {
        std::string ss(messagePrefix) ;
        queueLock.lock();
        if(!messageQueue.empty())
        {
            ss += messageQueue.front();
            messageQueue.pop();
            if(messageQueue.empty())
            {
                ss += "\r\n";
                socket.Send(ss.c_str(), ss.size()+1);
            }
        }
        while( !messageQueue.empty())
        {
            printf("Queue size: %zd\nFirst message: %s\n", messageQueue.size(), messageQueue.front().c_str());
            queueLock.unlock();
            Sleep(MESSAGE_DELAY);
            queueLock.lock();
            
            while(ss.size()<=100 && !messageQueue.empty())
            {
                ss +='#';
                ss += messageQueue.front();
                
                messageQueue.pop();
                
            }
            ss+="\r\n";
            socket.Send(ss.c_str(), ss.size()+1);
            ss=messagePrefix;
        }
        
        queueLock.unlock();
    };
    
    auto broadcastMessage = [&](Message& msg , std::map<std::string, UserEntry> &entries)
    {
        std::stringstream ss(msg.messageBody);
        msg.messageBody.erase(0,11);
        int numberOfUsers = 0;
        for(auto& entry : entries)
        {
            printf("message to user \"%s\" saved\n", entry.first.c_str());
            entry.second.messages.push_back(msg);
            numberOfUsers++;
        }
        return numberOfUsers;
    };
    
    
    
    
    
    bool running = true;
    std::condition_variable sendCV;
    std::condition_variable saveCV;
    std::thread cmdLineInputThread([&]()
                                   {
                                   while(getchar() != 'q')
                                   {
                                   printf("not q\n");
                                   }
                                   printf("is q\n");
                                   running = false;
                                   }
                                   );
    
    std::thread msgSenderThread([&]()
                                {
                                std::mutex CVMutex;
                                std::unique_lock<std::mutex> CVLock(CVMutex);
                                
                                int msec = GetTickCount();
                                while(running)
                                {
                                sendCV.wait(CVLock);
                                printf("Notified\n");
                                
                                int newMsec = GetTickCount();
                                int diff = newMsec - msec;
                                msec = newMsec;
                                Sleep( (diff < MESSAGE_DELAY ) * ( MESSAGE_DELAY - diff) ); 
                                flushMessageQueue();
                                
                                }
                                
                                }
                                );
    
    std::thread saveFileThread([&]()
                               {
                               std::mutex CVMutex;
                               std::unique_lock<std::mutex> CVLock(CVMutex);
                               
                               while(running)
                               {
                               if(saveCV.wait_for(CVLock, std::chrono::minutes(5)) == std::cv_status::timeout)
                               printf("5 minutes passed. Saving\n");
                               else
                               printf("Exiting program\n");
                               
                               entriesLock.lock();
                               saveEntriesToFile("entries.txt" , entries);
                               unsaved = false;
                               entriesLock.unlock();
                               }
                               }
                               );
    
    
    std::map<std::string, std::function<void( Message & , std::map<std::string , UserEntry>::iterator & )>> commands;
    
    commands.insert( std::make_pair("msg",
                                    [&](Message &msg, std::map<std::string,UserEntry>::iterator &iterator)
                                    {
                                    std::stringstream outputString;
                                    entriesLock.lock();
                                    int result = tryToSaveMessage(msg, entries, outputString);
                                    unsaved = true;
                                    entriesLock.unlock();
                                    
                                    queueLock.lock();
                                    messageQueue.push(outputString.str());
                                    printf("Added to queue\n");
                                    queueLock.unlock();
                                    sendCV.notify_one();
                                    })
                    );
    
    commands.insert( std::make_pair("anon",
                                    [&](Message &msg, std::map<std::string,UserEntry>::iterator &iterator)
                                    {
                                    msg.username = "Anonymous";
                                    std::stringstream outputString;
                                    
                                    entriesLock.lock();
                                    int result = tryToSaveMessage(msg, entries, outputString);
                                    unsaved = true;
                                    entriesLock.unlock();
                                    
                                    queueLock.lock();
                                    messageQueue.push(outputString.str());
                                    printf("Added to queue\n");
                                    queueLock.unlock();
                                    sendCV.notify_one();
                                    
                                    }
                                    )
                    );
    
    commands.insert( std::make_pair("broadcast",
                                    [&](Message &msg, std::map<std::string,UserEntry>::iterator &iterator)
                                    {
                                    std::stringstream ss;
                                    if(msg.username != "stc_")
                                    ss<<messagePrefix <<msg.username<<" you are not stc_ enough to use this command.\r\n";
                                    else 
                                    {
                                    entriesLock.lock();
                                    msg.username = "BC";
                                    int result = broadcastMessage(msg, entries);
                                    unsaved = true;
                                    entriesLock.unlock();
                                    ss<<envelope<<" sent to "<<result <<" users.";
                                    }
                                    queueLock.lock();
                                    messageQueue.push(ss.str());
                                    printf("Added to queue\n");
                                    queueLock.unlock();
                                    sendCV.notify_one();
                                    
                                    }
                                    )
                    );
    
    commands.insert( std::make_pair("list"  , 
                                    [&](Message &msg , std::map<std::string,UserEntry>::iterator &iterator)
                                    {
                                    int numberOfMessages = iterator->second.messages.size();
                                    if(numberOfMessages == 0)
                                    {
                                    std::stringstream ss;
                                    ss<< messagePrefix<< "You have no new messages.\r\n";
                                    std::string str = ss.str();
                                    socket.Send(str.c_str(), str.size()+1);
                                    }
                                    else
                                    {
                                    
                                    for(int i= 0; running && i < numberOfMessages ; i++)
                                    {
                                    std::stringstream ss;
                                    if(iterator->second.messages[i].username != "BC")
                                    ss <<messagePrefix<< "From "<< iterator->second.messages[i].username<<" ";
                                    else 
                                    ss <<messagePrefix<< "Broadcast message ";
                                    
                                    int seconds = difftime( time(NULL) ,iterator->second.messages[i].time);
                                    if(seconds < 0)
                                    ss << "once upon a time ";
                                    else
                                    {
                                    int days = seconds/SECONDS_PER_DAY;
                                    int hours = seconds/SECONDS_PER_HOUR;
                                    int minutes = seconds/SECONDS_PER_MINUTE;
                                    int functionIndex =static_cast<int>(days>0) +  
                                    static_cast<int>(hours>0) +
                                    static_cast<int>(minutes>0); 
                                    insertFunctions[ functionIndex ](ss, seconds);
                                    
                                    }
                                    ss << iterator->second.messages[i].messageBody<<"\r\n" ;
                                    std::string str = ss.str();
                                    socket.Send( str.c_str(), str.size()+1);
                                    Sleep(MESSAGE_DELAY);
                                    
                                    }
                                    iterator->second.messages.clear();
                                    }
                                    
                                    }
                                    )
                    );
    
    
    commands.insert( std::make_pair("lastseen"  , 
                                    [&](Message &msg, std::map<std::string,UserEntry>::iterator &iterator)
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
                                    ssToSend <<messagePrefix<< wantedName <<" 's last message was "<< difftime( msg.time , wantedUserIterator->second.lastSeen )<< " seconds ago.\r\n";
                                    std::string str = ssToSend.str();
                                    socket.Send( str.c_str() , str.size()+1 );
                                    }
                                    else printf("User %s not found\n", wantedName.c_str());
                                    }
                                    )
                    );
    
    
    commands.insert( std::make_pair("purge"  , 
                                    [&](Message &msg, std::map<std::string,UserEntry>::iterator &iterator)
                                    {
                                    std::stringstream ssToSend;
                                    ssToSend << messagePrefix ;
                                    
                                    if(msg.username != "stc_")
                                    ssToSend <<msg.username<<" you are not stc_ enough to use this command.\r\n";
                                    else {
                                    char c = 'a';
                                    std::stringstream ss(msg.messageBody);
                                    std::string wantedName;
                                    
                                    while(c!=' ')
                                    ss.get(c);
                                    ss>>wantedName;
                                    
                                    for(char & c: wantedName)
                                    c = tolower(c);
                                    
                                    ssToSend << wantedName;
                                    
                                    auto wantedUserIterator = entries.find(wantedName);
                                    if(wantedUserIterator != entries.end())
                                    {
                                    printf("user is %s\n",wantedName.c_str());
                                    
                                    ssToSend <<" 's "<< wantedUserIterator-> second.messages.size()<<" messages were deleted.\r\n";
                                    wantedUserIterator-> second.messages.clear();
                                    }
                                    else 
                                    ssToSend<<" not found.\r\n";
                                    }
                                    std::string str = ssToSend.str();
                                    socket.Send( str.c_str() , str.size()+1 );
                                    
                                    }
                                    )
                    );
    
    
    result = socket.Connect("irc.twitch.tv" , "6667");
    if(!result)
    {
        printf("Can not connect\n");
        exit(0);
    }
    
    int i;
    for(i = 0; i < prefix.size() ; i++)
        messagePrefix[i] = prefix[i];
    messagePrefix[i]=0;
    
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;
    iResult = socket.Send( pass.c_str() , pass.size()+1 );
    iResult = socket.Send( name.c_str() , name.size()+1 );
    iResult = socket.Send(  channel.c_str(), channel.size()+1);
    
    if (iResult == SOCKET_ERROR) {
        printf("Send failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    iResult = socket.Recv( recvbuf, recvbuflen);
    printf("%s\n", recvbuf);
    fflush(stdout);
    
    
    Message msg;
    ZeroMemory(recvbuf,  recvbuflen);
    int errorCode = 0;
    
    std::stringstream ss;
    ss << messagePrefix <<envelope<<"\r\n";
    socket.Send(ss.str().c_str(), ss.str().size() +1 );
    
    
    do{
        
        iResult = socket.Recv(recvbuf, recvbuflen-1);
        if(iResult > 1)
            recvbuf[iResult-2] = 0;
        
        if ( iResult > 0 )
        {
            if(recvbuf[0] == 'P' && recvbuf[1] == 'I' && recvbuf[2] == 'N' && recvbuf[3] == 'G')
            {
                std::cout << "PING"<<std::endl;
                socket.Send("PONG\r\n", 6);
                ZeroMemory(recvbuf, recvbuflen);
                continue;
            }
            
            if( strstr(recvbuf, "PRIVMSG") == NULL )
            {
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
            }
            else
            {
                int seconds = difftime( msg.time , iterator->second.lastSeen );
                iterator->second.lastSeen = msg.time;
                if(seconds > 7200)
                {
                    std::stringstream ss;
                    int numberOfMessages = iterator->second.messages.size();
                    ss<< messagePrefix << "Welcome back, "<<msg.username<<" . ";
                    if(numberOfMessages > 0)
                        ss<< "You have "<< numberOfMessages << " new messages. Type !list to see them.";
                    
                    ss << "\r\n";
                    
                    std::string str = ss.str();
                    socket.Send(str.c_str() , str.size()+1 );
                }
                
            }
            
            if(msg.messageBody[0] == '!' )
            {
                int space=0;
                
                while(msg.messageBody[space] != '\r' && msg.messageBody[space] != ' ')
                    space++;
                
                std::string command(msg.messageBody, 1,space-1);
                printf("Command is %s.\n", command.c_str());
                
                auto commandIterator = commands.find(command);
                if(commandIterator != commands.end())
                    commandIterator->second(msg, iterator);
                
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
    
    sendCV.notify_one();
    saveCV.notify_one();
    
    cmdLineInputThread.join();
    msgSenderThread.join();
    saveFileThread.join();
    
    return 0;
}

void saveEntriesToFile( const char *fileName , std::map<std::string, UserEntry> &entries )
{
    std::ofstream saveFile;
    saveFile.open(fileName, std::ios_base::out);
    saveFile<<entries.size();
    for(auto &entryPair : entries)
    {
        saveFile<<"<e>";
        UserEntry *entry = &entryPair.second;
        
        saveFile <<"<n>"<<entryPair.first<<"</n><t>";
        saveFile.write( reinterpret_cast<char*>(&entryPair.second.lastSeen) , sizeof(time_t)  );
        saveFile << "</t><s>"<<entryPair.second.messages.size()<<"</s>";
        
        for(Message &msg : entryPair.second.messages)
        {
            saveFile <<"<m>";
            saveFile <<"<n>"<< msg.username<<"</n><t>";
            
            saveFile.write( reinterpret_cast<char*>(&msg.time) , sizeof(time_t)  );
            saveFile <<"</t><mb>";
            saveFile.write(msg.messageBody.c_str(),msg.messageBody.size() )<<"</mb>";
            saveFile <<"</m>";
            
        }
        saveFile<<"</e>";
        
    }
    std::cout << "Saved "<<entries.size()<<" entries.\n";
    
    
    saveFile.close();
    
}


char* extractInt(char *str, int &number)
{
    number= 0;
    while(!isdigit(*str))
        str++;
    while(isdigit(*str))
    {
        number = number*10 + (*str++ - '0');
    }
    return str;
}


int readTag(char* buffer, std::string& tag)
{
    
    if(buffer[0] != '<')
    {
        DebugBreak();
        std::cout << "Error: Expected '<', found '"<<buffer[0]<<"'\n";
        return 0;
    }
    int i = 1;
    while(buffer[i]!='>')
        tag += buffer[i++];
    return i;
}

int readMessage(char* buffer, UserEntry &entry)
{
    char *begin = buffer;
    Message msg;
    std::string tag = "";
    int tagLen = readTag(buffer , tag);
    if(tagLen == 0 || tag != "n")
    {
        std::cout << "Error: Expected message name.\n";
        return -1;
    }
    buffer += tagLen +1;
    
    char * endOfName = strstr(buffer, "</n>");
    msg.username = std::string(buffer, endOfName - buffer);
    buffer = endOfName + 4;
    
    tag = "";
    tagLen = readTag(buffer , tag);
    if(tagLen == 0 || tag != "t")
    {
        std::cout << "Error: Expected message time.\n";
        return -1;
    }
    
    buffer += tagLen +1;
    msg.time = *reinterpret_cast<time_t*>(buffer);
    buffer += sizeof(time_t);
    while(*buffer != '<')
        buffer++;
    buffer +=  4;
    
    
    tag = "";
    tagLen = readTag(buffer , tag);
    if(tagLen == 0 || tag != "mb")
    {
        std::cout << "Error: Expected message body.\n";
        return -1;
    }
    buffer += tagLen +1;
    
    char * endOfBody = strstr(buffer, "</mb>");
    msg.messageBody = std::string(buffer, endOfBody - buffer);
    
    buffer = endOfBody + 5;
    
    entry.messages.push_back(msg);
    return buffer - begin;
}

int readEntry(char* buffer, std::pair<std::string , UserEntry> &entryPair)
{
    char *begin = buffer;
    int i = 0;
    std::string tag;
    int tagLen = readTag(buffer, tag);
    
    if(tagLen == 0 || tag != "n")
    {
        std::cout << "Error: Expected entry name.\n";
        return -1;
    }
    tag = "";
    buffer += tagLen + 1;
    char * endOfName = strstr(buffer, "</n>");
    entryPair.first = std::string(buffer, endOfName - buffer);
    
    buffer = endOfName + 4;
    tagLen = readTag(buffer, tag);
    if(tagLen == 0 || tag != "t")
    {
        std::cout << "Error: Expected time.\n";
        return -1;
    }
    buffer += tagLen+1;
    entryPair.second.lastSeen = *reinterpret_cast<time_t*>(buffer);
    buffer+=sizeof(time_t);
    while(*buffer != '<')
        buffer++;
    buffer +=  4;
    
    tag = "";
    tagLen = readTag(buffer, tag);
    if(tagLen == 0 || tag != "s")
    {
        std::cout << "Error: Expected number of messages for user "<<entryPair.first<<'\n';
        return -1;
    }
    tag = "";
    int numberOfMessages;
    buffer = extractInt(buffer, numberOfMessages);
    
    buffer += 4;
    std::cout << "User "<<entryPair.first<<" has "<<numberOfMessages<<" messages\n";
    
    for(int m = 0; m<numberOfMessages ; m++)
    {
        tag = "";
        tagLen = readTag(buffer , tag);
        if(tagLen == 0 || tag != "m")
        {
            DebugBreak();
            std::cout << "Error: Expected message.\n";
            return -1;
        }
        buffer += tagLen +1;
        
        int result = readMessage(buffer , entryPair.second);
        buffer += result;
        tag = "";
        tagLen = readTag(buffer , tag);
        if(tagLen == 0 || tag != "/m")
        {
            std::cout << "Error: Expected end of message.\n";
            return -1;
        }
        
        buffer += tagLen +1;
        
    }
    
    
    return buffer - begin;
}

bool loadEntries(const char *fileName , std::map<std::string, UserEntry> &entries )
{
    std::ifstream sizefile( fileName, std::ios::binary | std::ios::ate);
    size_t fileSize = sizefile.tellg();
    sizefile.seekg(0);
    char *buffer = new char[fileSize];
    
    memset(buffer, 0 ,fileSize);
    sizefile.read(buffer, fileSize);
    sizefile.close();
    
    
    size_t pos = 0;
    int numberOfEntries= 0;
    
    while(isdigit(buffer[pos]))
    {
        numberOfEntries = numberOfEntries*10 + (buffer[pos++] - '0');
    }
    std::cout <<" Loading "<<numberOfEntries<<" entries.\n";
    for(int i = 0 ; i<numberOfEntries ; i++)
    {
        std::string tag;
        int tagLen = readTag(buffer+pos,tag );
        
        if(tagLen == 0 || tag != "e")
        {
            std::cout << "Error: Expected '<e>'\n";
            return false;
        }
        pos += tagLen+1;
        UserEntry entry;
        std::pair<std::string , UserEntry> entryPair;
        int result = readEntry(buffer + pos , entryPair);
        pos += result;
        
        tag = "";
        tagLen = readTag(buffer+pos,tag );
        if(tagLen == 0 || tag != "/e")
        {
            std::cout << "Error: Expected '</e>'\n";
            return false;
        }
        
        pos += tagLen+1;
        
        std::cout << "Loaded entry for user "<<entryPair.first<<'\n';
        entries.insert(entryPair);
    }
    return true;
}
