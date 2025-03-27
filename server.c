#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <sqlite3.h>

/* portul folosit */
#define PORT 2909

/* codul de eroare returnat de anumite apeluri */
extern int errno;

typedef struct thData
{
  int idThread; // id-ul thread-ului tinut in evidenta de acest program
  int cl;       // descriptorul intors de accept
} thData;

/// declaratii baza de date
sqlite3 *db; // Conexiune la baza de date

/// vectorul cu care verificam daca putem reutiliza thread-urile
int folosit[100] = {0};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
void raspunde(void *);
int decl_baza_de_date();
int check_login(sqlite3 *db, const char *username, const char *password);
int add_user(sqlite3 *db, const char *username, const char *password);
int get_sold_value_as_string(sqlite3 *db, const char *username, char *sold_str);
int update_sold_with_password(sqlite3 *db, const char *username, const char *password, int operatie, const char *suma);
char *get_all_products(sqlite3 *db);
char *get_user_products(sqlite3 *db, const char *username);
int add_product(sqlite3 *db, const char *username, const char *product_name, const char *price_str);
char *search_product(sqlite3 *db, const char *name);
int acquire_product(sqlite3 *db, const char *product_id_str, const char *username);
int get_by_id(sqlite3 *db, int user_id, double valoare, int operatie);

int main()
{
  struct sockaddr_in server; // structura folosita de server
  struct sockaddr_in from;
  int nr; // mesajul primit de trimis la client
  int sd; // descriptorul de socket
  int pid;
  pthread_t th[101]; // Identificatorii thread-urilor care se vor crea
  int i = 0;

  if (decl_baza_de_date() == 1) // conectarea la baza de date
  {
    return 1;
  }

  /// creeare socket si tot ce mai avem nevoie

  /* crearea unui socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  /* pregatirea structurilor de date */
  bzero(&server, sizeof(server));
  bzero(&from, sizeof(from));

  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
  server.sin_family = AF_INET;
  /* acceptam orice adresa */
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  /* utilizam un port utilizator */
  server.sin_port = htons(PORT);

  /* atasam socketul */
  if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen(sd, 2) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }
  /* servim in mod concurent clientii...folosind thread-uri */
  while (1)
  {
    int client;
    thData *td; // parametru functia executata de thread
    int length = sizeof(from);

    printf("[server]Asteptam la portul %d...\n", PORT);
    fflush(stdout);

    /* acceptam un client (stare blocata pina la realizarea conexiunii) */
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[server]Eroare la accept().\n");
      continue;
    }

    /* s-a realizat conexiunea, se astepta mesajul */

    td = (struct thData *)malloc(sizeof(struct thData));

    /// aici incrcam sa refolosim thread-urile
    pthread_mutex_lock(&mutex);
    for (i = 0; i < 100; i++)
    {
      printf("am gasit thread-ul %d cu valoarea %d\n", i, folosit[i]);
      if (folosit[i] == 0)
      {
        td->idThread = i;
        folosit[i] = 1;
        td->cl = client;
        break;
      }
    }
    pthread_mutex_unlock(&mutex);

    if (i == 100)
    {
      printf("No threads available, rejecting client.\n");
      close(client);
    }
    else
    {
      printf("\nId-ul clientului este: %d\n", client);
      pthread_create(&th[i], NULL, &treat, td); // Create thread to handle client
    }

  } // while
};
static void *treat(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);
  printf("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
  fflush(stdout);
  pthread_detach(pthread_self());
  raspunde((struct thData *)arg);

  folosit[tdL.idThread] = 0;
  free(arg);
  return (NULL);
};

