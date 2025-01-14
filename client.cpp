#include "client.h"

int socketDescriptor; 


vector<pair<string, string>> retrieveAttachments(int emailId) 
{
    vector<pair<string, string>> attachments;
    sqlite3_stmt* stmt;

    string query = "SELECT attachment_name, attachment_content FROM attachments WHERE email_id = ?";

    int rc = sqlite3_prepare_v2(dataBase, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) 
    {
        perror("Error preparing SQL query for attachments! ");
        return attachments;
    }

    sqlite3_bind_int(stmt, 1, emailId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        if (name && content) {
            attachments.emplace_back(name, content);
        }
    }

    sqlite3_finalize(stmt);
    return attachments;
}


string getAbsolutePath(const string& filePath) 
{
    char absolutePath[PATH_MAX];
    if (realpath(filePath.c_str(), absolutePath)) 
    {
        return string(absolutePath);
    } 
    else 
    {
        cerr << "Error: Unable to resolve path for " << filePath << endl;
        return "";
    }
}



// encoding in baza64
string base64Encode(const vector<unsigned char>& data) 
{
    static const char encodingTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    string encoded;
    size_t inputLength = data.size();
    encoded.reserve(((inputLength + 2) / 3) * 4);

    for (size_t i = 0; i < inputLength;) 
    {
        uint32_t octetA = i < inputLength ? data[i++] : 0;
        uint32_t octetB = i < inputLength ? data[i++] : 0;
        uint32_t octetC = i < inputLength ? data[i++] : 0;

        uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        encoded.push_back(encodingTable[(triple >> 18) & 0x3F]);
        encoded.push_back(encodingTable[(triple >> 12) & 0x3F]);
        encoded.push_back(encodingTable[(triple >> 6) & 0x3F]);
        encoded.push_back(encodingTable[triple & 0x3F]);
    }

    for (size_t i = 0; i < (3 - (inputLength % 3)) % 3; ++i) 
    {
        encoded[encoded.size() - 1 - i] = '=';
    }

    return encoded;
}

// encoding in baza 64 pentru fisier
string encodeFileToBase64(const string& filePath) 
{
    ifstream file(filePath, ios::binary);
    if (!file) 
    {
        perror( "Error: Could not open file!" );
        return "";
    }

    vector<unsigned char> fileData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    return base64Encode(fileData);
}


void prepareEmail(const string& subject, const string& content, const vector<string>& attachments, string& emailContent) 
{
    string boundary = "boundary123";

    // Setez MIME headers
    emailContent = "Subject: " + subject + "\r\n";
    emailContent += "MIME-Version: 1.0\r\n";
    emailContent += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n";

    // Email body
    emailContent += "--" + boundary + "\r\n";
    emailContent += "Content-Type: text/plain; charset=\"UTF-8\"\r\n\r\n";
    emailContent += content + "\r\n\r\n";

    // Attachments
    for (const auto& file : attachments) {
        string base64FileContent = encodeFileToBase64(file); // Convert file to Base64
        string fileName = file.substr(file.find_last_of("/\\") + 1); // Extract filename

        emailContent += "--" + boundary + "\r\n";
        emailContent += "Content-Type: application/octet-stream; name=\"" + fileName + "\"\r\n";
        emailContent += "Content-Transfer-Encoding: base64\r\n";
        emailContent += "Content-Disposition: attachment; filename=\"" + fileName + "\"\r\n\r\n";
        emailContent += base64FileContent + "\r\n\r\n";
    }

    // pun sfarsitul la mail
    emailContent += "--" + boundary + "--\r\n";
    emailContent += ".\r\n"; 
}




// NetworkThread class
NetworkThread::NetworkThread(const string& serverIP, int port) : wxThread(wxTHREAD_DETACHED), serverIP(serverIP), port(port)
{
    loginFrame = new LoginFrame();
    mainWindow = new MainWindow();
}

