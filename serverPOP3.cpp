#include "serverPOP3.h"


int main(int argc, char *argv[]) 
{
    sqlite3_stmt *stmt;
    int rc;

    if (sqlite3_config(SQLITE_CONFIG_SERIALIZED) != SQLITE_OK) 
    {
        cout << "[POP3 Server] says: Eroare la configurarea modului serialized pentru SQLite" << endl;
        return 1;
    }
    

    rc = sqlite3_open("ProiectRetele.db", &dataBase);
    if(rc < 0)
    {
        cout << "Can't open database: %s\n", sqlite3_errmsg(dataBase);
        return 0;
    }
    else
        cout << "Opened database successfully\n";

    sqlite3_exec(dataBase, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

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
        receivedCommand = "STAT";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length() + 1);             //aici nu adaug +1 pentru ca adresele trebuie sa fie lipite de ":""
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
            commandContent = line.substr(receivedCommand.length() + 1);         // am spatiu dupa RETR de aia adaug + 1 (trebuie sa iau numarul)
        else                                                                    // trebuie sa folosesc atoi(content) pentru ca am un string cu un numar
            commandContent = "";
    }
    else if(line.find("DELE") == 0)
    {
        receivedCommand = "DELE";
        if(line.length() > receivedCommand.length() + 1)                        //verific daca utilizatorul a trimis si content 
            commandContent = line.substr(receivedCommand.length() + 1);         //am spatiu dupa RETR de aia adaug + 1 (trebuie sa iau numarul)
        else                                                                    //trebuie sa folosesc atoi(content) pentru ca am un string cu un numar
            commandContent = "";
    }
    else if(line.find("NOOP") == 0)
    {
        // AICI CONTENT AR TREBUI SA NU EXISTE
        receivedCommand = "NOOP";
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


bool addressCommand(int clientSocketDescriptor, CommandLine &commandLine, Stage *clientStage)
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

    if(clientStage->GetStage() == AuthorizationStage)
    {
        if (commandLine.command == "QUIT")
            return QUITCommandHandler(clientSocketDescriptor, response, clientStage, currentEmailClientUser);
        else if (commandLine.command == "USER")
            return USERCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "PASS")
            return PASSCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser, clientStage);
        else
            return UNKNOWNCommandHandler(clientSocketDescriptor, commandLine, response);
    }
    else if(clientStage->GetStage() == TransactionStage)
    {
        //cout << "[POP3 server] says: clientul " << socketDescriptor << " a ajuns in TransactionStage" << endl;
        if (commandLine.command == "QUIT")
            return QUITCommandHandler(clientSocketDescriptor, response, clientStage, currentEmailClientUser);
        else if (commandLine.command == "STAT")
            return STATCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "RETR")
            return RETRCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "DELE")
            return DELECommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "NOOP")
            return NOOPCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else if (commandLine.command == "RSET")
            return RSETCommandHandler(clientSocketDescriptor, commandLine, response, currentEmailClientUser);
        else
            return UNKNOWNCommandHandler(clientSocketDescriptor, commandLine, response);
    }
    else if (clientStage->GetStage() == TransactionStage)
    {
        cout << "[POP3 server] says: clientul " << socketDescriptor << " a ajuns in TransactionStage" << endl;
        if (commandLine.command == "QUIT")
            return QUITCommandHandler(clientSocketDescriptor, response, clientStage, currentEmailClientUser);
        else
            return UNKNOWNCommandHandler(clientSocketDescriptor, commandLine, response);
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
    // imi pun in structura mea datele din baza de date 
    loadEmailsFromDatabase(currentEmailClientUser);

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

bool QUITCommandHandler(int clientSocketDescriptor, string &response, Stage *clientStage, User &currentEmailClientUser)
{
    if (clientStage->GetStage() == TransactionStage)
    {
        clientStage->SetStage(UpdateStage);
        cout << "[POP3 Thread] says: Clientul " << clientSocketDescriptor  << " a ajuns in UpdateStage." << endl;

        if (!UPDATEhandler(clientSocketDescriptor, currentEmailClientUser))
        {
            response = "-ERR error occurred while updating\n";
            sendResponse(clientSocketDescriptor, response);
            return false;
        }
    }

    response = "+OK POP3 server signing off(maildrop empty)\n";
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

// cu functia asta am si salvat in baza de date toate mailurile utilizatorului 
bool STATCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{

    if(!commandLine.content.empty() || commandLine.content.find_first_not_of(' ') != string::npos)
    {
        int messageNumber = stoi(commandLine.content);

        if (messageNumber <= 0 || messageNumber > currentEmailClientUser.getUserEmails().size()) 
        {
            response = "-ERR mail doesn't exist\n";
            if (!sendResponse(clientSocketDescriptor, response)) 
            {
                cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru STAT cu argument invalid" << endl;
                return false;
            }
            return true;
        }

        Email &email = currentEmailClientUser.getUserEmails()[messageNumber - 1];

        response = "+OK " + to_string(messageNumber) + " " + to_string(email.getSize()) + "\n";
        if (!sendResponse(clientSocketDescriptor, response)) 
        {
            cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru STAT cu argument valid" << endl;
            return false;
        }
        return true;
    }

    // prima data trebuie sa apelez in terminal comanda getmail ca sa imi dea retrieve in Maildir la toate mailurile care nu au fost fetched pana acum
    CommandLine terminalCommandLine;
    terminalCommandLine.command = "getmail --rcfile ~/.getmail/getmailrc"; 

    int terminalStatus = system(terminalCommandLine.command.c_str());
    if (terminalStatus == -1) 
    {
        response = "-ERR Failed to retrieve emails\n";
        sendResponse(clientSocketDescriptor, response);
        perror("[POP3 Thread] says: Eroare la executia comenzii getmail in terminal");
        return false;
    }
    cout << "[POP3 Thread] says: Comanda getmail a fost executata cu succes in terminal" << endl;

    // acum localizez mailurile in Maildir folosind filesystem 
    ssize_t totalSize = 0;
    vector<string> emailsFromMaildir = getEmailsFromMaildir(MaildirPath);
    for (auto& mail : emailsFromMaildir)
    {
        fs::path mailPath = mail;
        ssize_t mailSize = fs::file_size(mailPath);
        string filename = mailPath.filename().string();
        cout << "Procesez fisierul: " << mailPath.string() << ", Size: " << mailSize << " bytes" << endl;
        totalSize += mailSize;

        cout << "[POP3 server] says: deschid mailul " << mailPath.string() << endl;

        // deschid fisierul si citesc continutul
        ifstream file(mailPath);
        if(!file.is_open())
        {
            perror("[POP3 Thread] says: Eroare la deschiderea mailului!");
            return false;
        }

        // citesc intreg fisierul si il salvez in fileContent (rdbuf returneaza bufferul asociat unui flux)
        stringstream fileContent;
        fileContent << file.rdbuf();
        file.close();

        // ma ocup de MIME content
        string emailBody, sender, recipient, subject, date;
        vector<pair<string, string>> attachments;
        if (!parseAttachments(fileContent.str(), emailBody, attachments, sender, recipient, subject, date)) 
        {
            cout << "[POP3 Thread] says: Eroare la parsarea MIME pentru emailul " << mailPath.filename().string() << endl;
            return false;
        }
        
        if(!checkEmailExistence(sender, recipient, subject, date))
        {
            if (!saveEmailToDatabase(sender, recipient, subject, date, emailBody, attachments, currentEmailClientUser, filename))
            {
                cout << "[POP3 Thread] says: Eroare la salvarea emailului in baza de date" << endl;
                return false;
            }
            cout << "[POP3 Thread] says: Emailul si atasamentele au fost salvate in baza de date: " << mailPath.filename().string() << endl;
        }
        else
        {
            cout << "[POP3 Thread] says: Emailul si atasamentele existatu deja baza de date: " << mailPath.filename().string() << endl;
            continue;
        }
        
    }

    response = "+OK " + to_string(emailsFromMaildir.size()) + " "  + to_string(totalSize) + '\n';
    if(!sendResponse(clientSocketDescriptor, response))
        return false;
    return true;
}

vector<string> getEmailsFromMaildir (string path)
{
    // directory_iterator itereaza prin continutul directorului, fisier cu fisier, pana si cele invisibile x 
    vector<string> emails;
    for(auto &entry : fs::directory_iterator(path))
    {
        // MacOS face .DS_Store file pe care il ignor si de asemenea ignor orice hidden file (care incepe cu .)
        string emailName = entry.path().filename().string();
        if (emailName == ".DS_Store" || emailName[0] == '.') 
            continue;
        

        // adaug in vectorul emails fiecare fisier valid 
        if(entry.is_regular_file())
        {
            emails.push_back(entry.path().string());
        }
    }
    return emails;
}


bool saveEmailToDatabase(const string &sender, const string &recipient, const string &subject, const string &date, const string &body, const vector<pair<string, string>> &attachments, User &currentEmailClientUser, const string &filename)
{

    sqlite3_stmt *statement;
    string query = "INSERT INTO emails (sender, recipient, subject, date, body, filename) VALUES (?, ?, ?, ?, ?, ?)";

    ssize_t totalSize = body.size();
    for (const auto &attachment : attachments)
    {
        totalSize += attachment.second.size();
    }

    int status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK) 
    {
        cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru email: " << sqlite3_errmsg(dataBase) << endl;
        return false;
    }

    sqlite3_bind_text(statement, 1, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, filename.c_str(), -1, SQLITE_TRANSIENT);    

 
    status = sqlite3_step(statement);
    if (status != SQLITE_DONE) 
    {
        cout << "[POP3 Thread] says: Eroare la executia query-ului SQL pentru email: " << sqlite3_errmsg(dataBase) << endl;
        sqlite3_finalize(statement);
        return false;
    }

    sqlite3_int64 databaseId = sqlite3_last_insert_rowid(dataBase);
    sqlite3_finalize(statement);

    cout << "[POP3 Thread] says: Am salvat mailul cu databaseID" <<  databaseId << " si cu filename " << filename  << " in baza de date " << endl;
     // salvez noul mail in obiectul meu ca sa il pot folosi usor dupa pentru RETR, printez direct dimensiunea si caut in baza de date dupa databaseID
    Email newEmail(totalSize, databaseId, false, filename);
    currentEmailClientUser.getUserEmails().push_back(newEmail);


    // inserez atasamentele 
    for (const auto &attachment : attachments)
    {
        query = "INSERT INTO attachments (email_id, attachment_name, attachment_content) VALUES (?, ?, ?)";

        status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
        if (status != SQLITE_OK)
        {
            cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru atasamente: " << sqlite3_errmsg(dataBase) << endl;
            return false;
        }

        sqlite3_bind_int64(statement, 1, databaseId);                                
        sqlite3_bind_text(statement, 2, attachment.first.c_str(), -1, SQLITE_TRANSIENT);  
        sqlite3_bind_text(statement, 3, attachment.second.c_str(), -1, SQLITE_TRANSIENT); 

        status = sqlite3_step(statement);
        if (status != SQLITE_DONE)
        {
            cout << "[POP3 Thread] says: Eroare la executia query-ului SQL pentru atasamente: " << sqlite3_errmsg(dataBase) << endl;
            sqlite3_finalize(statement);
            return false;
        }

        sqlite3_finalize(statement);
    }

    // salvez noul mail in obiectul meu ca sa il pot folosi usor dupa pentru RETR, printez direct dimensiunea si caut in baza de date dupa databaseID
    // Email newEmail(totalSize, databaseId, false);
    // currentEmailClientUser.getUserEmails().push_back(newEmail);

    return true;
}


bool parseAttachments(const string &emailContent, string &emailBody, vector<pair<string, string>> &attachments, string &sender, string&recipient, string &subject, string &date)
{
    //bool parsed = false;
    istringstream emailStream(emailContent);
    mim::MimeEntity mime(emailStream);                   // parsez continutul emailului

    sender = mime.header().from().str();            // extrag sender
    recipient = mime.header().to().str();           // extrag recipient
    subject = mime.header().subject();              // extrag subject
    date = mime.header().field("Date").value();     // extrag data

    // mailul are uneori si html si plain text asa ca incerc sa salvez doar una
    string plainTextBody, htmlBody;

    cout << " -------------------------------------------------------------------------------------------------- " << endl;
    cout << "Sender: " << sender << endl;
    cout << "Recipient: " << recipient << endl;
    cout << "Subject: " << subject << endl;
    cout << "Date: " << date << endl;

    // Helper function for recursive traversal of MIME parts
    function<void(const mim::MimeEntity &)> processPart = [&](const mim::MimeEntity &mime)
    {
        string disposition;
        if (mime.header().hasField("Content-Disposition"))
        {
            disposition = mime.header().contentDisposition().type();
        }

        // verfic mai intai tipul de content pe care il am in mail 
        if (mime.header().contentType().type() == "text")
        {
            //parsed = true;
            if(mime.header().contentType().subtype() == "plain")
            {
                plainTextBody = mime.body(); 
            }
            else if(mime.header().contentType().subtype() == "html")
            {
                htmlBody = mime.body();
            }
        }
        // daca am gasit un atasament
        else if (disposition == "attachment")
        {
            string filename = mime.header().contentDisposition().param("filename");
            if (filename.empty() == false)
            {
                string attachmentContent = mime.body();
                attachments.emplace_back(filename, attachmentContent);
            }
        }
        // aici e problema, cand e multipart, adica are si atasamente email body
        else if (mime.header().contentType().isMultipart())
        {
            for (const auto &part : mime.body().parts())
            {
                processPart(*part);
            }
        }
    };

    processPart(mime);

    emailBody = !htmlBody.empty() ? plainTextBody : htmlBody;
    cout << "EmailBody: " << emailBody << endl;
    cout << "Attachments: " << attachments.size() << endl;
    for (const auto &attachment : attachments)
    {
        cout << "Attachment Filename: " << attachment.first << ", Size: " << attachment.second.size() << " bytes" << endl;
    }
    return true;
}


// verifica daca am deja mailul in baza mea de date 
bool checkEmailExistence(const string &sender, const string &recipient, const string &subject, const string &date)
{
    sqlite3_stmt *statement;
    
    string checkQuery = "SELECT COUNT(*) FROM emails WHERE sender = ? AND recipient = ? AND subject = ? AND date = ?";

    int status = sqlite3_prepare_v2(dataBase, checkQuery.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK)
    {
        cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru verificare email: " << sqlite3_errmsg(dataBase) << endl;
        return false;
    }

    sqlite3_bind_text(statement, 1, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, subject.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, date.c_str(), -1, SQLITE_TRANSIENT);

    status = sqlite3_step(statement);
    if (status != SQLITE_ROW)
    {
        cout << "[POP3 Thread] says: Eroare la verificarea existentei emailului: " << sqlite3_errmsg(dataBase) << endl;
        sqlite3_finalize(statement);
        return false;
    }

    int count = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);

    if (count > 0)
    {
        cout << "[POP3 Thread] says: Emailul exista deja in baza de date." << endl;
        return true;
    }
    else
        return false;
}