void raspunde(void *arg)
{
  char mesaj[1000] = "", raspuns[1000] = "", username[100] = "", parola[50] = "", suma[50] = "", id[50] = "", nume[100] = "", sold[50] = "";
  struct thData tdL;
  tdL = *((struct thData *)arg);
  int logged_in = 0;
  char username_logged[100] = ""; /// variabilele pt verif daca suntem conectati sau nu

  while (1)
  {
    int biti;
    if ((biti = read(tdL.cl, mesaj, sizeof(mesaj))) <= 0) // Read message from client
    {
      if (errno == 0) // Connection closed by client
        break;
      printf("[Thread %d]\n", tdL.idThread);
      perror("Eroare la read() de la client.\n");
      break;
    }

    printf("[Thread %d] Mesajul a fost receptionat...'%s'\n", tdL.idThread, mesaj);

    if (strstr(mesaj, "login : ") == mesaj) /// login
    {
      if (logged_in == 1)
        strcpy(raspuns, "Sunteti deja intr-un cont.");
      else
      {
        int i = 8, j = 0;
        while (mesaj[i] != ' ')
        {
          username[j] = mesaj[i];
          j++;
          i++;
        }
        username[j] = '\0';

        i++;
        j = 0;
        while (mesaj[i] != '\0')
        {
          parola[j] = mesaj[i];
          i++;
          j++;
        }
        parola[j] = '\0';
        if (i != strlen(mesaj))
        {
          strcpy(raspuns, "Comanda nu este corecta.\n");
        }
        else
        {
          if (check_login(db, username, parola))
          {
            strcpy(raspuns, "Contul respectiv este valid");
            strcpy(username_logged, username);
            logged_in = 1;
          }
          else
            strcpy(raspuns, "Contul respectiv NU ESTE VALID");
        }
      }
    }
    else if (strstr(mesaj, "exit") == mesaj) /// exit
    {
      strcpy(raspuns, "Mesajul primit este exit. Conexiunea se termina.");
      // Send response back to client
      if (write(tdL.cl, raspuns, sizeof(raspuns)) <= 0)
      {
        printf("[Thread %d] ", tdL.idThread);
        perror("[Thread] Eroare la write() catre client.\n");
        break;
      }
      else
      {
        printf("[Thread %d] Mesajul a fost trasmis cu succes.\n", tdL.idThread);
      }
      break;
    }
    else if (strstr(mesaj, "vizualizare comenzi") == mesaj) /// vizualizare operatii
    {
      strcpy(raspuns, "Comenzile care pot fi folosite sunt: \n> login : <username> <parola> \n> vizualizare comenzi \n> exit \n> vizualizare sold \n> retragere sold : <suma> <parola> \n> adaugare sold : <suma> <parola> \n> logout \n> creare cont : <username> <parola> \n> vizualizare produse \n> vizualizare produse proprii \n> cumparare produs : <id> \n> listare produs : <nume> <pret> \n> cautare produs : <nume>");
    }
    else if (strstr(mesaj, "vizualizare sold") == mesaj) /// vizualizare sold
    {
      if (logged_in == 0)
        strcpy(raspuns, "Nu esti logat.");
      else
      {
        get_sold_value_as_string(db, username_logged, sold);
        strcpy(raspuns, "Soldul dumneavoasta este: ");
        strcat(raspuns, sold);
      }
    }
    else if (strstr(mesaj, "retragere sold : ") == mesaj) /// retragere sold
    {
      if (logged_in == 0)
      {
        strcpy(raspuns, "Nu sunteti logat intr-un cont.");
      }
      else
      {
        int i = 17;
        int j = 0;
        while (mesaj[i] != ' ')
        {
          suma[j] = mesaj[i];
          i++;
          j++;
        }
        suma[j] = '\0';
        i++;
        j = 0;
        while (mesaj[j] != '\0')
        {
          parola[j] = mesaj[i];
          i++;
          j++;
        }
        parola[j] = '\0';
        for (j = 0; j < strlen(suma); j++)
        {
          if (!(suma[j] >= '0' && suma[j] <= '9'))
          {
            break;
          }
        }
        if (j < strlen(suma))
          strcpy(raspuns, "Comanda introdusa este invalida");
        else
        {
          int operatie = 0;
          if (update_sold_with_password(db, username_logged, parola, operatie, suma))
            strcpy(raspuns, "Valoare soldului a fost schimbata.");
          else
            strcpy(raspuns, "S-a produs o eroare.");
        }
      }
    }
    else if (strstr(mesaj, "adaugare sold : ") == mesaj) /// adaugare sold
    {
      if (logged_in == 0)
      {
        strcpy(raspuns, "Nu sunteti logat intr-un cont.");
      }
      else
      {
        int i = 16;
        int j = 0;
        while (mesaj[i] != ' ')
        {
          suma[j] = mesaj[i];
          i++;
          j++;
        }
        suma[j] = '\0';
        i++;
        j = 0;
        while (mesaj[j] != '\0')
        {
          parola[j] = mesaj[i];
          i++;
          j++;
        }
        parola[j] = '\0';
        for (j = 0; j < strlen(suma); j++)
        {
          if (!(suma[j] >= '0' && suma[j] <= '9'))
          {
            break;
          }
        }
        if (j < strlen(suma))
          strcpy(raspuns, "Comanda introdusa este invalida");
        else
        {
          int operatie = 1;
          if (update_sold_with_password(db, username_logged, parola, operatie, suma))
            strcpy(raspuns, "Valoare soldului a fost schimbata.");
          else
            strcpy(raspuns, "S-a produs o eroare.");
        }
      }
    }
    else if (strcmp(mesaj, "logout") == 0) /// logout
    {
      if (logged_in == 0)
        strcpy(raspuns, "Nu esti logat.");
      else
      {
        logged_in = 0;
        strcpy(raspuns, "Contul curent a fost delogat cu succes");
      }
    }
    else if (strstr(mesaj, "creare cont : ") == mesaj) /// creare cont
    {
      if (logged_in == 1)
        strcpy(raspuns, "Trebuie sa nu fii logat pentru a crea un cont nou.");
      else
      {
        int i = 14, j = 0;
        strcpy(raspuns, "Comanda primita este creare cont. ");
        while (mesaj[i] != ' ')
        {
          username[j] = mesaj[i];
          j++;
          i++;
        }
        username[j] = '\0';

        i++;
        j = 0;
        while (mesaj[i] != '\0')
        {
          parola[j] = mesaj[i];
          i++;
          j++;
        }
        parola[j] = '\0';
        if (i != strlen(mesaj))
        {
          strcpy(raspuns, "Comanda nu este corecta.\n");
        }
        else
        {
          if (add_user(db, username, parola))
          {
            strcpy(raspuns, "Am creat un user nou.");
          }
        }
      }
    }
    else if (strcmp(mesaj, "vizualizare produse") == 0) /// vizualizare produse
    {
      if (logged_in == 1)
      {
        char *temp = get_all_products(db);
        strcpy(raspuns, "Produsele sunt:");
        strcat(raspuns, temp);
      }
      else
        strcpy(raspuns, "Nu esti logat");
    }
    else if (strcmp(mesaj, "vizualizare produse proprii") == 0) /// vizualizare produse proprii
    {
      if (logged_in == 1)
      {
        char *temp = get_user_products(db, username_logged);
        strcpy(raspuns, "Produsele tale sunt:");
        strcat(raspuns, temp);
      }
      else
        strcpy(raspuns, "Nu esti logat");
    }
    else if (strstr(mesaj, "cumparare produs : ") == mesaj) /// achizitionare produs
    {
      if (logged_in == 0)
        strcpy(raspuns, "Nu esti logat.");
      else
      {
        int i = 19, j = 0;
        while (mesaj[i] != '\0')
        {
          id[j] = mesaj[i];
          i++;
          j++;
        }
        id[j] = '\0';
        if (acquire_product(db, id, username_logged) == 1)
          strcpy(raspuns, "Produsul a fost cumparat! Felicitari!");
        else if(acquire_product(db, id, username_logged) == 0)
          strcpy(raspuns, "Nu s-a putut finaliza tranzactia.");
        else strcpy(raspuns, "Nu avem fonduri suficiente:(");
      }
    }
    else if (strstr(mesaj, "listare produs : ") == mesaj) /// listare produs
    {
      if (logged_in == 0)
        strcpy(raspuns, "Nu esti logat.");
      else
      {
        int i = 17, j = 0;
        while (mesaj[i] != ' ')
        {
          nume[j] = mesaj[i];
          i++;
          j++;
        }
        nume[j] = '\0';
        j = 0;
        i++;
        while (mesaj[i] != '\0')
        {
          suma[j] = mesaj[i];
          i++;
          j++;
        }
        suma[j] = '\0';
        for (j = 0; j < strlen(suma); j++)
        {
          if (!(suma[j] >= '0' && suma[j] <= '9'))
          {
            break;
          }
        }
        if (j < strlen(suma))
          strcpy(raspuns, "Comanda introdusa este invalida");
        else
        {
          int operatie = 0;
          if (add_product(db, username_logged, nume, suma))
            strcpy(raspuns, "Produsul a fost adaugat.");
          else
            strcpy(raspuns, "S-a produs o eroare.");
        }
      }
    }
    else if (strstr(mesaj, "cautare produs : ") == mesaj) /// cautare produs
    {
      if (logged_in == 0)
      {
        strcpy(raspuns, "Nein nu esti logat.");
      }
      else
      {
        int i = 17, j = 0;
        while (mesaj[i] != '\0')
        {
          nume[j] = mesaj[i];
          i++;
          j++;
        }
        nume[j] = '\0';
        printf("Obiectul pe care-l cautam este %s\n", nume);
        char *temp = search_product(db, nume);
        strcpy(raspuns, temp);
      }
    }
    else
    {
      strcpy(raspuns, "Comanda introdusa este invalida.");
    }

    // Send response back to client
    if (write(tdL.cl, raspuns, sizeof(raspuns)) <= 0)
    {
      printf("[Thread %d] ", tdL.idThread);
      perror("[Thread] Eroare la write() catre client.\n");
      break;
    }
    else
    {
      printf("[Thread %d] Mesajul a fost trasmis cu succes.\n", tdL.idThread);
    }
  }
  close(tdL.cl); // Close the connection after communication ends
}







































































