#include "client.h"
#include "clientUI.h"


int main(int argc, char *argv[])
{
    Stage clientStage;
    int socketDescriptor;       // descriptorul de socket ca sa pot interactiona cu socket-ul pe care il creez 
    struct sockaddr_in server;  // o structira de tip sockaddr_in ca sa creez socket-ul pentru server 
    port = atoi(argv[2]);


    // 1. Crearea unui socket
    socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if(socketDescriptor == -1)
    {
        perror("[Client] says: Eroare la creare socket\n");
        return errno;
    }

    // 2. Pregatirea structurilor de date 
    clientStage.SetStage(-1);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);


    // 3. Conectarea la server 
    if (connect(socketDescriptor, (struct sockaddr *) &server, sizeof(server)) == -1)
    {
        perror("[Client] says: Eroare la conectarea la server\n");
        return errno;
    }

    // 4. Citesc mesajul de la server daca a reusit conectarea 
    bzero(SMTPmessage, sizeof(SMTPmessage));
    if (read(socketDescriptor, &SMTPmessage, sizeof(SMTPmessage)) < 0)
    {
        perror("[Client] says: Eroare la citirea mesajului 1 primit de la server dupa conectare");
        return errno;
    }
        cout << SMTPmessage << endl;

    while(true)
    {
        cout << "Introduceti comanda: " << endl;
        bzero(CLIENTmessage, sizeof(CLIENTmessage));
        bzero(POP3message, sizeof(POP3message));
        bzero(SMTPmessage, sizeof(SMTPmessage));
        cin.getline(CLIENTmessage, sizeof(CLIENTmessage));


        //Gestionez in special comanda DATA
        if(strcmp(CLIENTmessage, "DATA") == 0)
        {
            handleDataCommand(socketDescriptor);
            continue; 
        }

        if (write(socketDescriptor, &CLIENTmessage, sizeof(CLIENTmessage)) < 0)
        {
            perror("[Client] says: Eroare la trimiterea mesajului din linia de comanda catre server");
            return errno;
        }

        if (read(socketDescriptor, &SMTPmessage, sizeof(SMTPmessage)) < 0)
        {
            perror("[Client] says: Eroare la citirea mesajului 2 primit de la server dupa conectare");
            return errno;
        }
        cout << SMTPmessage;

        if(strcmp(CLIENTmessage, "QUIT") == 0)
        {
            cout << "[Client] says: Te-ai deconectat." << endl;
            break;
        }
    }

    close(socketDescriptor);

}


void handleDataCommand(int socketDescriptor) {

    if (write(socketDescriptor, "DATA\r\n", strlen("DATA\r\n")) < 0) 
    {
        perror("[Client] says: Eroare la trimiterea comenzii DATA");
        exit(errno);
    }

    bzero(SMTPmessage, sizeof(SMTPmessage));
    if (read(socketDescriptor, SMTPmessage, sizeof(SMTPmessage)) < 0) 
    {
        perror("[Client] says: Eroare la citirea raspunsului pentru DATA");
        exit(errno);
    }
    cout << SMTPmessage << endl;


    string line;
    while (true) 
    {
        getline(cin, line);
        if (line == ".") 
        {
            line += "\r\n";
            if (write(socketDescriptor, line.c_str(), line.size()) < 0)
            {
                perror("[Client] says: Eroare la trimiterea liniei de final de mail");
                exit(errno);
            }
            break;
        }
        line += "\r\n";
        if (write(socketDescriptor, line.c_str(), line.size()) < 0) 
        {
            perror("[Client] says: Eroare la trimiterea continutului emailului");
            exit(errno);
        }
    }

    bzero(SMTPmessage, sizeof(SMTPmessage));
    if (read(socketDescriptor, SMTPmessage, sizeof(SMTPmessage)) < 0) 
    {
        perror("[Client] says: Eroare la citirea raspunsului final pentru DATA");
        exit(errno);
    }
    cout << SMTPmessage << endl;
}