bool RETRCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{
    // comanda nu da handle la date introduse gresit, adica sa introduca alte caractere in afara de un numar 
    // daca nu a introdus un numar ii spun clientului ca a gresit comandac
    if(commandLine.content.empty() || commandLine.content.find_first_not_of(' ') == string::npos)
    {
        response = "--ERR Unknown command\n";
        if (!sendResponse(clientSocketDescriptor, response)) 
        {
            cout << "[POP3 server] says: Eroare la trimiterea răspunsului atunci cand greseste comanda RETR" << endl;
            return false;
        }
        return true;
    }
    // am luat argumentul numeric ca sa il caut printre mailuri
    int messageNumber = stoi(commandLine.content);
    
    // pot parcurge iar directorul MailDir deoarece ele sunt in aceeasi ordine practic si doar sa il afisez pe ala
    vector<string> emailsFromMaildir = getEmailsFromMaildir(MaildirPath);
    int count = 1;
    for (auto& mail : emailsFromMaildir)
    {
        if(count == messageNumber)
        {
            fs::path mailPath = mail;
            ssize_t mailSize = fs::file_size(mailPath);
            response = "+OK " + to_string(mailSize) + (string)"\n";
            // if(!sendResponse(clientSocketDescriptor, response))
            // {
            //     cout << "[POP3 server] says: Eroare la trimiterea raspunsului in RETR!" << endl;
            //     return false;
            // }

            sqlite3_stmt * statement;
            string query = "SELECT e.body FROM emails e LEFT JOIN attachments a ON e.id = a.email_id WHERE e.id = ?";

            int status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
            if (status != SQLITE_OK)
            {
                cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru atasamente: " << sqlite3_errmsg(dataBase) << endl;
                return false;
            }

            //trebuie sa iterez prin vector ca sa pot ajunge la emailul cu numarul messageNumber si sa pot folosi functia getEmailDatbaseID
            sqlite3_bind_int64(statement, 1, currentEmailClientUser.getEmailDatabaseID(messageNumber));  
            cout << "Acesta este databaseID: " << currentEmailClientUser.getEmailDatabaseID(messageNumber) << endl;          

            status = sqlite3_step(statement);
            if (status == SQLITE_ROW)
            {
                const char* body = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
                if (body)
                {    
                    response = response + "+OK " + string(body) + "\r\n";
                    if (!sendResponse(clientSocketDescriptor, response)) 
                    {
                        cout << "[POP3 server] says: Eroare la trimiterea răspunsului în RETR!" << endl;
                        sqlite3_finalize(statement);
                        return false;
                    }
                }
                else
                {
                    response = "-ERR Email body not found\r\n";
                    if (!sendResponse(clientSocketDescriptor, response)) 
                    {
                        cout << "[POP3 server] says: Eroare la trimiterea răspunsului în RETR!" << endl;
                        sqlite3_finalize(statement);
                        return false;
                    }
                }
            }
            else
            {
                response = "-ERR Email body not found\r\n";
                if (!sendResponse(clientSocketDescriptor, response)) 
                {
                    cout << "[POP3 server] says: Eroare la trimiterea răspunsului în RETR!" << endl;
                    sqlite3_finalize(statement);
                    return false;
                }

            }
            sqlite3_finalize(statement);
            return true;
        }
        count++;
    }
    cout << "[POP3 server] says: NU EXISTA MAILUL" << endl;
    return false;
}