int decl_baza_de_date()
{
  int rc;             
  char name[50];      
  double price;       
  int seller_id;      
  sqlite3_stmt *stmt; 

  rc = sqlite3_open("users.db", &db);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Nu se deschide baza de date: %s\n", sqlite3_errmsg(db));
    return 1;
  }

  const char *create_users_table_sql =
      "CREATE TABLE IF NOT EXISTS Users ("
      "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
      "Username TEXT NOT NULL, "
      "Password TEXT NOT NULL, "
      "Sold REAL NOT NULL DEFAULT 0.0);";

  rc = sqlite3_exec(db, create_users_table_sql, 0, 0, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la crearea tabelului Users: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  printf("Tabelul Users a fost creat sau exista deja.\n");

  const char *create_products_table_sql =
      "CREATE TABLE IF NOT EXISTS Produse ("
      "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
      "Name TEXT NOT NULL, "
      "Price REAL NOT NULL, "
      "SellerID INTEGER NOT NULL, "
      "FOREIGN KEY (SellerID) REFERENCES Users(ID) ON DELETE CASCADE"
      ");";
  rc = sqlite3_exec(db, create_products_table_sql, 0, 0, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la crearea tabelului Produse: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  printf("Tabelul Produse a fost creat sau exista deja.\n");

  return 0;
}

int check_login(sqlite3 *db, const char *username, const char *password)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT ID FROM Users WHERE Username = ? AND Password = ?;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    printf("User's ID: %d\n", sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    return 1;
  }
  else
  {
    printf("Parola sau username gresit.\n");
    sqlite3_finalize(stmt);
    return 0;
  }
}

