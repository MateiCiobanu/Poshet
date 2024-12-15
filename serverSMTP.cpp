#include "serverSMTP.h"


int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t clientLength = sizeof(client);

    // 1. Create a socket
    socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor == -1) 
    {
        perror("[SMTP Server] says: Eroare la socket");
        return errno;
    }

    // Set socket to reuse address
    int on = 1;
    setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // 2. Prepare the socket structure
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(SMTPPORT);

    // 3. Bind the socket
    if (::bind(socketDescriptor, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("[SMTP Server] says: Eroare la bind");
        return errno;
    }

    // 4. Listen for clients
    if (listen(socketDescriptor, 5) == -1) {
        perror("[SMTP Server] says: Eroare la listen");
        return errno;
    }

    // 5. Create the thread pool
    threadsPool = new Thread[numberOfThreads];
    for (int i = 0; i < numberOfThreads; i++)
        pthread_create(&threadsPool[i].tid, NULL, &treat, (void*)(intptr_t)i);


    cout << "[SMTP Server] says: Astept la portul: " << SMTPPORT << endl;
    while (true) 
    {
        int cd = accept(socketDescriptor, (struct sockaddr *)&client, &clientLength);
        if(cd < 0)
        {
             perror("[SMTP Server] says: Eroare la accept");
        }
        else
        {
             enqueueClient(cd);
        }
    }

    return 0;
}


void* treat(void* arg) 
{
    while (true) 
    {
        int clientSocketDescriptor = dequeueClient();

        cout << "[SMTP Server] says: S-a connectat la server clientul: " << clientSocketDescriptor << endl;
        
        bzero(SMTPmessage, sizeof(SMTPmessage));
        strcpy(SMTPmessage, "Conexiunea s-a stabilit cu succes!");
        if(write(clientSocketDescriptor, &SMTPmessage, sizeof(SMTPmessage)) < 0)
        {
            perror("[SMTP Thread] says: Eroare la trimiterea mesajului catre client dupa conectare");
            close(clientSocketDescriptor);
            continue;
        }

        // Procesez clientul pana cand returneaza functia fals
        while(manageClient(clientSocketDescriptor, (intptr_t)arg));

        cout << "[SMTP Thread] says: S-a deconectat de la server clientul: " << clientSocketDescriptor << endl;
        close(clientSocketDescriptor);
    }

    return nullptr;
}


bool manageClient(int clientSocketDescriptor, intptr_t threadId)
{
    CommandLine commandLine;

    while (true) 
    {
        if (!receiveCommand(clientSocketDescriptor, commandLine))
            return false;

        if (!addressCommand(clientSocketDescriptor, commandLine))
            return false;
    }
}


void enqueueClient(int clientSocketDescriptor)
{
    lock_guard<mutex> lock(queueMutex);
    clientQueue.push(clientSocketDescriptor);
    queueCondition.notify_one();
}

int dequeueClient()
{
    unique_lock<mutex> lock(queueMutex);
    queueCondition.wait(lock, []{return !clientQueue.empty(); });
    int cd = clientQueue.front();
    clientQueue.pop();
    return cd;
}


// ar merge adaugat si ceva care sa elimine spatiile albe de la inceputul liniei, ca un trimmer
// tot ar putea sa identifice comanda cu find dar contentul nu ar mai merge 
// AVEM NEVOIE DE TRIMMER
CommandLine lineParser(string line)
{
    string receivedCommand;
    string commandContent;


    if(line.find("QUIT") == 0)
    {
        receivedCommand = "QUIT";
        commandContent = "";
    }
    else if(line.find("HELO") == 0)
    {
        receivedCommand = "HELO";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length() + 1);         // daca as avea multe spatii libere la inceput tot ar parsa bine  (TOT NU MERGE)
        else
            commandContent = "";
    }
    else if(line.find("MAIL FROM:") == 0)
    {
        receivedCommand = "MAIL FROM:";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("RCPT TO:") == 0)
    {
        receivedCommand = "RCPT TO:";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("DATA") == 0)
    {
        receivedCommand = "DATA";
        commandContent = "";
    }
    else
    {
        receivedCommand = "UNKNOWN";
        commandContent = line;
    }
    CommandLine result(receivedCommand, commandContent);
    return result;

}

bool receiveCommand(int clientSocketDescriptor, CommandLine &commandLine)
{
    char receivedCommandLine[1024];
    bzero(receivedCommandLine, sizeof(receivedCommandLine));

    int bytesRead = read(clientSocketDescriptor, receivedCommandLine, sizeof(receivedCommandLine));
    if(bytesRead == 0)          // deonectare client
    {
        cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la citire." << endl;
        return false; 
    }
    else if (bytesRead < 0)     // eroare client
    {
        if(errno == ECONNRESET || errno == EPIPE)
            cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la citire." << endl;
        else
            perror("[SMTP Thread] says: Error la citirea mesajului de la client");
        return false;
    }
    commandLine = lineParser(receivedCommandLine);
    return true;
}

