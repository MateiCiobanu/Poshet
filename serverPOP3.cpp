#include "serverPOP3.h"


int main(int argc, char *argv[]) 
{
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open("ProiectRetele.db", &dataBase);
    if(rc < 0)
    {
        cout << "Can't open database: %s\n", sqlite3_errmsg(dataBase);
        return 0;
    }
    else
        cout << "Opened database successfully\n";

    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t clientLength = sizeof(client);

    // 1. Create a socket
    socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor == -1) 
    {
        perror("[POP3 Server] says: Eroare la socket");
        return errno;
    }

    // Set socket to reuse address
    int on = 1;
    setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // 2. Prepare the socket structure
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(POP3PORT);

    // 3. Bind the socket
    if (::bind(socketDescriptor, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("[POP3 Server] says: Eroare la bind");
        return errno;
    }

    // 4. Listen for clients
    if (listen(socketDescriptor, 5) == -1) {
        perror("[POP3 Server] says: Eroare la listen");
        return errno;
    }

    // 5. Create the thread pool
    threadsPool = new Thread[numberOfThreads];
    for (int i = 0; i < numberOfThreads; i++)
        pthread_create(&threadsPool[i].tid, NULL, &treat, (void*)(intptr_t)i);


    cout << "[POP3 Server] says: Astept la portul: " << POP3PORT << endl;
    while (true) 
    {
        int cd = accept(socketDescriptor, (struct sockaddr *)&client, &clientLength);
        if(cd < 0)
        {
             perror("[POP3 Server] says: Eroare la accept");
             return 1;
        }
        else
        {
             enqueueClient(cd);
        }
    }

    return 0;
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

void* treat(void* arg) 
{
    while (true) 
    {
        int clientSocketDescriptor = dequeueClient();

        cout << "[POP3 Server] says: S-a connectat la server clientul: " << clientSocketDescriptor << endl;
        
        bzero(SMTPmessage, sizeof(SMTPmessage));
        strcpy(SMTPmessage, "+OK POP3 server ready \r\n");
        if(write(clientSocketDescriptor, &SMTPmessage, sizeof(SMTPmessage)) < 0)
        {
            perror("[POP3 Thread] says: Eroare la trimiterea mesajului catre client dupa conectare");
            close(clientSocketDescriptor);
            continue;
        }

        Stage clientStage;
        clientStage.SetStage(AuthorizationStage);

        // Procesez clientul pana cand returneaza functia fals
        while(manageClient(clientSocketDescriptor, (intptr_t)arg, &clientStage));

        cout << "[POP3 Thread] says: S-a deconectat de la server clientul: " << clientSocketDescriptor << endl;
        close(clientSocketDescriptor);
    }
    return nullptr;
}

// Functia returneaza fals atunci cand apare o eroare la receiveCommand sau la addressCommand
bool manageClient(int clientSocketDescriptor, intptr_t threadId, Stage *clientStage)
{
    CommandLine commandLine;

    while (true) 
    {
        if (!receiveCommand(clientSocketDescriptor, commandLine)) 
            return false;

        if (!addressCommand(clientSocketDescriptor, commandLine, clientStage))
            return false;
    }
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
    else if(line.find("USER") == 0)
    {
        receivedCommand = "USER";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length() + 1);         // am spatiu dupa USER de aia adaug +1
        else
            commandContent = "";
    }
    else if(line.find("PASS") == 0)
    {
        receivedCommand = "PASS";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length() + 1);         // am spatiu dupa PASS de aia adaug + 1
        else
            commandContent = "";
    }
    else if(line.find("STAT") == 0)
    {
        //AICI CONTENT AR TREBUI SA NU EXISTE
        receivedCommand = "RCPT TO:";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("LIST") == 0)
    {
        receivedCommand = "LIST";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("RETR") == 0)
    {
        receivedCommand = "RETR";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("DELE") == 0)
    {
        receivedCommand = "DELE";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("NOOP") == 0)
    {
        // AICI CONTENT AR TREBUI SA NU EXISTE
        receivedCommand = "LIST";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("RSET") == 0)
    {
        // AICI CONTENT AR TREBUI SA NU EXISTE
        receivedCommand = "RSET";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("TOP") == 0)
    {
        receivedCommand = "TOP";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
            commandContent = "";
    }
    else if(line.find("UIDL") == 0)
    {
        receivedCommand = "UIDL";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length());             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
        else
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
        cout << "[POP3 Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la citire." << endl;
        return false; 
    }
    else if (bytesRead < 0)     // eroare client
    {
        if(errno == ECONNRESET || errno == EPIPE)
            cout << "[POP3 Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la citire." << endl;
        else
            perror("[POP3 Thread] says: Error la citirea mesajului de la client");
        return false;
    }
    commandLine = lineParser(receivedCommandLine);
    return true;
}


