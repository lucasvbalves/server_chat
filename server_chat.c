/*
 ** server_chat.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#define MAXDATASIZE 2000
#define BACKLOG 10          // how many pending connections queue will hold

// Variáveis Globais
int pipe_fd[2]; //File descriptor for creating a pipe
int max_fd;
struct reg_lista *lista_clients;
fd_set master_fd_set;
pthread_mutex_t lock;

struct reg_client {
    int sock_fd;
    char name[100];

    struct reg_client *anterior;
    struct reg_client *posterior;
};

struct reg_lista {
    int num_elem;
    struct reg_client *inicio;
    struct reg_client *fim;
};

/*======== FUNCAO PARA CRIACAO E INICIALIZACAO DE UMA NOVA LISTA ===================*/

struct reg_lista *cria_lista(void) {
    struct reg_lista *lista;
    lista = malloc(sizeof (struct reg_lista));
    lista->num_elem = 0;
    lista->inicio = NULL;
    lista->fim = NULL;
    return lista;
}
/*==================================================================================*/

/*=============== FUNCAO PARA A INSERCAO DE UM NOVO ELEMENTO NA LISTA ==============*/

int add_client(struct reg_lista *lista, int sock_fd, char *name) {
    struct reg_client *new_client;
    new_client = malloc(sizeof (struct reg_client));
    if (new_client == NULL) {
        return -1;
    }

    new_client->sock_fd = sock_fd;
    strcpy(new_client->name, name);

    if (lista->num_elem == 0) {
        new_client->anterior = NULL;
        new_client->posterior = NULL;
        lista->inicio = new_client;
        lista->fim = new_client;
        lista->num_elem++;
    } else {
        new_client->anterior = lista->fim;
        new_client->posterior = NULL;
        lista->fim->posterior = new_client;
        lista->fim = new_client;
        lista->num_elem++;
    }
    return 0;
}
/*==================================================================================*/

/*================= FUNCAO PARA A REMOCAO DE UM ELEMENTO DA LISTA ==================*/

int remove_client(struct reg_lista *lista, struct reg_client *client) {

    if (lista->inicio == client) { // SE O CLIENT RECEBIDO FOR O PRIMEIRO DA LISTA
        lista->inicio = lista->inicio->posterior;
        if (lista->inicio == NULL) { // SE O FILME A SER REMOVIDO FOR O ULTIMO DA LISTA
            lista->fim = NULL;
        } else {
            lista->inicio->anterior == NULL;
        }
    } else if (lista->fim == client) { // SE O CLIENT RECEBIDO FOR O ULTIMO DA LISTA
        lista->fim->anterior->posterior = NULL;
        lista->fim = lista->fim->anterior;
    } else {
        client->anterior->posterior = client->posterior;
        client->posterior->anterior = client->anterior;
    }
    free(client);
    lista->num_elem--;
    return 0;
}
/*==================================================================================*/

/*==================== FUNCAO PARA BUSCA POR NOME NUMA LISTA =======================*/

struct reg_client *search_client_name(struct reg_lista *lista, char *search_name) {
    int ret_val;
    struct reg_client *search_client;
    search_client = lista->inicio;

    if (lista->num_elem == 0) {
        search_client = NULL;
        return search_client;
    } else {
        while (search_client != NULL) {
            ret_val = strcmp(search_client->name, search_name);
            if (ret_val == 0) {
                return search_client;
            } else {
                search_client = search_client->posterior;
            }
        }
        search_client = NULL;
        return search_client;
    }
}
/*==================================================================================*/

/*=================== FUNCAO PARA BUSCA POR SOCK_FD NUMA LISTA =====================*/

struct reg_client *search_client_sockfd(struct reg_lista *lista, int search_sockfd) {
    int ret_val;
    struct reg_client *search_client;
    search_client = lista->inicio;

    if (lista->num_elem == 0) {
        search_client = NULL;
        return search_client;
    } else {
        while (search_client != NULL) {
            if (search_client->sock_fd == search_sockfd) {
                return search_client;
            } else {
                search_client = search_client->posterior;
            }
        }
        search_client = NULL;
        return search_client;
    }
}
/*==================================================================================*/


// get sockaddr, IPv4 or IPv6:

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

//T1 espera as conexões com accept()

