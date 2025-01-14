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
#include <string>
#include <cstring>
#include <sstream>
#include <sqlite3.h>
#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/dataview.h>
#include <wx/thread.h>
#include <wx/display.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/app.h>
#include <mimetic/mimetic.h>



using namespace std;
namespace mim = mimetic;

#define AuthorizationStage 0
#define TransactionStage 1
#define UpdateStage 2

extern int errno;
int port;
char POP3message[1024];          // aici o sa fie primit orice mesaj de la server
char CLIENTmessage[1024];        // aici o sa creez mesajele primite din linia de comanda de la client
char SMTPmessage[1024];
sqlite3 *dataBase;


class Stage {
private:
    int numberOfStage;
public:
    Stage(){
        numberOfStage = 0;
    }
    void SetStage(int number){
        numberOfStage = number;
    }
    int GetStage(){
        return numberOfStage;
    }
};


class Email {
private:
    ssize_t size;
    int databaseID;
    bool markedForDelete;
    string filename; // numele fisierului din Maildir
public:
    Email(){
        size = 0;
        databaseID = -1;
        markedForDelete = false;
        filename = "";
    }
    Email(ssize_t size, int databaseID, bool markedForDelete, string filename){
        this->size = size;
        this->databaseID = databaseID;
        this->markedForDelete = markedForDelete;
        this->filename = filename;
    }
    void setSize(ssize_t size){
        this->size = size;
    }
    void setDatabaseID(int databaseID){
        this->databaseID = databaseID;
    }
    void setMarkedForDelete(bool markedForDelete){
        this->markedForDelete = markedForDelete;
    }
    void setFilename(string filename){
        this->filename = filename;
    }
    ssize_t getSize(){
        return this->size;
    }
    int getDatabaseID(){
        return this->databaseID;
    }
    bool getMarkedForDelete(){
        return this->markedForDelete;
    }
    string getFilename(){
        return this->filename;
    }
};


class User : public Email{
private:
    int uniqueID;
    string userName;
    string passWord;
    bool statusUserName;
    bool statusPassWord;
    int stage;
    vector<Email> userEmails;

public:
    User(){
        uniqueID = -1;
        userName = "";
        passWord = "";
        statusUserName = false;
        statusPassWord = false;
        stage = AuthorizationStage;
    }
    User(int uniqueID, string userName, string passWord, bool statusUserName, bool statusPassWord, int stage, vector<Email> userEmails)
    {
        this->uniqueID = uniqueID;
        this->userName = userName;
        this->passWord = passWord;
        this->statusUserName = statusUserName;
        this->statusPassWord = statusPassWord;
        this->stage = stage;
        this->userEmails = userEmails;
    }
    void setUniqueID(int uniqueID){
        this->uniqueID = uniqueID;
    }
    void setUserName(string userName){
        this->userName = userName;
    }
    void setPassWord(string passWord){
        this->passWord = passWord;
    }
    void setStatusUserName(bool statusUserName){
        this->statusUserName = statusUserName;
    }
    void setStatusPassWord(int statusPassWord){
        this->statusPassWord = statusPassWord;
    }
    void setStage(int stage){
        this->stage = stage;
    }
    void setUserEmails(vector<Email> userEmails){
        this->userEmails = userEmails;
    }

    int getUniqueID(){
        return this->uniqueID;
    }
    string getUserName(){
        return this->userName;
    }
    string getPassWord(){
        return this->passWord;
    }
    int getEmailClientNumber(int uniqueID){
        return uniqueID-100;
    }
    bool getStatusUserName(){
        return statusUserName;
    }
    vector<Email>& getUserEmails(){
        return userEmails;
    }
    int getEmailDatabaseID(int messageNumber){
        if(messageNumber <= userEmails.size())
            return userEmails[messageNumber-1].getDatabaseID();
        return -100;
    }
    bool getMarkedForDelete(int messageNumber){
        if(messageNumber <= userEmails.size())
            return userEmails[messageNumber-1].getMarkedForDelete();
        return true;
    }
    int getStage(){
        return this->stage;
    }

};


