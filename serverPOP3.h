#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>     //pt system 
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <queue>
#include <condition_variable>
#include <string>


using namespace std;

#define POP3PORT 8014
#define AuthorizationStage 0
#define TransactionStage 1
#define UpdateStage 2


queue<int> clientQueue;
mutex queueMutex;
condition_variable queueCondition;


extern int errno;
char POP3message[256];          //pentru fiecare mesaj pe care serverul il va trimite clientului dupa fiecare comanda
char SMTPmessage[256];
char CLIENTmessage[256];        //pentru fiecare mesaj pe care clientul il va trimite serverului
int numberOfThreads = 10;
pthread_mutex_t mutexLock = PTHREAD_MUTEX_INITIALIZER;  // variabila mutex ce va fi partajata de thread-uri
mutex cout_mutex;                  // Mutex for console output
int socketDescriptor;
sqlite3 *dataBase;


class Stage {
private:
    int numberOfStage;
public:
    Stage()
    {
        numberOfStage = AuthorizationStage;
    }
    void SetStage(int number)
    {
        numberOfStage = number;
    }
    int GetStage()
    {
        return numberOfStage;
    }
};

struct Thread {
    pthread_t tid;      // thread ID 
    int threadCount;    // nr de conexiuni servite
};

Thread *threadsPool = nullptr;    // un array de structuri de tip thread 


class User {
private:
    int uniqueID;
    string userName;
    string passWord;
    bool statusUserName;
    bool statusPassWord;
    int stage;
public:
    User(){
        uniqueID = -1;
        userName = "";
        passWord = "";
        statusUserName = false;
        statusPassWord = false;
        stage = AuthorizationStage;
    }
    User(int uniqueID, string userName, string passWord, bool statusUserName, bool statusPassWord, int stage)
    {
        this->uniqueID = uniqueID;
        this->userName = userName;
        this->passWord = passWord;
        this->statusUserName = statusUserName;
        this->statusPassWord = statusPassWord;
        this->stage = stage;
    }
    void setUniqueID(int uniqueID)
    {
        this->uniqueID = uniqueID;
    }
    void setUserName(string userName)
    {
        this->userName = userName;
    }
    void setPassWord(string passWord)
    {
        this->passWord = passWord;
    }
    void setStatusUserName(bool statusUserName)
    {
        this->statusUserName = statusUserName;
    }
    void setStatusPassWord(int statusPassWord)
    {
        this->statusPassWord = statusPassWord;
    }
    void setStage(int stage)
    {
        this->stage = stage;
    }
    int getUniqueID()
    {
        return this->uniqueID;
    }
    string getUserName()
    {
        return this->userName;
    }
    int getEmailClientNumber(int uniqueID)
    {
        return uniqueID-100;
    }
    bool getStatusUserName()
    {
        return statusUserName;
    }

};
User emailClientUsers[100];
static int numberOfEmailClientUsers=0;



struct CommandLine{
public:
    string command;
    string content;
    CommandLine() : command(""), content("") {}
    CommandLine(const char *command, const char *content)
    {
        this->command = command;
        this->content = content;
    }
    CommandLine(string command, string content)
    {
        this->command = command;
        this->content = content;
    }
};


// Functia executata de fiecare thread ce realizeaza comunicarea cu fiecare client
static void *treat(void *);
void createThread(int nr);
bool respond(int clientSD, int tid, Stage *currentStage);
CommandLine lineParser(string line);
bool CheckForQuitMessage(const char* receivedCommandLine);




bool manageClient(int clientSocketDescriptor, intptr_t threadId, Stage *clientStage);
void enqueueClient(int clientSocketDescriptor);     //pune clientii intr-o coada
int dequeueClient();                                //imi returneaza primul client din coada   

bool receiveCommand(int clientSocketDescriptor, CommandLine &commandLine);
bool addressCommand(int clientSocketDescriptor, CommandLine &commandLine, Stage *stage);


bool checkUserInDatabase(sqlite3 *dataBase, string userName);
bool checkPasswordInDatabase(sqlite3 *dataBase, string userName, string ID);
void setUser (User users[], string userName);
void generateUniqueUserID (int socketDescriptor);
bool sendResponse(int clientSocketDescriptor, const string &response);
bool sendResponse(int clientSocketDescriptor, const string &response, const string coutText, const string perrorText);
bool QUITCommandHandler(int clientSocketDescriptor, string &response);
bool UNKNOWNCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response);
bool USERCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser);
bool PASSCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser, Stage *stage);