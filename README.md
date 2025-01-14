# Poshet

Am folosit getmail6 pentru implementarea POP3.
Am folosit mimetic pentru implementarea SMTP deoarece are si suport MIME, adica pentru atasamente.
Pentru interfata grafica am folosit wxwidgets.


# compilare fiecare server si client
g++ -o serverPOP3 serverPOP3.cpp -pthread -lsqlite3 -std=c++17 -lmimetic
g++ -o serverSMTP serverSMTP.cpp -pthread -lsqlite3 -std=c++17 -lmimetic
g++ -std=c++17 -o client client.cpp $(wx-config --cxxflags --libs) -lsqlite3 -pthread


# rulare: 

./serverPOP3
./serverSMTP
./client