void handleDataCommand(int socketDescriptor);
bool loginHandler(int sockedDescriptor, User &currentEmailClientUser);

// vreau un handler care sa imi seteze userul curent intr-o variabila care sa retina informatiile legate de el
// vreau ca acest handler sa ii spuna serverului sa verifice daca aceste informatii sunt stocate in baza de date 
// vreau ca odata ce a verificat acest lucru sa ii spuna serveruluiPOP3 sa apeleze practic "system("USER username")" etc 


void handler(int sockedDescriptor);

class MainWindow : public wxFrame
{
private:
    wxTextCtrl* searchBox;
    wxListCtrl* emailTable;

    struct Email{
        int id;
        wxString subject;
        wxString sender;
        wxString date;
    };
    vector<Email> emails;

public:
    MainWindow();
    void PopulateEmailTable();
    void PopulateSentEmailsTable();
    void OnEmailDoubleClick(wxListEvent& event);
    void OnSearch(wxCommandEvent& event);
    void OnResize(wxSizeEvent& event);
    void OnNewMail(wxCommandEvent& event);
    void OnRefresh(wxCommandEvent& event);
};


class LoginFrame : public wxFrame
{
private:
    wxTextCtrl* usernameInput;
    wxTextCtrl* passwordInput;

    void userLogin(wxCommandEvent& event);
public:
    LoginFrame();
};


class NetworkThread : public wxThread
{
private:
    string serverIP;
    int port;
protected:
    virtual ExitCode Entry() override;
public:
    User currentEmailClientUser;
    LoginFrame* loginFrame;
    MainWindow* mainWindow;

    NetworkThread(const string& serverIP, int port);

    string getServerIP(){
        return this->serverIP;
    }
};


class ComposeMailFrame : public wxFrame
{
private:
    wxTextCtrl* recipientInput;
    wxTextCtrl* ccInput;
    wxTextCtrl* subjectInput;
    wxTextCtrl* emailContent;
    wxButton* attachButton;
    wxListBox* attachmentList;      // lista ca sa pot afisa fisierele atasate 

    // Vector in care retin caile fisierelor 
    vector<string> attachedFiles; 

public:
    ComposeMailFrame(wxFrame* parent);
    void OnSend(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnAttach(wxCommandEvent& event); 
};



class ForwardMailFrame : public wxFrame
{
private:
    wxTextCtrl* recipientInput;
public:
    ForwardMailFrame(wxFrame* parent);
    void OnSend(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
};



class ReplyMailFrame : public wxFrame
{
private:
    wxTextCtrl* recipientInput;
    wxTextCtrl* emailContent;
    wxString originalContent;
    wxString subject;
    
public:
    ReplyMailFrame(wxFrame* parent, const wxString& sender, const wxString& subject, const wxString& originalContent);
    void OnSend(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
};



class EmailViewFrame : public wxFrame
{
private:
    wxString sender;  
    wxString subject;
    wxTextCtrl* emailContent;
    vector<string> attachments;
public:
    EmailViewFrame(wxFrame* parent, const wxString& subject, const wxString& sender, const wxString& date, const wxString& content, const vector<string>& attachments, int emailId);
    void OnForward(wxCommandEvent& event);
    void OnReply(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);

    wxString GetEmailContent() const { return emailContent->GetValue(); }
    wxString GetSubject() const { return subject; }
    const vector<string>& GetAttachments() const { return attachments; } 

    int emailId;
};


class emailClient : public wxApp
{
public:
    virtual bool OnInit();
};



class EmailSenderThread : public wxThread
{
public:
    EmailSenderThread(const string& recipient, const string& subject, const string& content, const string& cc, const vector<string>& attachments, wxFrame* parentFrame);

protected:
    ExitCode Entry() override;

private:
    string recipient;
    string subject;
    string content;
    string cc;
    vector<string> attachments; 
    wxFrame* parentFrame;
};