NetworkThread::ExitCode NetworkThread::Entry()
{
    cout << "[Client Thread] says: Attempting to connect to server at " << getServerIP() << " on port " << port << endl;

    // 1. Create socket
    socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor == -1)
    {
        perror("[Client Thread] says: Error creating socket");
        return (ExitCode)1;
    }

    // 2. Configure server address
    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(serverIP.c_str());
    server.sin_port = htons(port);

    // 3. Connect to server
    if (connect(socketDescriptor, (struct sockaddr*)&server, sizeof(server)) == -1)
    {
        perror("[Client Thread] says: Error connecting to server");
        return (ExitCode)1;
    }
    cout << "[Client Thread] says: Connected to server successfully!" << endl;

    // 4. Read server response
    char SMTPmessage[1024];
    bzero(SMTPmessage, sizeof(SMTPmessage));
    if (read(socketDescriptor, SMTPmessage, sizeof(SMTPmessage)) < 0)
    {
        perror("[Client Thread] says: Error reading server response");
        return (ExitCode)1;
    }
    cout << "[Client Thread] says: Server message: " << SMTPmessage << endl;

    while(true)
    {
        bzero(CLIENTmessage, sizeof(CLIENTmessage));
        //bzero(POP3message, sizeof(POP3message));
        //bzero(SMTPmessage, sizeof(SMTPmessage));

        if(currentEmailClientUser.getStage() == AuthorizationStage && !currentEmailClientUser.getStatusUserName())
        {
            bzero(POP3message, sizeof(POP3message));

            string command = "USER " + currentEmailClientUser.getUserName();
            strcpy(CLIENTmessage, command.c_str());

            if (write(socketDescriptor, &CLIENTmessage, sizeof(CLIENTmessage)) < 0)
            {
                perror("[Client thread] says: Eroare la trimiterea mesajului din linia de comanda catre server");
                break;
            }

            if (read(socketDescriptor, &POP3message, sizeof(POP3message)) < 0)
            {
                perror("[Client thread] says: Eroare la citirea mesajului 2 primit de la server dupa conectare");
                break;
            }
            cout << "MESAJ DE LA POP3" << POP3message << endl;

            if(strcmp(POP3message, "+OK user accepted\n") == 0)
            {
                cout << "USERUL A FOST VERIFICAT!" << endl;
                currentEmailClientUser.setStatusUserName(true);
            }
            else
            {
                cout << "USERUL ESTE INCORECT!" << endl;
                loginFrame->CallAfter([this]() {
                    wxMessageBox("Invalid username!", "Error", wxOK | wxICON_ERROR);
                });
                break;
            }
        }
        // abia dupa ce stiu ca am setat username-ul, pot trimite si parola
        else if (currentEmailClientUser.getStage() == AuthorizationStage && currentEmailClientUser.getStatusUserName())
        {
            bzero(POP3message, sizeof(POP3message));

            string command = "PASS " + currentEmailClientUser.getPassWord();
            strcpy(CLIENTmessage, command.c_str());

            if (write(socketDescriptor, &CLIENTmessage, sizeof(CLIENTmessage)) < 0)
            {
                perror("[Client] says: Eroare la trimiterea mesajului din linia de comanda catre server");
                break;
            }

            if (read(socketDescriptor, &POP3message, sizeof(POP3message)) < 0)
            {
                perror("[Client] says: Eroare la citirea mesajului 2 primit de la server dupa conectare");
                break;
            }
            cout << POP3message;

            if(strcmp(POP3message, "+OK maildrop ready\n") == 0)
            {
                //cout << "PAROLA A FOST VERIFICATA!" << endl;
                currentEmailClientUser.setStage(TransactionStage);
                
                // Update GUI from the main thread
                loginFrame->CallAfter([this]() {
                    loginFrame->Hide();
                    mainWindow->Show();
                });
            }
            else
            {
                //cout << "PAROLA ESTE INCORECTA!" << endl;
                loginFrame->CallAfter([this]() {
                    wxMessageBox("Invalid password!", "Error", wxOK | wxICON_ERROR);
                });
                break;
            }         
        }
        else if(currentEmailClientUser.getStage() == TransactionStage)
        {
            bzero(POP3message, sizeof(POP3message));
            string statCommand = "STAT";
            strcpy(CLIENTmessage, statCommand.c_str());

            // Scriu comanda STAT către server
            if (write(socketDescriptor, CLIENTmessage, sizeof(CLIENTmessage)) < 0) {
                perror("[Client Thread] says: Error sending STAT command to server");
                break;
            }

            // Citesc răspunsul serverului
            if (read(socketDescriptor, POP3message, sizeof(POP3message)) < 0) {
                perror("[Client Thread] says: Error reading STAT response from server");
                break;
            }

            cout << "[Client Thread] says: STAT response: " << POP3message << endl;

            handler(socketDescriptor);
        }

    }
    close(socketDescriptor);

    //close(socketDescriptor); !!!! 
    return (ExitCode)0;
}


// LoginFrame class
void LoginFrame::userLogin(wxCommandEvent& event)
{
    string username = usernameInput->GetValue().ToStdString();
    string password = passwordInput->GetValue().ToStdString();

    // check if the username and password are stored in the database 
    NetworkThread* networkThread = new NetworkThread("127.0.0.1", 8014);
    networkThread->currentEmailClientUser.setStage(AuthorizationStage);
    networkThread->currentEmailClientUser.setUserName(username);
    networkThread->currentEmailClientUser.setPassWord(password);
    networkThread->loginFrame=this;
    
    //networkThread->mainWindow->Show();
    // Pass the current frame and main window
    networkThread->loginFrame = this;
    networkThread->mainWindow = dynamic_cast<MainWindow*>(GetParent());  // Ensure parent is set

    if (networkThread->Run() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Failed to start network thread", "Error", wxOK | wxICON_ERROR);
        return;
    }
}