int add_user(sqlite3 *db, const char *username, const char *password)
{
  sqlite3_stmt *stmt;                                                                  
  const char *sql = "INSERT INTO Users (Username, Password, Sold) VALUES (?, ?, 0.0);";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE)
  {
    fprintf(stderr, "Eroare la creare user: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

int get_sold_value_as_string(sqlite3 *db, const char *username, char *sold_str)
{
  const char *sql = "SELECT Sold FROM Users WHERE Username = ?;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0; 
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  int result = sqlite3_step(stmt);
  if (result == SQLITE_ROW)
  {
    double sold = sqlite3_column_double(stmt, 0);
    sprintf(sold_str, "%.2f", sold);
    sqlite3_finalize(stmt);
    return 1;
  }
  else
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  return 0;
}

int update_sold_with_password(sqlite3 *db, const char *username, const char *password, int operatie, const char *suma)
{
  const char *select_sql = "SELECT Sold FROM Users WHERE Username = ? AND Password = ?;";
  const char *update_sql = "UPDATE Users SET Sold = ? WHERE Username = ?;";
  sqlite3_stmt *stmt;
  double current_sold;
  double amount;
  double new_sold;

  amount = atoi(suma);

  if (sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    current_sold = sqlite3_column_double(stmt, 0);
  }
  else
  {
    printf("Username sau parola gresita.\n");
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  if (operatie == 1)
  {
    new_sold = current_sold + amount;
  }
  else
  {
    if (current_sold < amount)
    {
      printf("Soldul actual este mai mic decat suma pe care dorim sa o adaugam.\n");
      return 0;
    }
    new_sold = current_sold - amount;
  }

  if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_double(stmt, 1, new_sold);            
  sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);

  printf("Valoarea soldului utilizatorului '%s' este %.2f\n", username, new_sold);
  return 1;
}

char *get_all_products(sqlite3 *db)
{
  const char *query =
      "SELECT Produse.ID, Produse.Name, Produse.Price, Users.Username "
      "FROM Produse "
      "INNER JOIN Users ON Produse.SellerID = Users.ID;";
  sqlite3_stmt *stmt;
  static char result[1000] = "";
  char temp[256];

  if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  strcpy(result, "");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int id = sqlite3_column_int(stmt, 0);                       
    const unsigned char *name = sqlite3_column_text(stmt, 1);   
    double price = sqlite3_column_double(stmt, 2);              
    const unsigned char *seller = sqlite3_column_text(stmt, 3); 

    snprintf(temp, sizeof(temp), "\nID: %d, Nume: %s, Pret: %.2f, Vanzator: %s",
             id, name, price, seller);
    strcat(result, temp);
  }

  sqlite3_finalize(stmt);
  return result;
}

char *get_user_products(sqlite3 *db, const char *username)
{
  const char *query =
      "SELECT Produse.ID, Produse.Name, Produse.Price "
      "FROM Produse "
      "INNER JOIN Users ON Produse.SellerID = Users.ID "
      "WHERE Users.Username = ?;";
  sqlite3_stmt *stmt;
  static char result[1000] = "";
  char temp[256];      

  if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  strcpy(result, "");
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int id = sqlite3_column_int(stmt, 0);
    const unsigned char *name = sqlite3_column_text(stmt, 1);
    double price = sqlite3_column_double(stmt, 2);

    snprintf(temp, sizeof(temp), "\nID: %d, Nume produs: %s, Pret: %.2f", id, name, price);
    strcat(result, temp);
  }

  sqlite3_finalize(stmt);
  return result;
}