// functia asta o apelez dupa logarea folosind parola deoarece am neovie sa imi incarc structura de date USER
void loadEmailsFromDatabase(User& currentEmailClientUser) 
{
    sqlite3_stmt* statement;
    string query = "SELECT e.id, LENGTH(e.body) AS bodySize, "
                   "(SELECT SUM(LENGTH(a.attachment_content)) FROM attachments a WHERE a.email_id = e.id) AS attachmentsSize, "
                   "e.filename "
                   "FROM emails e";

    int status = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &statement, nullptr);
    if (status != SQLITE_OK) 
    {
        cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru încărcarea emailurilor: " << sqlite3_errmsg(dataBase) << endl;
        return;
    }

    while ((status = sqlite3_step(statement)) == SQLITE_ROW) 
    {
        int databaseID = sqlite3_column_int(statement, 0);
        ssize_t bodySize = sqlite3_column_int(statement, 1);
        ssize_t attachmentsSize = sqlite3_column_int(statement, 2);
        ssize_t mailSize = bodySize + (attachmentsSize > 0 ? attachmentsSize : 0);
        const char* filenameTest = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
        string filename = (filenameTest !=nullptr) ? string(filenameTest) : "";
        cout << "AICI AFISEZ FILENAME: " << filename << endl;

        
        Email newEmail(mailSize, databaseID, false, filename);
        currentEmailClientUser.getUserEmails().push_back(newEmail);
    }

    sqlite3_finalize(statement);
}



