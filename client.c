#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

void * threadRoutine (void * arg);
char strazi[10][50] = {"Str. Nicolina", "Bd. Alexandru Cel Bun", "Bd. Dacia", "Str. Arcu", "Bd. Poitiers", "Str. Libertatii", "Bd. Tutora", "Str. Ciric", "Bd. Independentei", "Bd.C.A. Rosetti"};

int main(int argc , char * argv[]){
    int mySocket, loggedIn = 0;
    struct sockaddr_in server;
    char msg[300], msg1[300];

    //Cream socketul
    if ((mySocket = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("Eroare la socket().\n");
      return 1;
    }
    // Ne asiguram ca server este formatat 
    bzero(&server, sizeof(server));
    // Formatam structura server
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1"); // Adresa IP a serverului este hardcodata asa pentru ca in cazul unei aplicatii client, aceasta se va conecta mereu la acelasi server
    server.sin_port = htons (2024);

    // Urmeaza conectarea la server
    if(connect(mySocket, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1){
        perror("Eroare la connect().\n");
        return 2;
    }
    fcntl(0, F_SETFL, O_NONBLOCK);
    
    while(1){
      fd_set current_fd, ready_fd;
      FD_ZERO(&current_fd);
      FD_SET(0, &current_fd);
      FD_SET(mySocket, &current_fd);
      // Din moment ce select este destructiv, vom face o copie pentru current_fd
      bcopy( (char *)&current_fd, (char *)&ready_fd , sizeof(ready_fd));
      if(select(FD_SETSIZE, &ready_fd, NULL, NULL, NULL) < 0){
        perror("Eroare la select().\n");
        return 3;
      }
      if (FD_ISSET(mySocket, &ready_fd)){
        bzero(msg1, sizeof(msg1));
        read(mySocket, msg1, 300);
        if (strcmp(msg1, "Connected\n") == 0){ // AM PRIMIT SEMNALUL CUM CA NE AM AUTENTIFICAT 
          loggedIn = 1;
          pthread_t t;
          if (pthread_create(&t, NULL, *threadRoutine, &mySocket) != 0){
            perror("Eroare la pthread_create().\n");
            return 4;
          }
        }
        else{
          printf("%s", msg1);
          fflush(stdout); // A nu se uita de aici incolo !! STDOUT NECESITA FFLUSH, ALTFEL STAM INCA 3 ORE LA DEBUGG :(
        }
      }
      else if(FD_ISSET(0, &ready_fd)){
        bzero(msg, sizeof(msg));
        fgets(msg, 300, stdin);
        write(mySocket, msg, 300);
      }
    }
    close(mySocket);
    return 0;
}
void * threadRoutine (void * arg){
  int server = *((int *)arg);
  char buffer1[20];
  srand(time(0));
  while(1){
    char mesajVitezaLocatie[300] = "Viteza ";
    sprintf(buffer1, "%d", rand()%70);
    strcat(mesajVitezaLocatie, buffer1);
    strcat(mesajVitezaLocatie, " ");
    strcat(mesajVitezaLocatie, strazi[rand()%10]);
    strcat(mesajVitezaLocatie, "\n");
    write(server, mesajVitezaLocatie, 300);
    printf("%s\n", mesajVitezaLocatie);
    sleep(60);
  }
}