bool addressCommand(int clientSocketDescriptor, CommandLine &commandLine)
{
    string response;

    if (commandLine.command == "QUIT")
    {
        response = "221 Goodbye\n";
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return false;
    }
    else if (commandLine.command == "HELO")
    {
        response = "250 Hello, " + commandLine.content + "! The server is ready!\n";
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }
    else if (commandLine.command == "MAIL FROM:")
    {
        CommandLine terminalCommandLine;
        terminalCommandLine.content = " \"From: ";
        terminalCommandLine.content = terminalCommandLine.content + commandLine.content + "\"";

        terminalCommandLine.command = "echo " + terminalCommandLine.content + " > primulMail.txt"; 

        int terminalStatus = system(terminalCommandLine.command.c_str());
        if (terminalStatus == -1) 
        {
            perror("[SMTP Thread] says: Eroare la executia comenzii in terminal");
            return false;
        }
        else 
            cout << "[SMTP Thread] says: Comanda a fost executata cu succes in terminal" << endl;

        response = terminalCommandLine.command;
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }
    else if (commandLine.command == "RCPT TO:")
    {
        CommandLine terminalCommandLine;
        terminalCommandLine.content = " \"To: ";
        terminalCommandLine.content = terminalCommandLine.content + commandLine.content + "\"";

        terminalCommandLine.command = "echo " + terminalCommandLine.content + " >> primulMail.txt"; 

        int terminalStatus = system(terminalCommandLine.command.c_str());
        if (terminalStatus == -1) 
        {
            perror("[SMTP Thread] says: Eroare la executia comenzii in terminal");
            return false;
        }
        else 
            cout << "[SMTP Thread] says: Comanda a fost executata cu succes in terminal" << endl;

        response = terminalCommandLine.command;
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }
    else if (commandLine.command == "DATA")
    {
        response = "354 Start mail input; end with <CRLF>.<CRLF>";
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Clientul a inceput sa scrie un mail " << endl;

        string emailBody;
        char buffer[1024];
        bool isReading = true;

        while (isReading) 
        {
            bzero(buffer, sizeof(buffer));
            int bytesRead = read(clientSocketDescriptor, buffer, sizeof(buffer));
            if (bytesRead <= 0) 
            {
                if (bytesRead == 0)
                    cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat." << endl;
                else
                    perror("[SMTP Thread] says: Error la citirea emailului de la client");
                return false;
            }

            buffer[bytesRead] = '\0'; // Null-terminate the buffer
            string line(buffer);
            line.erase(line.find_last_not_of("\r\n") + 1); // Remove CRLF

            if (line == ".") 
                isReading = false; 
            else 
            {
                if (!line.empty() && line[0] == '.') 
                {
                    line = line.substr(1); // Handle dot-stuffing
                }
                emailBody += line + "\n";
            }
        }

        CommandLine terminalCommandLine;
        terminalCommandLine.content = "\"" + emailBody + "\"";
        terminalCommandLine.command = "echo " + terminalCommandLine.content + " >> primulMail.txt"; 

        int terminalStatus = system(terminalCommandLine.command.c_str());
        if (terminalStatus == -1) 
        {
            perror("[SMTP Thread] says: Eroare la executia comenzii in terminal");
            return false;
        }
        else 
            cout << "[SMTP Thread] says: Comanda a fost executata cu succes in terminal" << endl;


        terminalCommandLine.content.clear();
        terminalCommandLine.command.clear();
        
        terminalCommandLine.command = "msmtp --debug --from=default -t < primulMail.txt";

        terminalStatus = system(terminalCommandLine.command.c_str());
        if (terminalStatus == -1) 
        {
            perror("[SMTP Thread] says: Eroare la executia comenzii in terminal");
            return false;
        }
        else 
            cout << "[SMTP Thread] says: Comanda a fost executata cu succes in terminal" << endl;


        cout << "[SMTP Thread] says: Mailul scris de client este:" << endl << emailBody << endl;

        // Acknowledge message receipt
        response = "250 OK, message accepted";
        if (write(clientSocketDescriptor, response.c_str(), response.size()) <= 0) {
            perror("[SMTP Thread] says: Error sending 250 response");
            return false;
        }
        return true;
    }
    else
    {
        response = "502 Uknown Command";
        if (write(clientSocketDescriptor, response.c_str(),response.size()) <= 0) 
        {
            if(errno == ECONNRESET || errno == EPIPE)
                cout << "[SMTP Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
            else 
                perror("[SMTP Thread] says: Error sending back client message");
            return false;
        }
        cout << "[SMTP Thread] says: Comanda necunoscuta de la clientul (" << clientSocketDescriptor << "): " << commandLine.command << endl;

        // commandLine.content.clear();
        // commandLine.command.clear();

        return true;
    }
}