int add_product(sqlite3 *db, const char *username, const char *product_name, const char *price_str)
{
  const char *get_seller_id_query = "SELECT ID FROM Users WHERE Username = ?;";
  const char *insert_product_query = "INSERT INTO Produse (Name, Price, SellerID) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int seller_id = -1;
  double price = atof(price_str);

  if (sqlite3_prepare_v2(db, get_seller_id_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    seller_id = sqlite3_column_int(stmt, 0);
  }

  sqlite3_finalize(stmt);

  if (sqlite3_prepare_v2(db, insert_product_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, product_name, -1, SQLITE_STATIC);
  sqlite3_bind_double(stmt, 2, price);
  sqlite3_bind_int(stmt, 3, seller_id);

  if (sqlite3_step(stmt) != SQLITE_DONE)
  {
    fprintf(stderr, "Eroare la adaugarea produsului: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  printf("Produsul '%s' a fost adaugat cu pretul %.2f de catre '%s'.\n", product_name, price, username);
  return 1;
}

char *search_product(sqlite3 *db, const char *product_name)
{
  const char *query =
      "SELECT Produse.ID, Produse.Name, Produse.Price, Users.Username "
      "FROM Produse "
      "INNER JOIN Users ON Produse.SellerID = Users.ID "
      "WHERE LOWER(Produse.Name) = LOWER(?);";
  sqlite3_stmt *stmt;
  char temp[256];
  static char result[1000] = "";
  strcpy(result, "");
  if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, product_name, -1, SQLITE_STATIC);
  int verificare_de_smeker = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    verificare_de_smeker++;
    int product_id = sqlite3_column_int(stmt, 0);                    
    const unsigned char *name = sqlite3_column_text(stmt, 1);        
    double price = sqlite3_column_double(stmt, 2);                   
    const unsigned char *seller_name = sqlite3_column_text(stmt, 3);

    snprintf(temp, sizeof(temp), "\nID: %d, Nume produs: %s, Pret: %.2f, Nume vanzator: %s", product_id, name, price, seller_name);
    strcat(result, temp);
  }
  if (verificare_de_smeker == 0)
  {
    strcpy(result, "Acest produs nu exista.");
  }
  else if (verificare_de_smeker == 1)
  {
    char result1[1000] = "";
    strcpy(result1, result);
    strcpy(result, "Produsul cautat este:");
    strcat(result, result1);
  }
  else
  {
    char result1[1000] = "";
    strcpy(result1, result);
    strcpy(result, "Produsele cautate sunt:");
    strcat(result, result1);
  }

  sqlite3_finalize(stmt);
  return result;
}

int acquire_product(sqlite3 *db, const char *product_id_str, const char *username)
{
  sqlite3_stmt *stmt;
  int product_id = atoi(product_id_str);
  int buyer_id = -1;
  double product_price = 0.0;
  int seller_id = -1;
  char *err_msg = NULL;

  const char *product_query = "SELECT Price, SellerID FROM Produse WHERE ID = ?;";
  const char *get_buyer_id_query = "SELECT ID FROM Users WHERE Username = ?;";

  if (sqlite3_prepare_v2(db, product_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_int(stmt, 1, product_id);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    product_price = sqlite3_column_double(stmt, 0);
    seller_id = sqlite3_column_int(stmt, 1);
    printf("Avem pretul %.2f si id-ul %d\n", product_price, seller_id);
  }
  else
  {
    printf("Produsul nu exista!\n");
    sqlite3_finalize(stmt);
    return 0;
  }

  // Step 3
  if (sqlite3_prepare_v2(db, get_buyer_id_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    buyer_id = sqlite3_column_int(stmt, 0);
  }
  else
  {
    fprintf(stderr, "User-ul '%s' nu exista.\n", username);
    sqlite3_finalize(stmt);
    return 0;
  }
  if(get_by_id(db, buyer_id, product_price, 0) == 0)
    {
      printf("Fonduri insuficiente.\n");
      return 2;
    }
  get_by_id(db, seller_id, product_price, 1);

  const char *delete_query =
      "DELETE FROM Produse WHERE ID = ?;";

  if (sqlite3_prepare_v2(db, delete_query, -1, &stmt, NULL) != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_int(stmt, 1, product_id);

  if (sqlite3_step(stmt) != SQLITE_DONE)
  {
    fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);

  printf("E bun! Bine baaa dormim diseara!!!!\n");
  return 1;
}



int get_by_id(sqlite3 *db, int user_id, double valoare, int operatie) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT Username, Password FROM Users WHERE ID = ?;";

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Eroare la interogare: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *username = (const char *)sqlite3_column_text(stmt, 0);
      const char *password = (const char *)sqlite3_column_text(stmt, 1);

      char aux[50] = "";
      sprintf(aux, "%.2f", valoare);
      if(update_sold_with_password(db, username, password, operatie, aux))
      {
        printf("Soldurile au fost schimbate\n");
      }
      else
      {
        return 0;
        printf("Nu e bun guys");
      }

    } else {
        printf("Userul cu ID-ul %d nu a fost gasit.\n", user_id);
    }

    sqlite3_finalize(stmt);
    return 1;
}