# Poshet

<br> Am folosit getmail6 pentru implementarea POP3. </br>
<br> Am folosit mimetic pentru implementarea SMTP deoarece are si suport MIME, adica pentru atasamente. </br>
<br> Pentru interfata grafica am folosit wxwidgets. </br>


# compilare fiecare server si client
<br> g++ -o serverPOP3 serverPOP3.cpp -pthread -lsqlite3 -std=c++17 -lmimetic </br>
<br> g++ -o serverSMTP serverSMTP.cpp -pthread -lsqlite3 -std=c++17 -lmimetic </br>
<br> g++ -std=c++17 -o client client.cpp $(wx-config --cxxflags --libs) -lsqlite3 -pthread </br>


# rulare: 

<br> ./serverPOP3 </br>
<br> ./serverSMTP </br>
<br> ./client </br>
