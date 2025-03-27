#include <sqlite3.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

/* codul de eroare returnat de anumite apeluri */
extern int errno;
/* portul de conectare la server*/
int port;

int main(int argc, char *argv[])
{
  /// Declaratii
  int sd;                    // descriptorul de socket
  struct sockaddr_in server; // structura folosita pentru conectare
                             // mesajul trimis
  int nr = 0;

  /* exista toate argumentele in linia de comanda? */
  if (argc != 3)
  {
    printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
    return -1;
  }

  // stabilim portul
  port = atoi(argv[2]);
  // cream socketul
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Eroare la socket().\n");
    return errno;
  }

  /// realizam conexiunea cu server-ul

  /* familia socket-ului */
  server.sin_family = AF_INET;
  /* adresa IP a serverului */
  server.sin_addr.s_addr = inet_addr(argv[1]);
  /* portul de conectare */
  server.sin_port = htons(port);

  /* ne conectam la server */
  if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[client] Eroare la connect().\n");
    return errno;
  }
  printf("[client] Operatiile care pot fi folosite sunt: \n> vizualizare comenzi \n> creare cont : <username> <parola> \n> login : <username> <parola> \n> logout \n> exit \n> vizualizare sold \n> retragere sold : <suma> <parola> \n> adaugare sold : <suma> <parola> \n> vizualizare produse \n> vizualizare produse proprii \n> cumparare produs : <id> \n> listare produs : <nume> <pret> \n> cautare produs : <nume> \n");

  while (1)
  {
    char mesaj[200] = "";
    char raspuns[1000] = "";

    /* citirea mesajului */
    printf("> ");
    fflush(stdout);
    read(0, mesaj, sizeof(mesaj));
    mesaj[strlen(mesaj) - 1] = '\0';

    //printf("[client] Mesajul citit de la tastatura este: '%s'.\n", mesaj);

    /* trimiterea mesajului la server */
    if (write(sd, mesaj, sizeof(mesaj)) <= 0)
    {
      perror("[client] Eroare la write() spre server.\n");
      return errno;
    }

    /* citirea raspunsului dat de server
       (apel blocat pina cand serverul raspunde) */
    if (read(sd, raspuns, sizeof(raspuns)) < 0)
    {
      perror("[client] Eroare la read() de la server.\n");
      return errno;
    }
    /* afisam mesajul primit sau iesim din while(1) daca primim mesajul: exit*/
    if(strcmp(raspuns, "Mesajul primit este exit. Conexiunea se termina.") == 0){
      printf("[client] %s\n", raspuns);
      break;
    }
    printf("[client] %s\n", raspuns);
  }
  /* inchidem conexiunea, am terminat */
  close(sd);
}