#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sqlite3.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

int client;
sqlite3 * db;
char  mesajInitial[] = "Conectarea la serverul de control al traficului s-a facut cu succes.\nDaca doriti sa va autentificati utilizati comanda 'login' urmata de numele de utilizator, iar daca doriti sa va inregistrati utilizati comanda 'register' urmata de numele de utilizator\n";
char mesajClient[100];
char mesajGlobal[300];
char mesajAlertaGlobal[300];
char locatieAlertaGlobala[50];

int fisierEvenimenteSportive, fisierEvenimenteSportiveCpy, fisierVreme, userAlert, fisierVremeCpy, fisierPretCarburant, fisierPretCarburantCpy;
char mesajEvenimenteSportive[100], mesajVreme[100], mesajPretCarburant[100];
char locatieEvenimentSportiv[50], locatieVreme[50], locatiePretCarburant[50];

char mesajCarburantAnterior[50];
char mesajSportAnterior[100];
char mesajVremeAnterior[100];

pthread_mutex_t mesajInitialLock;
pthread_mutex_t databaseLock;
pthread_mutex_t mesajGlobalLock;
void * threadRoutine (void * arg);
void * threadEventsRoutine (void * arg);

struct str{
    char numeStrada[50];
    int vitezaMaxima;
    int vecini[10];
    int marimeListaVecini;
}Strazi[10];