void *thread_t1(void *thread_argv) {

    int listen_sock_fd; // listen on sock_fd, new connection on new_fd
    int client_sock_fd;
    int ret_val;
    int yes = 1;
    int numbytes;
    int string_length;
    int bytes_sent;

    struct reg_client *client_struct;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;

    char ip_string[INET6_ADDRSTRLEN];
    char buf_recv[MAXDATASIZE];
    char buf_send[MAXDATASIZE];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((ret_val = getaddrinfo(NULL, thread_argv, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret_val));
        exit(1);
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {

        listen_sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (listen_sock_fd == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof (int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(listen_sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(listen_sock_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(listen_sock_fd, BACKLOG) == -1) {
        perror("Listen");
        exit(1);
    }

    printf("Servidor: Aguardando conexões...\n");

    while (1) { // main accept() loop
        sin_size = sizeof their_addr;
        client_sock_fd = accept(listen_sock_fd, (struct sockaddr *) &their_addr, &sin_size);
        if (client_sock_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                get_in_addr((struct sockaddr *) &their_addr),
                ip_string, sizeof ip_string);
        printf("Servidor: Conexão recebida de %s\n", ip_string);


        //recebe client_name
        if ((numbytes = recv(client_sock_fd, buf_recv, MAXDATASIZE - 1, 0)) == -1) {

            perror("ERRO (recv)");
            //fechar socket

        } else if (numbytes == 0) {

            printf("ERRO (recv): Cliente fechou conexão\n");
            //fechar socket

        } else {

            pthread_mutex_lock(&lock); // protege variável global para acesso
            client_struct = search_client_name(lista_clients, buf_recv);
            pthread_mutex_unlock(&lock); // libera variável global para acesso

            if (client_struct == NULL) { //lista vazia ou nome não existe

                pthread_mutex_lock(&lock); // protege variável global para acesso
                ret_val = add_client(lista_clients, client_sock_fd, buf_recv);
                pthread_mutex_unlock(&lock); // libera variável global para acesso

                if (ret_val == 0) {

                    strcpy(buf_send, "Conectado com sucesso");

                    string_length = strlen(buf_send);

                    bytes_sent = send(client_sock_fd, buf_send, string_length, 0);

                    if (bytes_sent == -1) {
                        perror("send");
                    }

                    pthread_mutex_lock(&lock); // protege variável global para acesso

                    FD_SET(client_sock_fd, &master_fd_set);

                    if (client_sock_fd > max_fd) { // keep track of the max
                        max_fd = client_sock_fd;
                    }

                    pthread_mutex_unlock(&lock); // libera variável global para acesso

                    string_length = strlen(buf_recv);

                    ret_val = write(pipe_fd[1], buf_recv, string_length);
                    if (ret_val <= 0) {
                        perror("pipe write");
                        exit(2);
                    }

                } else {
                    printf("Erro na adição da lista\n");
                    close(client_sock_fd); // parent doesn't need this

                }


            } else { //nome já existe na lista

                strcpy(buf_send, "Falha na conexão: CLIENT_NAME já está em uso");

                string_length = strlen(buf_send);

                bytes_sent = send(client_sock_fd, buf_send, string_length, 0);

                if (bytes_sent == -1) {
                    perror("send");
                }

                close(client_sock_fd);

            }
        }

        //limpa buffer de recepção dos nomes
        memset(buf_recv, '\0', sizeof buf_recv);
    }
}


//T2 gerencia as conexões abertas usando a função select()

void *thread_t2() {
    int ret_val;
    int i;
    int j;
    int k;
    int numbytes;
    int string_length;
    int bytes_sent;
    int sendto_sockfd;
    int max_fd_cpy;

    char comando[7];
    char pipe_message[24];
    char buf_recv[MAXDATASIZE];
    char buf_send[MAXDATASIZE];
    char buf_message[MAXDATASIZE];
    char buf_message_sendto[MAXDATASIZE];
    char buf_client_name[MAXDATASIZE];

    struct reg_client *client_struct;

    struct tm *time_struct;
    time_t time_type;

    fd_set master_fd_set_cpy;
    fd_set read_fd_set;

    pthread_mutex_lock(&lock); // protege variável global para acesso
    master_fd_set_cpy = master_fd_set;
    max_fd_cpy = max_fd;
    pthread_mutex_unlock(&lock); // libera variável global para acesso

    while (1) { // main accept() loop

        FD_ZERO(&read_fd_set);
        read_fd_set = master_fd_set_cpy;

        ret_val = select(max_fd_cpy + 1, &read_fd_set, NULL, NULL, NULL);

        if (ret_val == -1) {

            perror("ERRO (select)"); // error occurred in select()

        } else if (ret_val == 0) {

            printf("ERRO: Timeout ocorreu\n");\
            
        } else { // algum file descriptor pronto para ser lido

            for (i = 0; i <= max_fd_cpy; i++) { // percorre todos os file descriptors monitorados

                memset(comando, '\0', sizeof comando);
                memset(pipe_message, '\0', sizeof pipe_message);
                memset(buf_recv, '\0', sizeof buf_recv);
                memset(buf_send, '\0', sizeof buf_send);
                memset(buf_message, '\0', sizeof buf_message);
                memset(buf_message_sendto, '\0', sizeof buf_message_sendto);
                memset(buf_client_name, '\0', sizeof buf_client_name);

                if (FD_ISSET(i, &read_fd_set)) { // file descriptor 'i' pronto para ser lido

                    if (i != pipe_fd[0]) { // 'i não é o pipe[0], então é um socket

                        numbytes = recv(i, buf_recv, MAXDATASIZE - 1, 0);

                        if (numbytes <= 0) { //algo errado aconteceu na recepção

                            if (numbytes == 0) {

                                time_type = time(NULL);
                                time_struct = localtime(&time_type);

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                client_struct = search_client_sockfd(lista_clients, i);
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                printf("%.2d:%.2d\t%s\tDesconectado\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name);


                            } else {

                                perror("ERRO (recv)");

                            }

                            close(i); // problema no socket, fecha conexão

                            FD_CLR(i, &master_fd_set_cpy); // remove from master copy set

                            pthread_mutex_lock(&lock); // protege variável global para acesso
                            FD_CLR(i, &master_fd_set); // remove socket do master_fd_set
                            client_struct = search_client_sockfd(lista_clients, i);
                            pthread_mutex_unlock(&lock); // libera variável global para acesso                            

                            if (client_struct != NULL) { //client encontrado. remover da lista

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                ret_val = remove_client(lista_clients, client_struct);
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                if (ret_val != 0) {
                                    printf("ERRO: Falha na exclusão do client da lista\n");
                                }

                            } else { //problema: lista vazia ou não encontrado

                                printf("ERRO: Socket file descriptor não encontrado na lista\n");

                            }

                        } else { // numbytes > 0. recebeu mensagem de client

                            buf_recv[numbytes] = '\0';

                            // identificação do comando para executar
                            j = 0;
                            while (buf_recv[j] != ' ' && j < 6 && buf_recv[j] != '\0') {

                                comando[j] = toupper(buf_recv[j]);
                                j++;

                            }

                            strcpy(buf_message, &buf_recv[j + 1]); //retira o comando e salva apenas a mensagem

                            if (strcmp(comando, "SEND") == 0) {

                                pthread_mutex_lock(&lock); // protege variável global para acesso

                                //busca nome do cliente que mandou comando
                                client_struct = search_client_sockfd(lista_clients, i);

                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                if (client_struct != NULL) { //client encontrado. pega nome dele

                                    sprintf(buf_send, "%s: %s", client_struct->name, buf_message);

                                    string_length = strlen(buf_send);

                                    // we got some data from a client
                                    for (k = 0; k <= max_fd_cpy; k++) {
                                        // send to everyone!
                                        if (FD_ISSET(k, &master_fd_set_cpy)) {
                                            // exceto o pipe e o client que enviou (i)
                                            if (k != pipe_fd[0] && k != i) {
                                                bytes_sent = send(k, buf_send, string_length, 0);
                                                if (bytes_sent == -1) {
                                                    perror("send");
                                                }
                                            }
                                        }
                                    }

                                } else { //problema: lista vazia ou não encontrado

                                    printf("ERRO: Socket file descriptor não encontrado na lista\n");

                                }

                                time_type = time(NULL);
                                time_struct = localtime(&time_type);

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                client_struct = search_client_sockfd(lista_clients, i);
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                printf("%.2d:%.2d\t%s\t%s\tExecutado: Sim\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name, comando);

                            } else if (strcmp(comando, "SENDTO") == 0) {

                                //retirar client_name do buf_message
                                j = 0;
                                while (buf_message[j] != ' ' && buf_message[j] != '\0') {

                                    buf_client_name[j] = buf_message[j];
                                    j++;

                                }

                                strcpy(buf_message_sendto, &buf_message[j + 1]); //retira nome do client de destino e salva mensagem

                                pthread_mutex_lock(&lock); // protege variável global para acesso

                                //busca o client de destino pelo nome
                                client_struct = search_client_name(lista_clients, buf_client_name);

                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                if (client_struct != NULL) { //client de destino encontrado

                                    //salva sockfd do client de destino para enviar mensagem                                    
                                    sendto_sockfd = client_struct->sock_fd;

                                    pthread_mutex_lock(&lock); // protege variável global para acesso

                                    //busca client de origem pelo sockfd para pegar nome dele
                                    client_struct = search_client_sockfd(lista_clients, i);

                                    pthread_mutex_unlock(&lock); // libera variável global para acesso

                                    if (client_struct != NULL) {


                                        //monta o buf_send para envio
                                        sprintf(buf_send, "%s: %s", client_struct->name, buf_message_sendto);

                                        string_length = strlen(buf_send);

                                        bytes_sent = send(sendto_sockfd, buf_send, string_length, 0);

                                        if (bytes_sent == -1) {
                                            perror("send");
                                        }


                                    } else {//erro: lista vazia ou não encontrado

                                        printf("ERRO: Socket file descriptor não encontrado na lista\n");

                                    }

                                    time_type = time(NULL);
                                    time_struct = localtime(&time_type);

                                    pthread_mutex_lock(&lock); // protege variável global para acesso
                                    client_struct = search_client_sockfd(lista_clients, i);
                                    pthread_mutex_unlock(&lock); // libera variável global para acesso

                                    printf("%.2d:%.2d\t%s\t%s\tExecutado: Sim\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name, comando);

                                } else { //client não está na lista dos conectados. retorna menssagem de erro

                                    sprintf(buf_send, "ERRO: Client '%s' não está conectado", buf_client_name);

                                    string_length = strlen(buf_send);

                                    bytes_sent = send(i, buf_send, string_length, 0);

                                    if (bytes_sent == -1) {
                                        perror("send");
                                    }

                                    time_type = time(NULL);
                                    time_struct = localtime(&time_type);

                                    pthread_mutex_lock(&lock); // protege variável global para acesso
                                    client_struct = search_client_sockfd(lista_clients, i);
                                    pthread_mutex_unlock(&lock); // libera variável global para acesso

                                    printf("%.2d:%.2d\t%s\t%s\tExecutado: Não\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name, comando);

                                }

                            } else if (strcmp(comando, "WHO") == 0) {

                                strcpy(buf_send, "===== CLIENTS CONECTADOS =====\n");

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                client_struct = lista_clients->inicio;
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                while (client_struct != NULL) {

                                    string_length = strlen(buf_send);

                                    strcpy(&buf_send[string_length], client_struct->name);

                                    string_length = strlen(buf_send);

                                    buf_send[string_length] = '\n';

                                    client_struct = client_struct->posterior;

                                }

                                string_length = strlen(buf_send);

                                strcpy(&buf_send[string_length], "==============================");

                                string_length = strlen(buf_send);

                                bytes_sent = send(i, buf_send, string_length, 0);

                                if (bytes_sent == -1) {
                                    perror("send");
                                }

                                time_type = time(NULL);
                                time_struct = localtime(&time_type);

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                client_struct = search_client_sockfd(lista_clients, i);
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                printf("%.2d:%.2d\t%s\t%s\tExecutado: Sim\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name, comando);

                            } else { // erro: comando não suportado                                

                                time_type = time(NULL);
                                time_struct = localtime(&time_type);

                                pthread_mutex_lock(&lock); // protege variável global para acesso
                                client_struct = search_client_sockfd(lista_clients, i);
                                pthread_mutex_unlock(&lock); // libera variável global para acesso

                                printf("%.2d:%.2d\t%s\t%s\tExecutado: Não\n", time_struct->tm_hour, time_struct->tm_min, client_struct->name, comando);

                            }

                        }

                    } else { //mensagem no pipe

                        ret_val = read(pipe_fd[0], pipe_message, 23);
                        if (ret_val <= 0) {
                            perror("read");
                            exit(3);
                        }

                        pthread_mutex_lock(&lock); // protege variável global para acesso
                        master_fd_set_cpy = master_fd_set;
                        max_fd_cpy = max_fd;
                        pthread_mutex_unlock(&lock); // libera variável global para acesso

                        time_type = time(NULL);
                        time_struct = localtime(&time_type);

                        printf("%.2d:%.2d\t%s\tConectado\n", time_struct->tm_hour, time_struct->tm_min, pipe_message);


                    }

                } // END got new incoming connection


            } // END looping through file descriptors  

        }


    }

}

int main(int argc, char *argv[]) {

    pthread_t id_t1_t;
    pthread_t id_t2_t;
    int ret_val;

    if (argc != 2) {
        fprintf(stderr, "Uso: server_chat <PORT>\n");
        exit(1);
    }



    lista_clients = cria_lista();

    if (lista_clients == NULL) {
        printf("ERRO: Não foi possível alocar lista de clientes");
        exit(1);
    }

    ret_val = pipe(pipe_fd);
    if (ret_val < 0) {
        perror("pipe ");
        exit(1);
    }

    FD_ZERO(&master_fd_set);
    FD_SET(pipe_fd[0], &master_fd_set);

    max_fd = pipe_fd[0];

    ret_val = pthread_mutex_init(&lock, NULL);
    if (ret_val != 0) {
        printf("\n mutex init failed\n");
        return 1;
    }

    pthread_create(&id_t1_t, NULL, thread_t1, argv[1]);
    pthread_create(&id_t2_t, NULL, thread_t2, NULL);

    pthread_join(id_t1_t, NULL);
    pthread_join(id_t2_t, NULL);

    pthread_mutex_destroy(&lock);

}