bool DELECommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{
    // comanda nu da handle la date introduse gresit, adica sa introduca alte caractere in afara de un numar 
    // daca nu a introdus un numar ii spun clientului ca a gresit comandac
    if(commandLine.content.empty() || commandLine.content.find_first_not_of(' ') == string::npos)
    {
        response = "--ERR Unknown command\n";
        if (!sendResponse(clientSocketDescriptor, response)) 
        {
            cout << "[POP3 server] says: Eroare la trimiterea răspunsului atunci cand greseste comanda RETR" << endl;
            return false;
        }
        return true;
    }

    // am luat argumentul numeric ca sa il caut printre mailuri
    int messageNumber = stoi(commandLine.content);
    
    // pot parcurge iar directorul MailDir deoarece ele sunt in aceeasi ordine practic si doar sa il afisez pe ala
    vector<string> emailsFromMaildir = getEmailsFromMaildir(MaildirPath);
    if (messageNumber > currentEmailClientUser.getUserEmails().size())
    {
        response = "-ERR the requested mail doesn't exist\n";
        if (!sendResponse(clientSocketDescriptor, response)) 
        {
            cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru DELE" << endl;
            return false;
        }
        return true;
    }


    Email &emailToDelete = currentEmailClientUser.getUserEmails()[messageNumber - 1];
    if(emailToDelete.getMarkedForDelete() == true)
    {
        response = "-ERR the requested mail has already been marked for deletion\n";
        if (!sendResponse(clientSocketDescriptor, response)) 
        {
            cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru DELE" << endl;
            return false;
        }
        return true;
    }


    emailToDelete.setMarkedForDelete(true);
    response = "+OK Message " + to_string(messageNumber) + " marked for deletion\n";
    if (!sendResponse(clientSocketDescriptor, response)) 
    {
        cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru DELE" << endl;
        return false;
    }
    cout << "[POP3 server] says: Mesajul " << messageNumber << " a fost marcat pentru ștergere." << endl;
    return true;
    
}


