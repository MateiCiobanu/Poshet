#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>


using namespace std;

#define PreConnectionStage -1
#define AuthorizationStage 0
#define TransactionStage 1
#define UpdateStage 2

extern int errno;
int port;
char POP3message[256];          // aici o sa fie primit orice mesaj de la server
char CLIENTmessage[256];        // aici o sa creez mesajele primite din linia de comanda de la client
char SMTPmessage[256];



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


void handleDataCommand(int socketDescriptor);