bool addressCommand(int clientSocketDescriptor, CommandLine &commandLine, Stage *stage)
{
    // verific daca exisa deja userul
    int userIndex = -1;
    for (int i=0; i<numberOfEmailClientUsers; i++) 
    {
        if (emailClientUsers[i].getUniqueID() == clientSocketDescriptor) 
        {
            userIndex = i;
            break;
        }
    }

    // daca nu exista deja userul, il setez eu in vector
    if (userIndex == -1) 
    {
        emailClientUsers[numberOfEmailClientUsers].setUniqueID(clientSocketDescriptor);
        emailClientUsers[numberOfEmailClientUsers].setStage(AuthorizationStage);
        userIndex = numberOfEmailClientUsers;
        numberOfEmailClientUsers++;
    }
    
    // il folosesc ca sa am comunicare intre USER si PASS
    User &currentEmailClientUser = emailClientUsers[userIndex];
    string response;

    if(stage->GetStage() == AuthorizationStage)
    {
        if (commandLine.command == "QUIT")
            return QUITCommandHandler(clientSocketDescriptor, response);
        else if (commandLine.command == "USER")
            return USERCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "PASS")
            return PASSCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser, stage);
        else
            return UNKNOWNCommandHandler(clientSocketDescriptor, commandLine, response);
    }
    else if(stage->GetStage() == TransactionStage)
    {
        cout << "[POP3 server] says: clientul " << socketDescriptor << " a ajuns in TransactionStage" << endl;
        return false;
    }
    return false;
}

bool checkUserInDatabase(sqlite3 *dataBase, string userName)
{
    sqlite3_stmt *statement;
    string query = "SELECT username FROM users WHERE username = ?";

    // Prepare the SQL statement
    int status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK)
    {
        cerr << "[POP3 Thread] says: Eroare la prepararea query-ului SQL: " << sqlite3_errmsg(dataBase) << endl;
        return false;
    }

    // Bind the username to the placeholder
    sqlite3_bind_text(statement, 1, userName.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the query
    status = sqlite3_step(statement);
    if (status == SQLITE_ROW)
    {
        // User exists
        const char* dbUserName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (dbUserName && strcmp(userName.c_str(), dbUserName) == 0)
        {
            sqlite3_finalize(statement);
            return true;
        }
    }
    else if (status == SQLITE_DONE)
    {
        cout << "[POP3 Thread] says: Nu exista acest user: " << userName << endl;
    }
    else
    {
        cerr << "[POP3 Thread] says: Eroare la executia comenzii SQL: " << sqlite3_errmsg(dataBase) << endl;
    }

    // Clean up and return false if user was not found
    sqlite3_finalize(statement);
    return false;
}