bool NOOPCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{
    response = "+OK\n";
    if (!sendResponse(clientSocketDescriptor, response)) 
    {
        cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru NOOP" << endl;
        return false;
    }
    cout << "[POP3 server] says: comanda NOOP a fost executata de clientul " << clientSocketDescriptor << endl;
    return true;
}


bool RSETCommandHandler(int clientSocketDescriptor, CommandLine &commandLine, string &response, User &currentEmailClientUser)
{
    for(int i=0; i<currentEmailClientUser.getUserEmails().size(); i++)
    {
        Email &email = currentEmailClientUser.getUserEmails()[i];
        email.setMarkedForDelete(false);
    }
    response = "+OK maildrop has been reset\n";
    if (!sendResponse(clientSocketDescriptor, response)) 
    {
        cout << "[POP3 server] says: Eroare la trimiterea răspunsului pentru RSET" << endl;
        return false;
    }
    return true;
}

bool UPDATEhandler(int clientSocketDescriptor, User &currentEmailClientUser)
{
    sqlite3_stmt *statement;
    string deleteEmailQuery = "DELETE FROM emails WHERE id = ?";
    string deleteAttachmentQuery = "DELETE from attachments WHERE email_id = ?";
    string retrieveFileName = "SELECT filename FROM emails WHERE id = ?";

    vector<Email> &userEmails = currentEmailClientUser.getUserEmails();

    for (auto it = userEmails.begin(); it != userEmails.end(); )
    {
        if (it->getMarkedForDelete())
        {
            int emailID = it->getDatabaseID();
            string filename = "";
            int status;

            // aflu care este numele fisierului in Maildir
            status = sqlite3_prepare_v2(dataBase, retrieveFileName.c_str(), -1, &statement, nullptr);
            if(status == SQLITE_OK)
            {
                sqlite3_bind_int(statement, 1, emailID);
                if (sqlite3_step(statement) == SQLITE_ROW)
                {
                    // nu imi dau seama de ce e segmentation fault, poate de aici 
                    const char *copieFilename = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
                    if(copieFilename)
                        filename = copieFilename;
                }
                sqlite3_finalize(statement);
            }
            // Verificare suplimentară pentru numele fișierului
            // daca e gol, trec mai departe fara sa ster mailul
            if (filename.empty())  
            {
                cout << "[POP3 Thread] says: Eroare - numele fișierului e gol" << emailID << endl;
                ++it; 
                continue;
            }

            // sterg atasamentele mailului
            status = sqlite3_prepare_v2(dataBase, deleteAttachmentQuery.c_str(), -1, &statement, nullptr);
            if(status == SQLITE_OK)
            {
                sqlite3_bind_int(statement, 1, emailID);
                if (sqlite3_step(statement) != SQLITE_DONE)
                {
                    cerr << "[POP3 Thread] says: Eroare la stergerea atasamentelor din baza de date!" << endl;
                    sqlite3_finalize(statement);
                    return false;
                }
                sqlite3_finalize(statement);
            }
            else
            {
                cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru stergerea atasamentelor!" << endl;
                return false;
            }      

            // sterg emailul din baza de date
            status = sqlite3_prepare_v2(dataBase, deleteEmailQuery.c_str(), -1, &statement, nullptr);
            if (status == SQLITE_OK)
            {
                sqlite3_bind_int(statement, 1, emailID);
                if (sqlite3_step(statement) != SQLITE_DONE)
                {
                    cerr << "[POP3 Thread] says: Eroare la stergerea emailului din baza de date!" << endl;
                    sqlite3_finalize(statement);
                    return false;
                }
                sqlite3_finalize(statement);
            }
            else
            {
                cout << "[POP3 Thread] says: Eroare la prepararea query-ului SQL pentru stergerea emailului!" << endl;
                return false;
            }            

            // sterg mailul din Maildir
            fs::path emailPath = MaildirPath + filename;
            cout << "Sterg din maildir emailPath: " << emailPath << endl;
            if (fs::exists(emailPath))
            {
                fs::remove(emailPath);
                cout << "[POP3 Thread] says: am sters emailul din Maildir: " << emailPath.string() << endl;
            }
            else
            {
                cout << "[POP3 Thread] says: Fisierul emailului " << emailPath.string() << " nu a fost gasit in Maildir." << endl;
            }

            // sterg mailul din obiectul meu
            it = userEmails.erase(it);
            cout << "[POP3 Thread] says: Emailul ID " << emailID << " a fost sters cu succes." << endl;
        }
        else
        {
            ++it;
        }
    }

    cout << "[POP3 Thread] says: toate emailurile marcate pentru stergere au fost eliminate. "  << endl;
    return true;
}