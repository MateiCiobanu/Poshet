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


using namespace std;

#define SMTPPORT 8013
#define POP3PORT 8014
#define PreConnectionStage -1
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


class Stage {
private:
    int numberOfStage;
public:
    Stage()
    {
        numberOfStage = 0;
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
void raspunde(int cl,int idThread);
void createThread(int nr);
bool respond(int clientSD, int tid, Stage *currentStage);
CommandLine lineParser(string line);
bool CheckForQuitMessage(const char* receivedCommandLine);




bool manageClient(int clientSocketDescriptor, intptr_t threadId);
void enqueueClient(int clientSocketDescriptor);     //pune clientii intr-o coada
int dequeueClient();                                //imi returneaza primul client din coada   

bool receiveCommand(int clientSocketDescriptor, CommandLine &commandLine);
bool addressCommand(int clientSocketDescriptor, CommandLine &commandLine);
bool handleDataCommand(int clientSocketDescriptor);