bool checkPasswordInDatabase(sqlite3 *dataBase, string userName, string passWord)
{
    sqlite3_stmt *statement;
    string query = "SELECT password FROM users WHERE username = ?";
    
    // Prepare the SQL statement
    int status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK)
    {
        cerr << "[POP3 Thread] says: Eroare la prepararea query-ului SQL: " << sqlite3_errmsg(dataBase) << endl;
        return false;
    }

    // Bind the username to the placeholder
    sqlite3_bind_text(statement, 1, userName.c_str(), -1, SQLITE_TRANSIENT);

    // Execute the query
    status = sqlite3_step(statement);
    if (status == SQLITE_ROW)
    {
        // Extract the password from the database
        const char* dbPassword = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));

        if (strcmp(passWord.c_str(), dbPassword) == 0)
        {
            sqlite3_finalize(statement);
            return true; // Password matches
        }
        else
        {
            cout << "[POP3 Thread] says: Parola nu se potriveste pentru utilizatorul " << userName << endl;
            sqlite3_finalize(statement);
            return false; // Password does not match
        }
    }
    else if (status == SQLITE_DONE)
    {
        cout << "[POP3 Thread] says: Nu s-a găsit parola pentru utilizatorul: " << userName << endl;
        sqlite3_finalize(statement);
        return false; // User not found or no password
    }
    else
    {
        cerr << "[POP3 Thread] says: Eroare la executia comenzii SQL: " << sqlite3_errmsg(dataBase) << endl;
        sqlite3_finalize(statement);
        return false; // SQL execution error
    }
}

// o functie ca sa fac codul mai lizibil
bool sendResponse(int clientSocketDescriptor, const string &response, const string coutText, const string perrorText) 
{
    if (write(clientSocketDescriptor, response.c_str(), response.size()) <= 0) 
    {
        if (errno == ECONNRESET || errno == EPIPE) 
        {
            cout << coutText << endl;
        } 
        else 
        {
            perror(perrorText.c_str());
        }
        return false;
    }
    return true;
}

bool sendResponse(int clientSocketDescriptor, const string &response) 
{
    if (write(clientSocketDescriptor, response.c_str(), response.size()) <= 0) 
    {
        if (errno == ECONNRESET || errno == EPIPE) 
        {
            cout << "[POP3 Thread] says: Clientul " << clientSocketDescriptor << " s-a deconectat fortat la scriere." << endl;
        } 
        else 
        {
            perror("[POP3 Thread] says: Error sending back client message");
        }
        return false;
    }
    return true;
}

bool USERCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{
    if(!checkUserInDatabase(dataBase, commandLine.content))
    {
        response = "-ERR user not found\n";
        if(!sendResponse(clientSocketDescriptor, response))
            return false;
        cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }

    currentEmailClientUser.setUserName(commandLine.content);
    currentEmailClientUser.setStatusUserName(true);

    response = "+OK user accepted\n";
    if(!sendResponse(clientSocketDescriptor, response))
            return false;
    cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
    return true;
}

bool PASSCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser, Stage *stage)
{
    if(!currentEmailClientUser.getStatusUserName())
    {
        response = "-ERR USER required before PASS\n";
        if(!sendResponse(clientSocketDescriptor, response))
            return false;
        cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }

    if(!checkPasswordInDatabase(dataBase, currentEmailClientUser.getUserName(), commandLine.content))
    {
        response = "-ERR invalid password\n";
        if(!sendResponse(clientSocketDescriptor, response))
            return false;
        cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
        return true;
    }

    //parola e corecta
    currentEmailClientUser.setPassWord(commandLine.content);
    currentEmailClientUser.setStatusPassWord(true);
    stage->SetStage(TransactionStage);

    response = "+OK maildrop ready\n";
    if(!sendResponse(clientSocketDescriptor, response))
            return false;
    cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
    return true;
}

bool QUITCommandHandler(int clientSocketDescriptor, string &response)
{
    response = "+OK\n";
    if(!sendResponse(clientSocketDescriptor, response))
        return false;
    cout << "[POP3 Thread] says: Mesaj trimis clientului (" << clientSocketDescriptor << "): " << response << endl;
    return false;
}

bool UNKNOWNCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response)
{
    response = "-ERR Unknown command\n";
    if(!sendResponse(clientSocketDescriptor, response))
            return false;
    cout << "[POP3 Thread] says: Comanda necunoscuta de la clientul (" << clientSocketDescriptor << "): " << commandLine.command << endl;
    return true;
}