LoginFrame::LoginFrame() : wxFrame(nullptr, wxID_ANY, "Login", wxDefaultPosition, wxSize(400, 300))
{
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* usernameSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* usernameLabel = new wxStaticText(panel, wxID_ANY, "Username:", wxDefaultPosition, wxSize(70, -1));
    usernameInput = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1));
    usernameSizer->Add(usernameLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    usernameSizer->Add(usernameInput, 1, wxALL | wxEXPAND, 5);

    wxBoxSizer* passwordSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* passwordLabel = new wxStaticText(panel, wxID_ANY, "Password:", wxDefaultPosition, wxSize(70, -1));
    passwordInput = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1), wxTE_PASSWORD);
    passwordSizer->Add(passwordLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    passwordSizer->Add(passwordInput, 1, wxALL | wxEXPAND, 5);

    wxButton* loginButton = new wxButton(panel, wxID_ANY, "Login");
    loginButton->Bind(wxEVT_BUTTON, &LoginFrame::userLogin, this);

    mainSizer->Add(usernameSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(passwordSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(loginButton, 0, wxALIGN_CENTER | wxALL, 10);

    panel->SetSizer(mainSizer);
}




// ComposeMailFrame class 
ComposeMailFrame::ComposeMailFrame(wxFrame* parent) : wxFrame(parent, wxID_ANY, "Compose New Email", wxDefaultPosition, wxSize(800, 600))
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Recipient 
    wxBoxSizer* recipientSizer = new wxBoxSizer(wxHORIZONTAL);
    recipientSizer->Add(new wxStaticText(this, wxID_ANY, "To:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    recipientInput = new wxTextCtrl(this, wxID_ANY);
    recipientSizer->Add(recipientInput, 1, wxALL | wxEXPAND, 5);
    mainSizer->Add(recipientSizer, 0, wxALL | wxEXPAND, 5);

    // CC 
    wxBoxSizer* ccSizer = new wxBoxSizer(wxHORIZONTAL);
    ccSizer->Add(new wxStaticText(this, wxID_ANY, "CC:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    ccInput = new wxTextCtrl(this, wxID_ANY);
    ccSizer->Add(ccInput, 1, wxALL | wxEXPAND, 5);
    mainSizer->Add(ccSizer, 0, wxALL | wxEXPAND, 5);

    // Subject 
    wxBoxSizer* subjectSizer = new wxBoxSizer(wxHORIZONTAL);
    subjectSizer->Add(new wxStaticText(this, wxID_ANY, "Subject:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    subjectInput = new wxTextCtrl(this, wxID_ANY);
    subjectSizer->Add(subjectInput, 1, wxALL | wxEXPAND, 5);
    mainSizer->Add(subjectSizer, 0, wxALL | wxEXPAND, 5);

    // Email Content 
    emailContent = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    mainSizer->Add(emailContent, 1, wxALL | wxEXPAND, 5);

    // Attachments  
    wxBoxSizer* attachSizer = new wxBoxSizer(wxHORIZONTAL);
    attachButton = new wxButton(this, wxID_ANY, "Attach Files");
    attachmentList = new wxListBox(this, wxID_ANY);
    attachSizer->Add(attachButton, 0, wxALL, 5);
    attachSizer->Add(attachmentList, 1, wxALL | wxEXPAND, 5);
    mainSizer->Add(attachSizer, 0, wxALL | wxEXPAND, 5);

    // Button
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* sendButton = new wxButton(this, wxID_ANY, "Send");
    wxButton* cancelButton = new wxButton(this, wxID_ANY, "Cancel");
    buttonSizer->Add(sendButton, 0, wxALL, 5);
    buttonSizer->Add(cancelButton, 0, wxALL, 5);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT, 5);

    // Bind 
    sendButton->Bind(wxEVT_BUTTON, &ComposeMailFrame::OnSend, this);
    cancelButton->Bind(wxEVT_BUTTON, &ComposeMailFrame::OnCancel, this);
    attachButton->Bind(wxEVT_BUTTON, &ComposeMailFrame::OnAttach, this);

    SetSizer(mainSizer);
}

void ComposeMailFrame::OnSend(wxCommandEvent& event)
{
    wxString recipient = recipientInput->GetValue();
    wxString subject = subjectInput->GetValue();
    wxString content = emailContent->GetValue();
    wxString cc = ccInput->GetValue();

    if (recipient.IsEmpty() || content.IsEmpty())
    {
        wxMessageBox("Recipient and content fields cannot be empty!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // dau disable cat timp se trimite mailul
    this->Disable();

    // fac un nou thread ca sa trimit mailul
    EmailSenderThread* emailThread = new EmailSenderThread(
        recipient.ToStdString(),
        subject.ToStdString(),
        content.ToStdString(),
        cc.ToStdString(),
        attachedFiles,

        this // pasez ca argument=(parinte) fereastra curenta
    );

    cout << "Fisierele atasate sunt: " << endl;
    for (const auto& file : attachedFiles)
    {
        cout << file << endl;
    }

    if (emailThread->Run() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Failed to start email-sending thread.", "Error", wxOK | wxICON_ERROR);
        this->Enable();             // daca thread-ul da eroare, atunci dau enable la fereastra 
        return;
    }

    wxMessageBox("Email is being sent in the background. Please wait...", "Information", wxOK | wxICON_INFORMATION);
}

void ComposeMailFrame::OnCancel(wxCommandEvent& event)
{
    Close();
}

void ComposeMailFrame::OnAttach(wxCommandEvent& event)
{
    wxFileDialog fileDialog(this, "Select Files", "", "", "All Files (*.*)|*.*", wxFD_OPEN | wxFD_MULTIPLE);
    if (fileDialog.ShowModal() == wxID_CANCEL)
        return;

    wxArrayString selectedFiles;
    fileDialog.GetPaths(selectedFiles);

    cout << "ATTACHMENTS: " << endl;
    for (const auto& file : selectedFiles)
    {
        string absolutePath = getAbsolutePath(file.ToStdString());
        if (!absolutePath.empty()) 
        {
            cout << absolutePath << endl;
            attachedFiles.push_back(absolutePath); 
            attachmentList->Append(wxString(absolutePath));
        }
    }
}


// ForwardMailFrame class
ForwardMailFrame::ForwardMailFrame(wxFrame* parent) : wxFrame(parent, wxID_ANY, "Forward Email", wxDefaultPosition, wxSize(600, 400))
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Recipient 
    wxBoxSizer* recipientSizer = new wxBoxSizer(wxHORIZONTAL);
    recipientSizer->Add(new wxStaticText(this, wxID_ANY, "To:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    recipientInput = new wxTextCtrl(this, wxID_ANY);
    recipientSizer->Add(recipientInput, 1, wxALL | wxEXPAND, 5);
    mainSizer->Add(recipientSizer, 0, wxALL | wxEXPAND, 5);

    // Buttons 
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* sendButton = new wxButton(this, wxID_ANY, "Send");
    wxButton* cancelButton = new wxButton(this, wxID_ANY, "Cancel");
    buttonSizer->Add(sendButton, 0, wxALL, 5);
    buttonSizer->Add(cancelButton, 0, wxALL, 5);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT, 5);

    // Bind 
    sendButton->Bind(wxEVT_BUTTON, &ForwardMailFrame::OnSend, this);
    cancelButton->Bind(wxEVT_BUTTON, &ForwardMailFrame::OnCancel, this);

    SetSizer(mainSizer);
}

void ForwardMailFrame::OnSend(wxCommandEvent& event)
{
    wxString recipient = recipientInput->GetValue();

    if (recipient.IsEmpty())
    {
        wxMessageBox("Recipient field cannot be empty!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // dau fetch la email content si la atasamente din parintele EmailViewFrame
    EmailViewFrame* parentFrame = dynamic_cast<EmailViewFrame*>(GetParent());
    if (!parentFrame)
    {
        wxMessageBox("Failed to retrieve the email content!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxString content = parentFrame->GetEmailContent(); 
    wxString subject = "Fwd: " + parentFrame->GetSubject(); 
    const vector<string>& attachments = parentFrame->GetAttachments(); 

    // dau disable cat actioneaza thread0ul
    this->Disable();

    // incep un thread nou
    EmailSenderThread* emailThread = new EmailSenderThread(
        recipient.ToStdString(),
        subject.ToStdString(),
        content.ToStdString(),
        "", 
        attachments, 
        this            // dau ca argument fereastra parinte
    );

    if (emailThread->Run() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Failed to start email-sending thread.", "Error", wxOK | wxICON_ERROR);
        this->Enable();
        return;
    }

    wxMessageBox("Email is being forwarded in the background. Please wait...", "Information", wxOK | wxICON_INFORMATION);
}

void ForwardMailFrame::OnCancel(wxCommandEvent& event)
{
    Close();
}




    // // ReplyMailFrame class
    // ReplyMailFrame::ReplyMailFrame(wxFrame* parent, const wxString& sender) : wxFrame(parent, wxID_ANY, "Reply to Email", wxDefaultPosition, wxSize(800, 400))
    // {
    //     wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    //     // Recipient Section
    //     wxBoxSizer* recipientSizer = new wxBoxSizer(wxHORIZONTAL);
    //     recipientSizer->Add(new wxStaticText(this, wxID_ANY, "To:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    //     recipientInput = new wxTextCtrl(this, wxID_ANY, sender);
    //     recipientInput->SetEditable(false);
    //     recipientSizer->Add(recipientInput, 1, wxALL | wxEXPAND, 5);
    //     mainSizer->Add(recipientSizer, 0, wxALL | wxEXPAND, 5);

    //     // Email Content Section
    //     emailContent = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    //     mainSizer->Add(emailContent, 1, wxALL | wxEXPAND, 5);

    //     // Buttons Section
    //     wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    //     wxButton* sendButton = new wxButton(this, wxID_ANY, "Send");
    //     wxButton* cancelButton = new wxButton(this, wxID_ANY, "Cancel");
    //     buttonSizer->Add(sendButton, 0, wxALL, 5);
    //     buttonSizer->Add(cancelButton, 0, wxALL, 5);
    //     mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT, 5);

    //     // Bind events
    //     sendButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnSend, this);
    //     cancelButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnCancel, this);

    //     SetSizer(mainSizer);
    // }

    // void ReplyMailFrame::OnSend(wxCommandEvent& event)
    // {
    //     wxString content = emailContent->GetValue();

    //     if (content.IsEmpty())
    //     {
    //         wxMessageBox("Email content cannot be empty!", "Error", wxOK | wxICON_ERROR);
    //         return;
    //     }

    //     wxMessageBox("Reply sent successfully!", "Success", wxOK | wxICON_INFORMATION);
    //     Close();
    // }

    // void ReplyMailFrame::OnCancel(wxCommandEvent& event)
    // {
    //     Close();
    // }


                            //     ReplyMailFrame::ReplyMailFrame(wxFrame* parent, const wxString& sender)
                            //     : wxFrame(parent, wxID_ANY, "Reply to Email", wxDefaultPosition, wxSize(600, 400))
                            // {
                            //     wxPanel* panel = new wxPanel(this, wxID_ANY);

                            //     // Recipient field
                            //     wxStaticText* recipientLabel = new wxStaticText(panel, wxID_ANY, "To:", wxPoint(10, 10));
                            //     recipientInput = new wxTextCtrl(panel, wxID_ANY, sender, wxPoint(50, 10), wxSize(500, 25)); // Pre-fill with sender's email

                            //     // Email content
                            //     wxStaticText* contentLabel = new wxStaticText(panel, wxID_ANY, "Message:", wxPoint(10, 50));
                            //     emailContent = new wxTextCtrl(panel, wxID_ANY, "", wxPoint(10, 80), wxSize(560, 200), wxTE_MULTILINE);

                            //     // Buttons
                            //     wxButton* sendButton = new wxButton(panel, wxID_OK, "Send", wxPoint(410, 300));
                            //     wxButton* cancelButton = new wxButton(panel, wxID_CANCEL, "Cancel", wxPoint(500, 300));

                            //     // Event bindings
                            //     sendButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnSend, this);
                            //     cancelButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnCancel, this);
                            // }

                            // void ReplyMailFrame::OnSend(wxCommandEvent& event)
                            // {
                            //     wxString recipient = recipientInput->GetValue();
                            //     wxString content = emailContent->GetValue();

                            //     // Implement email sending logic (SMTP)
                            //     cout << "Sending reply to: " << recipient.ToStdString() << endl;
                            //     cout << "Content: " << content.ToStdString() << endl;

                            //     // Close frame after sending
                            //     Close();
                            // }

// ReplyMailFrame class
ReplyMailFrame::ReplyMailFrame(wxFrame* parent, const wxString& sender, const wxString& subject, const wxString& originalContent) : wxFrame(parent, wxID_ANY, "Reply to Email", wxDefaultPosition, wxSize(600, 400)), subject(subject)
{
    wxPanel* panel = new wxPanel(this, wxID_ANY);

    // Recipient 
    wxStaticText* recipientLabel = new wxStaticText(panel, wxID_ANY, "To:", wxPoint(10, 10));
    recipientInput = new wxTextCtrl(panel, wxID_ANY, sender, wxPoint(50, 10), wxSize(500, 25)); 

    // Email 
    wxStaticText* contentLabel = new wxStaticText(panel, wxID_ANY, "Message:", wxPoint(10, 50));
    emailContent = new wxTextCtrl(panel, wxID_ANY, "", wxPoint(10, 80), wxSize(560, 200), wxTE_MULTILINE);

    // formatez mailul ca sa arate de parca ar fi un chain de mailuri 
    wxArrayString lines = wxSplit(originalContent, '\n');
    wxString quotedOriginalContent;
    for (const auto& line : lines) 
    {
        wxString trimmedLine = line;        // Creează o copie a liniei
        trimmedLine.Trim(false);            // ii dau Trim pe copie
        if (!trimmedLine.IsEmpty() && trimmedLine.StartsWith(">")) 
        {
            // Adaugun '>' la liniile deja existente
            quotedOriginalContent += ">" + trimmedLine + "\n";
        } 
        else 
        {
            // pun cate un '>' la liniile noi
            quotedOriginalContent += "> " + trimmedLine + "\n";
        }
    }

    // adaug continutul prelucrat
    emailContent->SetValue("\n\n" + quotedOriginalContent);

    // Buttons
    wxButton* sendButton = new wxButton(panel, wxID_OK, "Send", wxPoint(410, 300));
    wxButton* cancelButton = new wxButton(panel, wxID_CANCEL, "Cancel", wxPoint(500, 300));

    sendButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnSend, this);
    cancelButton->Bind(wxEVT_BUTTON, &ReplyMailFrame::OnCancel, this);
}



void ReplyMailFrame::OnSend(wxCommandEvent& event)
{
    wxString recipient = recipientInput->GetValue();
    wxString content = emailContent->GetValue();

    if (recipient.IsEmpty() || content.IsEmpty())
    {
        wxMessageBox("Recipient and content fields cannot be empty!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // nu pun mai mult de un "re"
    wxString replySubject = subject.StartsWith("Re:") ? subject : "Re: " + subject;

    this->Disable();

    EmailSenderThread* emailThread = new EmailSenderThread(
        recipient.ToStdString(),
        replySubject.ToStdString(),
        content.ToStdString(),
        "", 
        {},
        this
    );

    if (emailThread->Run() != wxTHREAD_NO_ERROR)
    {
        wxMessageBox("Failed to start email-sending thread.", "Error", wxOK | wxICON_ERROR);
        this->Enable();
        return;
    }

    wxMessageBox("Reply is being sent in the background. Please wait...", "Information", wxOK | wxICON_INFORMATION);
}

void ReplyMailFrame::OnCancel(wxCommandEvent& event)
{
    Close();
}


// EmailViewFrame class
EmailViewFrame::EmailViewFrame(wxFrame* parent, const wxString& subject, const wxString& sender, const wxString& date, const wxString& content, const vector<string>& attachments, int emailId) : wxFrame(parent, wxID_ANY, "View Email", wxDefaultPosition, wxSize(600, 400)), subject(subject), sender(sender), attachments(attachments), emailId(emailId) 
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Email Details 
    wxStaticText* subjectLabel = new wxStaticText(this, wxID_ANY, "Subject: " + subject);
    wxStaticText* senderLabel = new wxStaticText(this, wxID_ANY, "Sender: " + sender);
    wxStaticText* dateLabel = new wxStaticText(this, wxID_ANY, "Date: " + date);
    emailContent = new wxTextCtrl(this, wxID_ANY, content, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);

    mainSizer->Add(subjectLabel, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(senderLabel, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(dateLabel, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(emailContent, 1, wxALL | wxEXPAND, 5);

    // Attachments 
    if (!attachments.empty()) 
    {
        wxStaticText* attachmentLabel = new wxStaticText(this, wxID_ANY, "Attachments:");
        mainSizer->Add(attachmentLabel, 0, wxALL | wxEXPAND, 5);

        wxListBox* attachmentList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        for (const auto& attachment : attachments) 
        {
            attachmentList->Append(wxString::FromUTF8(attachment));
        }
        mainSizer->Add(attachmentList, 0, wxALL | wxEXPAND, 5);

        // double click event
        attachmentList->Bind(wxEVT_LISTBOX_DCLICK, [this, attachmentList, attachments](wxCommandEvent& event) 
        {
            int index = attachmentList->GetSelection();
            if (index != wxNOT_FOUND) 
            {
                wxFileDialog saveDialog(this, "Save Attachment", "", attachments[index], "All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
                if (saveDialog.ShowModal() == wxID_OK) 
                {
                    ofstream outFile(saveDialog.GetPath().ToStdString(), ios::binary);
                    outFile << attachments[index]; 
                    wxMessageBox("Attachment saved successfully!", "Info", wxOK | wxICON_INFORMATION);
                }
            }
        });
    }

    // Buttons 
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    wxButton* replyButton = new wxButton(this, wxID_ANY, "Reply");
    wxButton* forwardButton = new wxButton(this, wxID_ANY, "Forward");
    wxButton* deleteButton = new wxButton(this, wxID_ANY, "Delete");


    buttonSizer->Add(replyButton, 0, wxALL, 5);
    buttonSizer->Add(forwardButton, 0, wxALL, 5);
    buttonSizer->Add(deleteButton, 0, wxALL, 5);

    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT, 5);

    replyButton->Bind(wxEVT_BUTTON, &EmailViewFrame::OnReply, this);
    forwardButton->Bind(wxEVT_BUTTON, &EmailViewFrame::OnForward, this);
    deleteButton->Bind(wxEVT_BUTTON, &EmailViewFrame::OnDelete, this);

    SetSizer(mainSizer);
}

void EmailViewFrame::OnForward(wxCommandEvent& event)
{
    ForwardMailFrame* forwardFrame = new ForwardMailFrame(this);
    forwardFrame->Show();
}

void EmailViewFrame::OnReply(wxCommandEvent& event)
{
    ReplyMailFrame* replyFrame = new ReplyMailFrame(
        this,
        sender,            // senderul initial
        subject,           // subiectul initial
        emailContent->GetValue()
    );
    replyFrame->Show();
}

void EmailViewFrame::OnDelete(wxCommandEvent& event) 
{
    
    wxString deleCommand = wxString::Format("DELE %d\r\n", emailId);

    // Trimit comanda către serverPOP3
    if (write(socketDescriptor, deleCommand.c_str(), deleCommand.size()) < 0) {
        wxMessageBox("Failed to send delete command to the server!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // Citeșc răspunsul de la server
    char serverResponse[1024];
    bzero(serverResponse, sizeof(serverResponse));
    if (read(socketDescriptor, serverResponse, sizeof(serverResponse)) < 0) {
        wxMessageBox("Failed to read server response!", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // Verific dacă răspunsul e ok
    if (strncmp(serverResponse, "+OK", 3) == 0) 
    {
        wxMessageBox("Email marked for deletion successfully!", "Success", wxOK | wxICON_INFORMATION);
        this->Close(); 
    }
    else 
    {
        wxMessageBox("Failed to mark email for deletion!", "Error", wxOK | wxICON_ERROR);
    }
}


// MainWindow class 
MainWindow::MainWindow() : wxFrame(nullptr, wxID_ANY, "Poshet Email Client", wxDefaultPosition, wxSize(800, 600))
{
    // Main Sizer
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // bara de sus 
    wxPanel* topPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    searchBox = new wxTextCtrl(topPanel, wxID_ANY, "Search emails...", wxDefaultPosition, wxSize(400, -1));
    topSizer->Add(searchBox, 1, wxALL | wxEXPAND, 5);
    topPanel->SetSizer(topSizer);

    // search event
    searchBox->Bind(wxEVT_TEXT, &MainWindow::OnSearch, this);

    // continut mijloc 
    wxBoxSizer* middleSizer = new wxBoxSizer(wxHORIZONTAL);

    // panou stanga 
    wxPanel* leftPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    wxButton* newMailButton = new wxButton(leftPanel, wxID_ANY, "New Mail");
    wxButton* inboxButton = new wxButton(leftPanel, wxID_ANY, "Inbox");
    wxButton* sentButton = new wxButton(leftPanel, wxID_ANY, "Sent");
    wxButton* binButton = new wxButton(leftPanel, wxID_ANY, "Bin");
    wxButton* refreshButton = new wxButton(topPanel, wxID_ANY, "Refresh");

    // butoane stanga
    leftSizer->Add(newMailButton, 0, wxALL | wxEXPAND, 5);
    leftSizer->Add(inboxButton, 0, wxALL | wxEXPAND, 5);
    leftSizer->Add(sentButton, 0, wxALL | wxEXPAND, 5);
    leftSizer->Add(binButton, 0, wxALL | wxEXPAND, 5);
    topSizer->Add(refreshButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    leftPanel->SetSizer(leftSizer);

    newMailButton->Bind(wxEVT_BUTTON, &MainWindow::OnNewMail, this);
    refreshButton->Bind(wxEVT_BUTTON, &MainWindow::OnRefresh, this);
    sentButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {PopulateSentEmailsTable();});
    inboxButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {PopulateEmailTable();});

    // panou dreapta (tabel mailuri)
    wxPanel* rightPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

    emailTable = new wxListCtrl(rightPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    emailTable->InsertColumn(0, "ID", wxLIST_FORMAT_LEFT, 50);
    emailTable->InsertColumn(1, "Subject", wxLIST_FORMAT_LEFT, 300);
    emailTable->InsertColumn(2, "Sender", wxLIST_FORMAT_LEFT, 200);
    emailTable->InsertColumn(3, "Date", wxLIST_FORMAT_LEFT, 150);

    // pun mailurile in tabel
    PopulateEmailTable();

    rightSizer->Add(emailTable, 1, wxALL | wxEXPAND, 5);
    rightPanel->SetSizer(rightSizer);

    // combin cele 2 panouri
    middleSizer->Add(leftPanel, 1, wxEXPAND);
    middleSizer->Add(rightPanel, 3, wxEXPAND);

    // adaug si bara de sus si continutul din mijloc
    mainSizer->Add(topPanel, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(middleSizer, 1, wxEXPAND);

    SetSizer(mainSizer);

    Bind(wxEVT_SIZE, &MainWindow::OnResize, this);
}

void MainWindow::PopulateEmailTable()
{

    emails.clear();
    emailTable->DeleteAllItems();

    // deschid baza de date
    sqlite3* db;
    if (sqlite3_open("ProiectRetele.db", &db) != SQLITE_OK)
    {
        wxMessageBox("Failed to open the database!", "Database Error", wxOK | wxICON_ERROR);
        return;
    }

    // dau fetch
    const char* query = "SELECT id, subject, sender, date FROM emails";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
    {
        wxMessageBox("Failed to prepare database query!", "Database Error", wxOK | wxICON_ERROR);
        sqlite3_close(db);
        return;
    }


    int rowIndex = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const char* subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

        emails.push_back({id, subject ? wxString(subject) : "No Subject",
                        sender ? wxString(sender) : "Unknown Sender",
                        date ? wxString(date) : "No Date"});

        long index = emailTable->InsertItem(emailTable->GetItemCount(), to_string(id));
        emailTable->SetItem(index, 1, subject ? wxString(subject) : "No Subject");
        emailTable->SetItem(index, 2, sender ? wxString(sender) : "Unknown Sender");
        emailTable->SetItem(index, 3, date ? wxString(date) : "No Date");

        if (rowIndex % 2 == 0)
        {
            emailTable->SetItemBackgroundColour(index, wxColour(30, 30, 30));
        }
        else
        {
            emailTable->SetItemBackgroundColour(index, wxColour(50, 50, 50));
        }
        rowIndex++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    emailTable->Bind(wxEVT_LIST_ITEM_ACTIVATED, &MainWindow::OnEmailDoubleClick, this);
}


void MainWindow::PopulateSentEmailsTable() 
{
    // Curat tabelul si lista de mailuri
    emails.clear();
    emailTable->DeleteAllItems();

    // Deschid baza de date
    if (sqlite3_open("ProiectRetele.db", &dataBase) != SQLITE_OK) {
        wxMessageBox("Failed to open the database!", "Database Error", wxOK | wxICON_ERROR);
        return;
    }

    // Query pentru emailurile trimise de "ciobanu.matei@asii.ro"
    const char* query = "SELECT id, sender, subject, date FROM emails WHERE sender = 'ciobanu.matei@asii.ro'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(dataBase, query, -1, &stmt, nullptr) != SQLITE_OK) {
        wxMessageBox("Failed to prepare query for sent emails!", "Database Error", wxOK | wxICON_ERROR);
        sqlite3_close(dataBase);
        return;
    }

    int rowIndex = 0;
    // Preiau emailurile și populez tabelul
    while (sqlite3_step(stmt) == SQLITE_ROW) 
    {
        int id = sqlite3_column_int(stmt, 0);
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        emails.push_back({
            id,
            sender ? wxString(subject) : "No Subject",
            subject ? wxString(sender) : "Unknown Receiver",
            date ? wxString(date) : "No Date"
        });

        long index = emailTable->InsertItem(emailTable->GetItemCount(), to_string(id));
        emailTable->SetItem(index, 1, subject ? wxString(subject) : "No Subject");
        emailTable->SetItem(index, 2, sender ? wxString(sender) : "Unknown Receiver");
        emailTable->SetItem(index, 3, date ? wxString(date) : "No Date");

        // pun alte culori 
        if (rowIndex % 2 == 0) {
            emailTable->SetItemBackgroundColour(index, wxColour(30, 30, 30));
        } else {
            emailTable->SetItemBackgroundColour(index, wxColour(55, 55, 55));
        }
        rowIndex++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(dataBase);
}



void MainWindow::OnEmailDoubleClick(wxListEvent& event) 
{
    long itemIndex = event.GetIndex();
    if (itemIndex >= 0 && itemIndex < emails.size()) 
    {
        const Email& email = emails[itemIndex];

        // deschis baza de date
        sqlite3* db;
        if (sqlite3_open("ProiectRetele.db", &db) != SQLITE_OK) {
            wxMessageBox("Failed to open the database!", "Database Error", wxOK | wxICON_ERROR);
            return;
        }

        // dau fetch pentru continutul mailului selectat
        const char* query = "SELECT body FROM emails WHERE id = ?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
            wxMessageBox("Failed to prepare the query!", "Database Error", wxOK | wxICON_ERROR);
            sqlite3_close(db);
            return;
        }

        sqlite3_bind_int(stmt, 1, email.id);

        wxString emailBody;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* body = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (body) {
                emailBody = wxString::FromUTF8(body);
            }
        } else {
            wxMessageBox("Failed to fetch the email content!", "Query Error", wxOK | wxICON_ERROR);
        }

        sqlite3_finalize(stmt);

        // dau fetch pt attachments
        vector<pair<string, string>> attachments = retrieveAttachments(email.id);

        // convertesc atasamentele intr-un vector de string
        vector<string> attachmentNames;
        for (const auto& attachment : attachments) 
        {
            attachmentNames.push_back(attachment.first);
        }

        EmailViewFrame* emailViewFrame = new EmailViewFrame(
            this, email.subject, email.sender, email.date, emailBody, attachmentNames, email.id
        );
        emailViewFrame->Show();

        sqlite3_close(db);
    }
}

void MainWindow::OnSearch(wxCommandEvent& event)
{
    wxString searchTerm = searchBox->GetValue().Lower();
    emailTable->DeleteAllItems();

    for (const auto& email : emails)
    {
        if (email.subject.Lower().Find(searchTerm) != wxNOT_FOUND || 
            email.sender.Lower().Find(searchTerm) != wxNOT_FOUND)
        {
            long index = emailTable->InsertItem(emailTable->GetItemCount(), to_string(email.id));
            emailTable->SetItem(index, 1, email.subject);
            emailTable->SetItem(index, 2, email.sender);
            emailTable->SetItem(index, 3, email.date);
        }
    }
}

void MainWindow::OnResize(wxSizeEvent& event)
{
    int totalWidth = GetClientSize().GetWidth();

    if (emailTable)
    {
        int leftPanelWidth = 200;
        int tableWidth = totalWidth - leftPanelWidth - 20;

        emailTable->SetColumnWidth(0, tableWidth * 0.1);
        emailTable->SetColumnWidth(1, tableWidth * 0.4);
        emailTable->SetColumnWidth(2, tableWidth * 0.3);
        emailTable->SetColumnWidth(3, tableWidth * 0.2);
    }

    event.Skip();
}

void MainWindow::OnNewMail(wxCommandEvent& event)
{
    ComposeMailFrame* composeFrame = new ComposeMailFrame(this);
    composeFrame->Show();
}

void MainWindow::OnRefresh(wxCommandEvent& event)
{
    PopulateEmailTable();
}



void handler(int sockedDescriptor)
{
    // MainWindow* mainWindow = new MainWindow();
    // mainWindow->Show();
    
    // creez un thread care se conecteaza la server prin port si dupa apeleaza functia asta care face un chat infinit cu serverul
    // deci in functia asta am comunicarea cu serverului care este apelata de thread-ul care se conecteaza la server 

    cin.getline(CLIENTmessage, sizeof(CLIENTmessage));

    if (write(socketDescriptor, &CLIENTmessage, sizeof(CLIENTmessage)) < 0)
    {
        perror("[Client] says: Eroare la trimiterea mesajului din linia de comanda catre server");
        return;
    }

    if (read(socketDescriptor, &POP3message, sizeof(POP3message)) < 0)
    {
        perror("[Client] says: Eroare la citirea mesajului 2 primit de la server dupa conectare");
        return;
    }
    cout << POP3message;
}



// EmailSenderThread Class
EmailSenderThread::EmailSenderThread(const string& recipient, const string& subject, const string& content, const string& cc, const vector<string>& attachments, wxFrame* parentFrame) : wxThread(wxTHREAD_DETACHED), recipient(recipient), subject(subject), content(content), cc(cc), attachments(attachments), parentFrame(parentFrame) {}

wxThread::ExitCode EmailSenderThread::Entry()
{

    int smtpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (smtpSocket == -1)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("Failed to create socket for SMTP server.", "Error", wxOK | wxICON_ERROR);
        });
        return (ExitCode)1;
    }

    struct sockaddr_in smtpServer;
    bzero(&smtpServer, sizeof(smtpServer));
    smtpServer.sin_family = AF_INET;
    smtpServer.sin_port = htons(8013); 
    smtpServer.sin_addr.s_addr = inet_addr("127.0.0.1"); 

    if (connect(smtpSocket, (struct sockaddr*)&smtpServer, sizeof(smtpServer)) == -1)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("Failed to connect to the SMTP server.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    char serverResponse[1024];
    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "220", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not respond with a 220 message.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    // trimit HELO 
    string heloCommand = "HELO localhost\r\n";
    write(smtpSocket, heloCommand.c_str(), heloCommand.size());
    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "250", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not accept HELO command.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    // trimit MAIL FROM 
    string mailFromCommand = "MAIL FROM:ciobanu.matei@asii.ro"; // Replace with sender email
    write(smtpSocket, mailFromCommand.c_str(), mailFromCommand.size());
    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "250", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not accept MAIL FROM command.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    // trimit RCPT TO 
    string rcptToCommand = "RCPT TO:" + recipient;
    write(smtpSocket, rcptToCommand.c_str(), rcptToCommand.size());
    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "250", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not accept RCPT TO command.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    // trimit DATA 
    string dataCommand = "DATA\r\n";
    write(smtpSocket, dataCommand.c_str(), dataCommand.size());

    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "354", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not accept DATA command.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    string emailContent;
    cout << "Sunt inainte de prepareMail: " << endl;
    cout << "Subject: " << subject << endl;
    cout << "Content: " << content << endl;
    cout << "---------------------------------------------------------------------------------- "<< endl;
    prepareEmail(subject, content, attachments, emailContent);
    emailContent += "\r\n.\r\n";

    cout << "[DEBUG] Generated Email:\n" << emailContent << "\n";


    write(smtpSocket, emailContent.c_str(), emailContent.size());


    bzero(serverResponse, sizeof(serverResponse));
    read(smtpSocket, serverResponse, sizeof(serverResponse));
    if (strncmp(serverResponse, "250", 3) != 0)
    {
        parentFrame->CallAfter([=]() {
            wxMessageBox("SMTP server did not accept the email content.", "Error", wxOK | wxICON_ERROR);
        });
        close(smtpSocket);
        return (ExitCode)1;
    }

    parentFrame->CallAfter([=]() {
        wxMessageBox("Email sent successfully!", "Success", wxOK | wxICON_INFORMATION);
    });

    return (ExitCode)0;
}



// emailClient class 
bool emailClient::OnInit()
{
    LoginFrame* loginFrame = new LoginFrame();
    MainWindow* mainWindow = new MainWindow();

    mainWindow->PopulateEmailTable();

    mainWindow->Hide(); // Initially hide the main window
    loginFrame->SetParent(mainWindow); // Link the frames

    loginFrame->Show();
    return true;
}
wxIMPLEMENT_APP(emailClient);