static int dbCallback(void *NotUsed, int argc, char **argv, char **azColName) {
   int i;
   for(i = 0; i<argc; i++) {
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}
int callBackRegister(void * passedParam, int argc, char ** argv, char ** columnNames){
    int i = 0;
    for (i=0;i<argc;i++){
        strcat(passedParam, " ");
        strcat(passedParam, argv[i]);
    }
    return 0;
}
void initializareStrazi();
int indexStrada(char * numeStrada);
int suntVecine(char * primaStrada, char * aDouaStrada);

int main(int argc, char * argv[]){
    srand(time(0));
    remove("carburant.txt");
    remove("sport.txt");
    remove("vreme.txt");
    remove("alerta.txt");
    userAlert = open ("alerta.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    fisierEvenimenteSportive = open ("sport.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    fisierVreme = open("vreme.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    fisierPretCarburant = open("carburant.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    initializareStrazi();
    struct sockaddr_in server; // structura folosita de catre server
    struct sockaddr_in incomming; // structura folosita de catre client
    int mySocket;
    int optval = 1;
    //Cream un socket 
    if( (mySocket = socket (AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Eroare la socket().\n");
        return 1;
    }
    // Vom face astfel incat sa nu apara eroare la bind la crearea serverului
    setsockopt(mySocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    // Ne asiguram ca structurile folosite sunt formatate
    bzero(&server, sizeof(server));
    bzero(&incomming, sizeof(incomming));
    // Formatam structura folosita de server
    server.sin_family = AF_INET;	
    server.sin_addr.s_addr = htonl (INADDR_ANY);
    server.sin_port = htons (2024);
    // Incercam sa atasam socketul
    if (bind(mySocket, (struct sockaddr *) &server, sizeof(struct sockaddr)) == -1){
        perror("Eroare la bind().\n");
        return 2;
    }
    // A reusit bindul
    // Acum trebuie sa asteptam conectarea clientilor
    if (listen(mySocket, 1000) == -1){
        perror("Eroare la listen().\n");
        return 3;
    }
    // Vom face queueul de asteptare de maxim 1000 de clienti, presupunand ca 1000 este un nuar suficient de mare
    // In caz contrar acest numar poate fi crescut

    // Acum trebuie serviti clientii
    while(1){
        int length = sizeof(incomming);
        // Urmeaza sa fie acceptata conexiunea cu un client.
        client = accept (mySocket, (struct sockaddr *) &incomming, &length);
        // Acum trebuie ca fiecare client sa fie tratat de cate un thread (DOAMNE AJUTA!)
        pthread_t theThread, events;
        if (pthread_create(&events, NULL, * threadEventsRoutine, &client) != 0){
            perror("Eroare la pthread_create().\n");
            return 5;
        }
        if (pthread_create(&theThread, NULL, *threadRoutine, &client) != 0){
            perror("Eroare la pthread_create().\n");
            return 4;
        }
        //if (pthread_join(theThread, NULL) != 0) {
        //    perror("Eroare la pthread_join().\n");
        // }
    }
    close(fisierVreme);
    close(fisierEvenimenteSportive);
    close(fisierPretCarburant);
    pthread_mutex_destroy(&mesajInitialLock);
    pthread_mutex_destroy(&databaseLock);
    pthread_mutex_destroy(&mesajGlobalLock);
    sqlite3_close(db);
    return 0;
}

void * threadRoutine (void * arg){
    //O variabila care ne ajuta sa determinam scope ul unui mesaj de alerta
    char mesajAlertaLocal[300]; char locatieAlertaLocal[50];
    //O variabila care marcheaza daca utilizatorul este logged in
    int weAreLoggedIn = 0;
    //Acesta este socket descriptorul cu care vom face operatiuni
    int client = *((int *)arg);
    //Aici vom retine parola in cazul autentificarii / parolele in cazul inregistrarii
    char parola[300] = "", parola2[300], * parolaPointer;
    //In raspuns vom citi mesajele primite de la client
    char raspuns[300];
    //copieRaspuns este folosit pentru a ocoli modificarile facute de strtok asupra stringului raspuns :)
    char copieRaspuns [300]; 
    //De acestea vom avea nevoie in eventualitatea in care utilizatorul se inregistreaza, pentru a afla preferintele acestuia legate de...
    char optiuneEvenimenteSportive[300], optiunePretCarburant[300], optiuneVreme[300];
    char evenimenteSportive[5], pretCarburant[5], vreme[5];
    char * sport, * carburant, * meteo;
    //Numele de utilizator 
    char * numeUtilizator;
    //Locatia curent a utilizatorului
    char * locatieActuala = malloc (30 * sizeof(char));
    char locatieActualaPermanenta[30];
    //Viteza actuala a utilizatorului
    char * vitezaActualaString = malloc (10 * sizeof(char));
    int vitezaActuala;
    // Protocolul de comunicare incepe cu un mesaj de la server catre client de bun venit si o invitate de login / register
    pthread_mutex_lock(&mesajInitialLock);
    write (client, mesajInitial, 500);
    pthread_mutex_unlock(&mesajInitialLock);
     // Trebuie sa initializam baza de date pentru clienti
    sqlite3_open("myDB.db", &db);
    char * sqlStatement = "CREATE TABLE IF NOT EXISTS UTILIZATORI( USERNAME TEXT PRIMARY KEY NOT NULL, PASSWORD TEXT, EVENIMENTE_SPORTIVE BOOLEAN, PRET_CARBURANT BOOLEAN, VREME BOOLEAN)";
    int error = sqlite3_exec(db, sqlStatement, dbCallback, 0, NULL);
    if (error != SQLITE_OK){
        printf("Eroare la sqlite_exec(). %d\n", error);
    }
    else{
        printf("Table created succesfully!\n");
    }
    // Citim optiunea aleasa de catre utilizator
    read (client, raspuns, 300);
    //pthread_mutex_lock(&databaseLock);
    strcpy(copieRaspuns, raspuns);
    // Analizam comanda data
    if (strcmp(strtok(raspuns, " "), "login") == 0){
        numeUtilizator = strtok(NULL, "\n");
        //In sql3 vom pune comanda SQL necesara pentru a verifica daca exista vre un utilizator in baza de date cu nume de utilizator ca cel primit
        char sql3[300] = "SELECT COUNT(*) FROM UTILIZATORI GROUP BY USERNAME HAVING USERNAME LIKE '";
        strcat(sql3, numeUtilizator);
        strcat(sql3, "';");
        char * rezultatFunctieCallback = malloc (300 * sizeof(char)); //Aici vom stoca raspunsul primit in urma interogarii bazei de date
        int error = sqlite3_exec(db, sql3, callBackRegister, rezultatFunctieCallback, NULL); 
        printf("%s, %ld\n", rezultatFunctieCallback, strlen(rezultatFunctieCallback));
        if (error != SQLITE_OK){
            printf("Eroare la sqlite_exec(). %d\n", error);
        }
        else{
            printf("Command executed succesfully!\n");
        }
        while (strlen(rezultatFunctieCallback) == 0){ //Daca lungimea este 0 atunci inseamna ca rezultatul interogarii este ca nu exista acel nume de utilizator si trebuie reincercat pana se va trimite un nume de utilizator deja inregistrat
            write(client, "Acest nume de utilizator nu este inregistrat! Incercati din nou.\n", 300);
            bzero(&rezultatFunctieCallback, sizeof(rezultatFunctieCallback));
            read(client, numeUtilizator, 300);
            printf("%s\n", numeUtilizator);
            numeUtilizator = strtok(numeUtilizator, "\n");
            char sql2[300] = "SELECT COUNT(*) FROM UTILIZATORI GROUP BY USERNAME HAVING USERNAME LIKE '";;
            strcat(sql2, numeUtilizator);
            strcat(sql2, "';");
            printf("%s\n", sql2);
            rezultatFunctieCallback = malloc (300 * sizeof(char));
            int error = sqlite3_exec(db, sql2, callBackRegister, rezultatFunctieCallback, NULL);
            printf("%s, %ld\n", rezultatFunctieCallback, strlen(rezultatFunctieCallback));
            if (error != SQLITE_OK){
                printf("Eroare la sqlite_exec(). %d\n", error);
            }
            else{
                printf("Command executed succesfully!\n");
            }
        }
        //Pana aici stim cu siguranta ca numele de utilizator este unul dintre cele inregistrate deja si acum trebuie doar sa asteptam sa primit parola corespunzatoare inregistrarii respective din baza de date
        write (client, "Parola: ", 300);
        read (client, parola, 300);
        parolaPointer = strtok(parola, "\n");
        printf("%s\n", parolaPointer);
        char verificareParola[200] = "SELECT PASSWORD FROM UTILIZATORI WHERE USERNAME LIKE'";
        strcat(verificareParola, numeUtilizator);
        strcat(verificareParola, "';");
        printf("%s\n", verificareParola);
        char * parolaBD = malloc (sizeof(char)*200);
        int error1 = sqlite3_exec(db, verificareParola, callBackRegister, parolaBD, NULL);
        parolaBD = strtok(parolaBD, " ");
        printf("%s, %ld\n", parolaBD, strlen(parolaBD));
        if (error1 != SQLITE_OK){
            printf("Eroare la sqlite_exec(). %d\n", error);
        }
        else{
            printf("Command executed succesfully!\n");
        }
        while(strcmp(parola, parolaBD)!=0){
            write(client, "Parola gresita! Incercati din nou.\n", 300);
            read (client, parola, 300);
            parolaPointer = strtok(parola, "\n");
            printf("%s\n", parolaPointer);
            printf("%s\n", verificareParola);
            printf("%s, %ld\n", parolaBD, strlen(parolaBD));
            if (error1 != SQLITE_OK){
                printf("Eroare la sqlite_exec(). %d\n", error);
            }
            else{
                printf("Command executed succesfully!\n");
            }
        }
        //Aici trebuie sa aflam preferintele utilizatorului
        char getPreferinte[300] = "SELECT EVENIMENTE_SPORTIVE, PRET_CARBURANT, VREME FROM UTILIZATORI WHERE USERNAME LIKE'";
        strcat(getPreferinte, numeUtilizator);
        strcat(getPreferinte, "';");
        char * getPreferences = malloc (sizeof(char)*200);
        int error2 = sqlite3_exec(db, getPreferinte, callBackRegister, getPreferences, NULL);
        sport = malloc (5*sizeof(char));
        carburant = malloc (5*sizeof(char));
        meteo = malloc (5*sizeof(char));
        char * ceva = strtok(getPreferences, " ");
        sport = strtok(NULL, " ");
        carburant = strtok(NULL, " "); 
        meteo = strtok(NULL, " ");
        strcpy(evenimenteSportive, sport);
        strcpy(pretCarburant, carburant);
        strcpy(vreme, meteo);
        printf("Optiuni client %s: %s, %s, %s\n", numeUtilizator, evenimenteSportive, pretCarburant, vreme);
        if (error2 != SQLITE_OK){
            printf("Eroare la sqlite_exec(). %d\n", error);
        }
        else{
            printf("Command executed succesfully!\n");
        }


            char mesajRaspuns[] = "Salut ";
            strcat(mesajRaspuns, numeUtilizator);
            strcat(mesajRaspuns, ", autentificarea a avut loc cu succes! :)\n");
            write (client, mesajRaspuns, 300);
    }
    else if (strcmp(strtok(copieRaspuns, " "), "register") == 0){
        numeUtilizator = strtok(NULL, "\n");
        char sql[200] = "SELECT COUNT(*) FROM UTILIZATORI GROUP BY USERNAME HAVING USERNAME LIKE '";
        strcat(sql, numeUtilizator);
        strcat(sql, "';");
        printf("%s\n", sql);
        char * rezultatFunctieCallback = malloc (300 * sizeof(char));
        error = sqlite3_exec(db, sql, callBackRegister, rezultatFunctieCallback, NULL); // Aici va fi nevoie de o noua functie de callback care sa verifice raspunsul dat de baza de date
        printf("%s, %ld\n", rezultatFunctieCallback, strlen(rezultatFunctieCallback));
        printf("%s\n", strtok(strtok(rezultatFunctieCallback, " "), " "));
        if (error != SQLITE_OK){
            printf("Eroare la sqlite_exec(). %d\n", error);
        }
        else{
            printf("Command executed succesfully!\n");
        }
        while (strlen(rezultatFunctieCallback) == 0){ 
            write(client, "Acest nume de utilizator este deja luat. Incercati sa va inregistrati cu alt nume de utilizator.\n", 300);
            bzero(&rezultatFunctieCallback, sizeof(rezultatFunctieCallback));
            read(client, numeUtilizator, 300);
            printf("%s\n", numeUtilizator);
            numeUtilizator = strtok(numeUtilizator, "\n");
            char sql2[300] = "SELECT COUNT(*) FROM UTILIZATORI GROUP BY USERNAME HAVING USERNAME LIKE '";;
            strcat(sql2, numeUtilizator);
            strcat(sql2, "';");
            printf("%s\n", sql2);
            rezultatFunctieCallback = malloc (300 * sizeof(char));
            int error = sqlite3_exec(db, sql2, callBackRegister, rezultatFunctieCallback, NULL);
            printf("%s, %ld\n", rezultatFunctieCallback, strlen(rezultatFunctieCallback));
            if (error != SQLITE_OK){
                printf("Eroare la sqlite_exec(). %d\n", error);
            }
            else{
                printf("Command executed succesfully!\n");
            }
        }
        write (client, "Parola: ", 300);
        read (client, parola, 300);
        write (client, "Confirmati parola: ", 300);
        read (client, parola2, 300);
        while ((strcmp(parola, parola2) != 0)){
            write (client, "Parolele sunt diferite, introduce-ti din nou parola: ", 300);
            bzero(&parola2, sizeof(parola2));
            read (client, parola2, 300);
        }
        write (client, "Doriti sa fiti informati in legatura cu evenimentele sportive ce au loc in apropierea dumneavoastra?(Da|NU)\n", 300);
        read (client, optiuneEvenimenteSportive, 300);
        printf("%s\n", optiuneEvenimenteSportive);
        if (strcmp(optiuneEvenimenteSportive, "Da\n") == 0){
            strcat(evenimenteSportive, "1");
        }
        else if (strcmp(optiuneEvenimenteSportive, "Nu\n") == 0){
            strcat(evenimenteSportive, "0");
        }
        else{
            while (strcmp(optiuneEvenimenteSportive, "Da\n") != 0 && strcmp(optiuneEvenimenteSportive, "Nu\n") != 0){
                bzero(&optiuneEvenimenteSportive, sizeof(optiuneEvenimenteSportive));
                write (client, "Raspunsul anterior nu respecta formatul. Doriti sa fiti informati in legatura cu evenimentele sportive ce au loc in apropierea dumneavoastra?(Da|NU)\n", 300);
                read (client, optiuneEvenimenteSportive, 300);
                printf("%s\n", optiuneEvenimenteSportive);
                if (strcmp(optiuneEvenimenteSportive, "Da\n") == 0){
                    strcat(evenimenteSportive, "1");
                    break;
                }
                else if (strcmp(optiuneEvenimenteSportive, "Nu\n") == 0){
                    strcat(evenimenteSportive, "0");
                    break;
                }
            }
        }
        write (client, "Doriti sa fiti informati in legatura cu pretul carburantului?(Da|NU)\n", 300);
        read (client, optiunePretCarburant, 300);
        if (strcmp(optiunePretCarburant, "Da\n") == 0){
            strcat(pretCarburant, "1");
        }
        else if (strcmp(optiunePretCarburant, "Nu\n") == 0){
            strcat(pretCarburant, "0");
        }
        else{
            while (strcmp(optiunePretCarburant, "Da\n") != 0 && strcmp(optiunePretCarburant, "Nu\n") != 0){
                bzero(&optiunePretCarburant, sizeof(optiunePretCarburant));
                write (client, "Raspunsul anterior nu respecta formatul. Doriti sa fiti informati in legatura cu pretul carburantului?(Da|NU)\n", 300);
                read (client, optiunePretCarburant, 300);
                if (strcmp(optiunePretCarburant, "Da\n") == 0){
                    strcat(pretCarburant, "1");
                    break;
                }
                else if (strcmp(optiunePretCarburant, "Nu\n") == 0){
                    strcat(pretCarburant, "0");
                    break;
                }
            }
        }
        write (client, "Doriti sa fiti informati in legatura cu vremea?(Da|NU)\n", 300);
        read (client, optiuneVreme, 300);
        if (strcmp(optiuneVreme, "Da\n") == 0){
            strcat(vreme, "1");
        }
        else if (strcmp(optiuneVreme, "Nu\n") == 0){
            strcat(vreme, "0");
        }
        else{
            while (strcmp(optiuneVreme, "Da\n") != 0 && strcmp(optiuneVreme, "Nu\n") != 0){
                bzero(&evenimenteSportive, sizeof(evenimenteSportive));
                write (client, "Raspunsul anterior nu respecta formatul. Doriti sa fiti informati in legatura cu vremea?(Da|NU)\n", 300);
                read (client, optiuneVreme, 300);
                if (strcmp(optiuneVreme, "Da\n") == 0){
                    strcat(vreme, "1");
                    break;
                }
                else if (strcmp(optiuneVreme, "Nu\n") == 0){
                    strcat(vreme, "0");
                    break;
                }
            }
        }
        if (strcmp(parola, parola2) == 0){ //if ul acesta trebuie eliminat la un moment dat :)
            char * parolaFaraNewLine = malloc(sizeof(char) * 200);
            parolaFaraNewLine = strtok(parola, "\n");
            char registerCommand[200] = "INSERT INTO UTILIZATORI ( USERNAME, PASSWORD, EVENIMENTE_SPORTIVE, PRET_CARBURANT, VREME ) VALUES (";
            strcat(registerCommand, "'");
            strcat(registerCommand, numeUtilizator);
            strcat(registerCommand, "'");
            strcat(registerCommand, ",");

            strcat(registerCommand, "'");
            strcat(registerCommand, parolaFaraNewLine);
            strcat(registerCommand, "'");
            strcat(registerCommand, ",");
            
            strcat(registerCommand, evenimenteSportive);
            strcat(registerCommand, ",");

            strcat(registerCommand, pretCarburant);
            strcat(registerCommand, ",");

            strcat(registerCommand, vreme);
            strcat(registerCommand, ");");
            rezultatFunctieCallback = malloc (300 * sizeof(char));
            int error = sqlite3_exec(db, registerCommand, callBackRegister, rezultatFunctieCallback, NULL); // Aici va fi nevoie de o noua functie de callback care sa verifice raspunsul dat de baza de date
            printf("%s\n", rezultatFunctieCallback);
            if (error != SQLITE_OK){
                printf("Eroare la sqlite_exec(). %d\n", error);
                while (error != SQLITE_OK){
                    error = sqlite3_exec(db, registerCommand, callBackRegister, rezultatFunctieCallback, NULL); // Aici va fi nevoie de o noua functie de callback care sa verifice raspunsul dat de baza de date
                    //printf("%s\n", rezultatFunctieCallback);
                }
            }
            else{
                printf("Command executed succesfully!\n");
            }
        }
        char mesajRaspuns[] = "Salut ";
        strcat(mesajRaspuns, numeUtilizator);
        strcat(mesajRaspuns, ", inregistrarea a avut loc cu succes! :)");
        strcat(mesajRaspuns, "\n");
        write (client, mesajRaspuns, 300);
        weAreLoggedIn = 1;
    }
    else{
        write(client, "Comanda nerecunoscuta! Va rugam sa va conectati sau sa va autentificati.\n", 300);
    }
    //pthread_mutex_unlock(&databaseLock);
    write(client, "Connected\n", 300);
    fcntl(0, F_SETFL, O_NONBLOCK);
    char msg[300], msg1[300], msg2[5], msg3[5], msg4[5], msg5[5];
    while(1){
        fd_set current_fd, ready_fd;
        FD_ZERO(&current_fd);
        FD_SET(0, &current_fd);
        FD_SET(fisierPretCarburant, &current_fd);
        FD_SET(fisierVreme, &current_fd);
        FD_SET(fisierEvenimenteSportive, &current_fd);
        FD_SET(userAlert, &current_fd);
        FD_SET(client, &current_fd);
        // Din moment ce select este destructiv, vom face o copie pentru current_fd
        bcopy( (char *)&current_fd, (char *)&ready_fd , sizeof(ready_fd));
        if(select(FD_SETSIZE, &ready_fd, NULL, NULL, NULL) < 0){
            perror("Eroare la select().\n");
        }
        if (FD_ISSET(client, &ready_fd)){
            bzero(msg, sizeof(msg));
            read(client, msg, 300);
            printf("%s", msg);
            fflush(stdout);
            if (strlen(msg) >3 && strcmp(strtok(msg, " "), "Viteza") == 0){
                vitezaActualaString = strtok(NULL, " ");
                vitezaActuala = atoi(vitezaActualaString);
                locatieActuala = strtok(NULL, "\n");
                strcpy(locatieActualaPermanenta, locatieActuala);
                printf("Am primit viteza si locatia %d, %s!\n", vitezaActuala, locatieActualaPermanenta);
                char streetMsg[300] = "Pe ";
                strcat(streetMsg, locatieActualaPermanenta);
                strcat(streetMsg, " limita legala de viteza este de ");
                char buffer[50];
                sprintf(buffer, "%d", Strazi[indexStrada(locatieActualaPermanenta)].vitezaMaxima);
                strcat(streetMsg, buffer);
                strcat(streetMsg, " km/h.");
                if (Strazi[indexStrada(locatieActualaPermanenta)].vitezaMaxima < vitezaActuala){
                     strcat(streetMsg, "\nVa recomandam sa incetiniti.");
                }
                strcat(streetMsg, "\n\n\n");
                write(client, streetMsg, 300);
                //Daca utilizatorul a trimis o noua viteza si o noua locatie, pentru acea noua locatie trebuie sa ii oferim informatiile pentru care a optat
                if (strcmp(evenimenteSportive, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    char anunturiSportive[10][100] = {"Raliul Dakar", "Campionatul de Formula 1", "SuperBowl", "Miami Open", "Turul Frantei", "Turneul de la Wimbledon", 
                    "Campionatul Mondial de handbal feminin", "Campionatul European de fotbal", "Australian Open", "Campionatele Europene de Gimnastica"};
                    char mesajDeTrimis1[300] = "In momentul de fata, pe ";
                    strcat(mesajDeTrimis1, locatieActualaPermanenta);
                    strcat(mesajDeTrimis1, " are loc ");
                    strcat(mesajDeTrimis1, anunturiSportive[rand()%10]);
                    strcat(mesajDeTrimis1, "\n");
                    write(client, mesajDeTrimis1, 300);
                }
                if (strcmp(pretCarburant, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    char buffer[50];
                    sprintf(buffer, "%d", rand()%10);
                    char mesajDeTrimis2[300] = "In momentul de fata, pe ";
                    strcat(mesajDeTrimis2, locatieActualaPermanenta);
                    strcat(mesajDeTrimis2, " carburantul se gaseste la un pret de ");
                    strcat(mesajDeTrimis2, buffer);
                    strcat(mesajDeTrimis2, " lei\n");
                    write(client, mesajDeTrimis2, 300);
                }
                if (strcmp(vreme, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    char anunturiVreme[10][50] = {"senin", "vant", "ploaie", "ninsoare", "acoperire cu nori", "ceata", "furtuna de praf", "tornada", "uragan", "furtuna cu gheata"};
                    char mesajDeTrimis3[300] = "In momentul de fata, pe ";
                    strcat(mesajDeTrimis3, locatieActualaPermanenta);
                    strcat(mesajDeTrimis3, " are loc evenimentul meteorologic ");
                    strcat(mesajDeTrimis3,  anunturiVreme[rand()%10]);
                    strcat(mesajDeTrimis3, "\n");
                    write(client, mesajDeTrimis3, 300);
                }
            }
            else if (strlen(msg) >3 && strcmp(strtok(msg, "\n"), "Accident") == 0){
                printf("Am primit accidentul la locatia %s!\n", locatieActualaPermanenta);
                pthread_mutex_lock(&mesajGlobalLock);
                strcpy(locatieAlertaGlobala, locatieActualaPermanenta);
                strcpy(mesajAlertaGlobal, "accident");
                strcpy(mesajAlertaLocal, "accident"); // Pentru a evita afisarea in cadrul acestui thread
                strcpy(locatieAlertaLocal, locatieActualaPermanenta);
                pthread_mutex_unlock(&mesajGlobalLock);
            }
            else if (strlen(msg) >3 && strcmp(strtok(msg, "\n"), "Animale") == 0){
                printf("Am primit accidentul la locatia %s!\n", locatieActualaPermanenta);
                pthread_mutex_lock(&mesajGlobalLock);
                strcpy(locatieAlertaGlobala, locatieActualaPermanenta);
                strcpy(mesajAlertaGlobal, "animale pe drum");
                strcpy(mesajAlertaLocal, "animale pe drum"); // Pentru a evita afisarea in cadrul acestui thread
                strcpy(locatieAlertaLocal, locatieActualaPermanenta);
                pthread_mutex_unlock(&mesajGlobalLock);
            }
            else if (strlen(msg) >3 && strcmp(strtok(msg, "\n"), "Radar") == 0){
                printf("Am primit accidentul la locatia %s!\n", locatieActualaPermanenta);
                pthread_mutex_lock(&mesajGlobalLock);
                strcpy(locatieAlertaGlobala, locatieActualaPermanenta);
                strcpy(mesajAlertaGlobal, "radar");
                strcpy(mesajAlertaLocal, "radar"); // Pentru a evita afisarea in cadrul acestui thread
                strcpy(locatieAlertaLocal, locatieActualaPermanenta);
                pthread_mutex_unlock(&mesajGlobalLock);
            }
            else if (strlen(msg) >3 && strcmp(strtok(msg, "\n"), "Groapa") == 0){
                printf("Am primit accidentul la locatia %s!\n", locatieActualaPermanenta);
                pthread_mutex_lock(&mesajGlobalLock);
                strcpy(locatieAlertaGlobala, locatieActualaPermanenta);
                strcpy(mesajAlertaGlobal, "groapa extrem de periculoasa");
                strcpy(mesajAlertaLocal, "groapa extrem de periculoasa"); // Pentru a evita afisarea in cadrul acestui thread
                strcpy(locatieAlertaLocal, locatieActualaPermanenta);
                pthread_mutex_unlock(&mesajGlobalLock);
            }
            else if (strlen(msg) >3 && strcmp(strtok(msg, "\n"), "Quit") == 0){
                printf("Unul dintre clienti s-a deconectat. :(\n");
                pthread_exit(NULL);
            }
            else{
                printf("Unul dintre clienti s-a deconectat. :(\n");
                pthread_exit(NULL);
            }
        }
        if(FD_ISSET(0, &ready_fd)){
            //Se pot raporta informatii si la nivelul serverului si anume daca se scrie un mesaj de interes in cadrul serverului, va fi tratat ca orice mesaj primit de la unul dintre clienti
            bzero(msg1, sizeof(msg1));
            fgets(msg1, 300, stdin);
            printf("%s\n", msg1);
            fflush(stdout);
        }
        if(FD_ISSET(fisierEvenimenteSportive, &ready_fd)){
            bzero(msg2, sizeof(msg2));
            read(fisierEvenimenteSportive, msg2, sizeof(char));
            if (strcmp(msg2, "1") && strcmp(mesajSportAnterior, mesajEvenimenteSportive)){
                //printf("%s %s", locatieEvenimentSportiv, mesajEvenimenteSportive);
                strcpy(mesajSportAnterior, mesajEvenimenteSportive);
                if (strcmp(evenimenteSportive, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    if (strcmp(locatieEvenimentSportiv, locatieActualaPermanenta) == 0 || suntVecine(locatieEvenimentSportiv, locatieActualaPermanenta) == 1){
                        char mesajDeTrimis1[300] = "In momentul de fata, pe ";
                        strcat(mesajDeTrimis1, locatieEvenimentSportiv);
                        if (suntVecine(locatieEvenimentSportiv, locatieActualaPermanenta) == 1){
                            strcat(mesajDeTrimis1, ", strada vecina cu cea pe care va aflati,");
                        }
                        strcat(mesajDeTrimis1, " are loc ");
                        strcat(mesajDeTrimis1, mesajEvenimenteSportive);
                        write(client, mesajDeTrimis1, 300);
                    }
                }
                fflush(stdout);
            }
        }
        if(FD_ISSET(fisierPretCarburant, &ready_fd)){
            bzero(msg3, sizeof(msg3));
            read(fisierPretCarburant, msg3, sizeof(char));
            if (strcmp(msg3, "1") && strcmp(mesajCarburantAnterior, mesajPretCarburant)){
                //printf("%s %s\n", locatiePretCarburant, mesajPretCarburant);
                strcpy(mesajCarburantAnterior, mesajPretCarburant);
                if (strcmp(pretCarburant, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    if (strcmp(locatiePretCarburant, locatieActualaPermanenta) == 0 || suntVecine(locatieActualaPermanenta, locatiePretCarburant) == 1){
                        char mesajDeTrimis2[300] = "In momentul de fata, pe ";
                        strcat(mesajDeTrimis2, locatiePretCarburant);
                        if (suntVecine(locatiePretCarburant, locatieActualaPermanenta) == 1){
                            strcat(mesajDeTrimis2, ", strada vecina cu cea pe care va aflati,");
                        }
                        strcat(mesajDeTrimis2, " carburantul se gaseste la un pret de ");
                        strcat(mesajDeTrimis2, mesajPretCarburant);
                        strcat(mesajDeTrimis2, " lei\n");
                        write(client, mesajDeTrimis2, 300);
                    }
                }
                fflush(stdout);
            }
        }
        if(FD_ISSET(fisierVreme, &ready_fd)){
            bzero(msg4, sizeof(msg4));
            read(fisierVreme, msg4, sizeof(char));
            if(strcmp(msg4, "1") && strcmp(mesajVremeAnterior, mesajVreme)){
                //printf("%s %s", locatieVreme, mesajVreme);
                strcpy(mesajVremeAnterior, mesajVreme);
                if (strcmp(vreme, "1") == 0){ //Daca acest client a optat pentru optiunea de a primi aceste mesaje
                    if (strcmp(locatieVreme, locatieActualaPermanenta) == 0 || suntVecine(locatieActualaPermanenta, locatieVreme) == 1){
                        char mesajDeTrimis3[300] = "In momentul de fata, pe ";
                        strcat(mesajDeTrimis3, locatieVreme);
                        if (suntVecine(locatieVreme, locatieActualaPermanenta) == 1){
                            strcat(mesajDeTrimis3, ", strada vecina cu cea pe care va aflati,");
                        }
                        strcat(mesajDeTrimis3, " are loc evenimentul meteorologic ");
                        strcat(mesajDeTrimis3, mesajVreme);
                        write(client, mesajDeTrimis3, 300);
                    }
                }
                fflush(stdout);
            }
        }
        if(FD_ISSET(userAlert, &ready_fd)){
            bzero(msg5, sizeof(msg5));
            read(userAlert, msg5, sizeof(char));
            char temp1[50], temp2[50], temp3[50], temp4[50];
            strcpy(temp1, locatieAlertaGlobala);
            strcpy(temp2, mesajAlertaGlobal);
            strcpy(temp3, locatieAlertaLocal);
            strcpy(temp4, mesajAlertaLocal);
            if(strcmp(msg5, "1") && ( strcmp(mesajAlertaGlobal, mesajAlertaLocal) || strcmp(locatieAlertaLocal, locatieAlertaGlobala)) && strlen(mesajAlertaGlobal)>0 && strcmp(strcat(temp1, temp2), strcat(temp3, temp4))){
                printf("%s %s\n", locatieAlertaGlobala, mesajAlertaGlobal);
                strcpy(mesajAlertaLocal, mesajAlertaGlobal);
                strcpy(locatieAlertaLocal, locatieAlertaGlobala);
                if (strcmp(locatieAlertaGlobala, locatieActualaPermanenta) == 0 || suntVecine(locatieActualaPermanenta, locatieAlertaGlobala) == 1){
                    char mesajDeTrimis4[300] = "Un utilizator a raportat, la locatia ";
                    pthread_mutex_lock(&mesajGlobalLock);
                    strcat(mesajDeTrimis4, locatieAlertaGlobala);
                    if (suntVecine(locatieAlertaGlobala, locatieActualaPermanenta) == 1){
                        strcat(mesajDeTrimis4, ", strada vecina cu cea pe care va aflati,");
                        strcat(mesajDeTrimis4, " urmatorul eveniment din trafic: ");
                    }
                    else{
                        strcat(mesajDeTrimis4, ", urmatorul eveniment din trafic: ");
                    }
                    strcat(mesajDeTrimis4, mesajAlertaGlobal);
                    pthread_mutex_unlock(&mesajGlobalLock);
                    strcat(mesajDeTrimis4, "\n");
                    write(client, mesajDeTrimis4, 300);
                }
                fflush(stdout);
            }
        }
    }
    close(client);
} 
void * threadEventsRoutine (void * arg){
    int client = *((int *)arg);
    srand(time(0));
    //Aceste date ar fi luate in mod normal de pe internet, dar pentru a demonstra functionalitatea programului vom alege cu ajutorul rand() dintre niste mesaje predefinite
    char strazi[10][50] = {"Str. Nicolina", "Bd. Alexandru Cel Bun", "Bd. Dacia", "Str. Arcu", "Bd. Poitiers", "Str. Libertatii", "Bd. Tutora", "Str. Ciric", "Bd. Independentei", "Bd. C.A. Rosetti"};
    char anunturiSportive[10][100] = {"Raliul Dakar", "Campionatul de Formula 1", "SuperBowl", "Miami Open", "Turul Frantei", "Turneul de la Wimbledon", 
    "Campionatul Mondial de handbal feminin", "Campionatul European de fotbal", "Australian Open", "Campionatele Europene de Gimnastica"};
    char anunturiVreme[10][50] = {"senin", "vant", "ploaie", "ninsoare", "acoperire cu nori", "ceata", "furtuna de praf", "tornada", "uragan", "furtuna cu gheata"};
    lseek(fisierEvenimenteSportive, 0, SEEK_SET);
    lseek(fisierVreme, 0, SEEK_SET);
    lseek(fisierPretCarburant, 0, SEEK_SET);
    char temp1[300];
    char temp2[300];
    char temp3[300];
    while(1){
        sleep(20);
        bzero(temp1, 300 * sizeof(char));
        bzero(temp2, 300 * sizeof(char));
        bzero(temp3, 300 * sizeof(char));
        //strcat(temp1, strazi[rand()%10]);
        //strcat(temp1, " ");
        strcpy(locatieEvenimentSportiv, strazi[rand()%10]);
        strcat(temp1, anunturiSportive[rand()%10]);
        strcat(temp1, "\n");
        strcpy(mesajEvenimenteSportive, temp1);
        printf("%s %s", locatieEvenimentSportiv, temp1);
        fflush(stdout);
        write(fisierEvenimenteSportive, "1", sizeof(char));

        //strcat(temp2, strazi[rand()%10]);
        //strcat(temp2, " ");
        strcpy(locatieVreme, strazi[rand()%10]);
        strcat(temp2, anunturiVreme[rand()%10]);
        strcat(temp2, "\n");
        strcpy(mesajVreme, temp2);
        printf("%s %s", locatieVreme, temp2);fflush(stdout);
        write(fisierVreme, "1", sizeof(char));

        //strcat(temp3, strazi[rand()%10]);
        //strcat(temp3, " ");
        strcpy(locatiePretCarburant, strazi[rand()%10]);
        char buffer[50];
        sprintf(buffer, "%d", rand()%10);
        strcat(temp3, buffer);
        //strcat(temp3, "\n");
        strcpy(mesajPretCarburant, temp3);
        printf("%s %s\n", locatiePretCarburant, temp3);fflush(stdout);
        write(fisierPretCarburant, "1", sizeof(char));

    }
}
void initializareStrazi(){
    char strazi[10][50] = {"Str. Nicolina", "Bd. Alexandru Cel Bun", "Bd. Dacia", "Str. Arcu", "Bd. Poitiers", "Str. Libertatii", "Bd. Tutora", "Str. Ciric", "Bd. Independentei", "Bd. C.A. Rosetti"};
    for(int i=0;i<10;i++){
        strcpy(Strazi[i].numeStrada, strazi[i]);
    }
    int viteze[] = {30, 50, 70, 60, 15};
    for(int j=0;j<10;j++){
        Strazi[j].vitezaMaxima = viteze[rand()%6];
    }
    //Presupunem ca strazile sunt alaturate in felul urmator : Nicolina - LIbertatii, Libertatii - Poitiers, Poitiers - Nicolina, Alexandru - Dacia, Arcu - Independentei, Ciric - Independentei, C.A.Rosetti - Independentei 
    Strazi[0].vecini[0] = 4; 
    Strazi[0].vecini[1] = 5;
    Strazi[0].marimeListaVecini = 2;

    Strazi[4].vecini[0] = 0; 
    Strazi[4].vecini[1] = 5;
    Strazi[4].marimeListaVecini = 2;

    Strazi[5].vecini[0] = 4; 
    Strazi[5].vecini[1] = 0;
    Strazi[5].marimeListaVecini = 2; 

    Strazi[1].vecini[0] = 2;
    Strazi[1].marimeListaVecini = 1;
    
    Strazi[2].vecini[0] = 1;
    Strazi[2].marimeListaVecini = 1;

    Strazi[3].vecini[0] = 8;
    Strazi[3].marimeListaVecini = 1;

    Strazi[7].vecini[0] = 8;
    Strazi[7].marimeListaVecini = 1;

    Strazi[9].vecini[0] = 8;
    Strazi[9].marimeListaVecini = 1;

    Strazi[8].vecini[0] = 3;
    Strazi[8].vecini[1] = 7;
    Strazi[8].vecini[2] = 9;
    Strazi[8].marimeListaVecini = 3;
}
int indexStrada(char * numeStrada){
    for (int i=0;i<10;i++){
        if (strcmp(Strazi[i].numeStrada, numeStrada) == 0){
            return i;
        }
    }
    return -1;
}
int suntVecine(char * primaStrada, char * aDouaStrada){
    int a = indexStrada(primaStrada);
    int b = indexStrada(aDouaStrada);
    for (int i=0;i<Strazi[a].marimeListaVecini;i++){
        if (Strazi[a].vecini[i] == b){
            return 1;
        }
    }
    return -1;
}
