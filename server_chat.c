/*
 ** server.c -- a stream socket server demo
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

#define MAXDATASIZE 2000
#define BACKLOG 10          // how many pending connections queue will hold

// Variáveis Globais
int pipe_fd[2]; //File descriptor for creating a pipe
int max_fd;
fd_set master_fd_set;
struct reg_lista *lista_clients;

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
        printf("Lista dos clientes esta vazia!\n");
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
        printf("Nao encontrado na lista dos clientes!\n");
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
        printf("Lista dos clientes esta vazia!\n");
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
        printf("Nao encontrado na lista dos clientes!\n");
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

    printf("thread argumento: %s\n", thread_argv);

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
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections...\n");



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
        printf("server: got connection from %s\n", ip_string);


        //recebe client_name
        if ((numbytes = recv(client_sock_fd, buf_recv, MAXDATASIZE - 1, 0)) == -1) {

            perror("ERRO (recv)");
            //fechar socket

        } else if (numbytes == 0) {

            printf("ERRO (recv): Cliente fechou conexão\n");
            //fechar socket

        } else {

            printf("Nome Cliente: %s\n", buf_recv);

            client_struct = search_client_name(lista_clients, buf_recv);

            if (client_struct == NULL) { //lista vazia ou nome não existe

                ret_val = add_client(lista_clients, client_sock_fd, buf_recv);
                if (ret_val == 0) {
                    printf("Client adicionado na lista com sucesso\n");

                    strcpy(buf_send, "Conectado com sucesso");

                    printf("enviado pro client: %s\n", buf_send);

                    string_length = strlen(buf_send);

                    bytes_sent = send(client_sock_fd, buf_send, string_length, 0);

                    if (bytes_sent == -1) {
                        perror("send");
                    }

                    printf("%d bytes enviados\n", bytes_sent);


                    FD_SET(client_sock_fd, &master_fd_set);

                    if (client_sock_fd > max_fd) { // keep track of the max
                        max_fd = client_sock_fd;
                    }

                    printf("thread T1 - new_sock_fd = %d\n", client_sock_fd);
                    printf("thread T1 - max_fd = %d\n", max_fd);

                    ret_val = write(pipe_fd[1], "NEW_CONNECTION_ACCEPTED", 23);
                    if (ret_val != 23) {
                        printf("thread T1 - erro pipe - ret_val = %d\n", ret_val);
                        perror("pipe write");
                        exit(2);
                    }

                    printf("thread T1 - pipe write ok - ret_val = %d\n", ret_val);


                } else {
                    printf("Erro na adição da lista\n");
                    close(client_sock_fd); // parent doesn't need this

                }


            } else { //nome já existe na lista

                printf("Client já existe na lista\n");

                strcpy(buf_send, "ERRO: CLIENT_NAME já está em uso");

                printf("enviado pro client: %s\n", buf_send);

                string_length = strlen(buf_send);

                bytes_sent = send(client_sock_fd, buf_send, string_length, 0);

                if (bytes_sent == -1) {
                    perror("send");
                }

                printf("%d bytes enviados\n", bytes_sent);

                close(client_sock_fd); // parent doesn't need this

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

    char comando[7];
    char pipe_message[24];
    char buf_recv[MAXDATASIZE];
    char buf_send[MAXDATASIZE];
    char buf_message[MAXDATASIZE];
    char buf_message_sendto[MAXDATASIZE];
    char buf_client_name[MAXDATASIZE];

    struct reg_client *client_struct;

    fd_set read_fd_set;

    while (1) { // main accept() loop

        FD_ZERO(&read_fd_set);
        read_fd_set = master_fd_set;

        printf("thread_t2 - pipe_fd[0] = %d\n", pipe_fd[0]);
        printf("thread_t2 - max_fd = %d\n", max_fd);

        ret_val = select(max_fd + 1, &read_fd_set, NULL, NULL, NULL);

        printf("thread_t2 - select ret_val = %d\n", ret_val);

        if (ret_val == -1) {

            perror("ERRO (select)"); // error occurred in select()

        } else if (ret_val == 0) {

            printf("ERRO: Timeout ocorreu\n");\
            
        } else {

            for (i = 0; i <= max_fd; i++) {

                printf("for do select %d\n", i);

                if (FD_ISSET(i, &read_fd_set)) { // we got one!!

                    if (i != pipe_fd[0]) { //mensagem em algum socket

                        numbytes = recv(i, buf_recv, MAXDATASIZE - 1, 0);

                        if (numbytes <= 0) { //algo errado aconteceu

                            if (numbytes == 0) {

                                printf("ERRO (recv): Client fechou conexão (socket %d)\n", i);

                            } else {

                                perror("ERRO (recv)");

                            }

                            close(i); // fecha conexão

                            FD_CLR(i, &master_fd_set); // remove from master set                            

                            client_struct = search_client_sockfd(lista_clients, i);

                            if (client_struct != NULL) { //client encontrado. remover da lista

                                ret_val = remove_client(lista_clients, client_struct);

                                if (ret_val == 0) {
                                    printf("Client excluido com sucesso!\n");
                                } else {
                                    printf("Falha na exclusao do client!\n");
                                }


                            } else { //problema: lista vazia ou não encontrado

                                printf("ERRO: Socket file descriptor não encontrado na lista\n");

                            }

                        } else { // recebeu mensagem de cliente

                            printf("Mensagem client: %s\n", buf_recv);

                            // identificação do comando para executar
                            j = 0;
                            while (buf_recv[j] != ' ') {

                                comando[j] = toupper(buf_recv[j]);
                                j++;

                            }

                            printf("comando: '%s'\n", comando);

                            strcpy(buf_message, &buf_recv[j + 1]);

                            printf("message: '%s'\n", buf_message);

                            if (strcmp(comando, "SEND") == 0) {

                                //busca nome do cliente que mandou comando
                                client_struct = search_client_sockfd(lista_clients, i);

                                if (client_struct != NULL) { //client encontrado. pega nome dele

                                    sprintf(buf_send, "%s: %s", client_struct->name, buf_message);

                                    printf("buf_send: '%s'\n", buf_send);

                                    string_length = strlen(buf_send);

                                    // we got some data from a client
                                    for (k = 0; k <= max_fd; k++) {
                                        // send to everyone!
                                        if (FD_ISSET(k, &master_fd_set)) {
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


                            } else if (strcmp(comando, "SENDTO") == 0) {

                                //retirar client_name do buf_message
                                j = 0;
                                while (buf_message[j] != ' ') {

                                    buf_client_name[j] = buf_message[j];
                                    j++;

                                }

                                printf("sendto client_name: '%s'\n", buf_client_name);

                                strcpy(buf_message_sendto, &buf_message[j + 1]);

                                printf("sendto message: '%s'\n", buf_message_sendto);

                                printf("executar comando %s\n", comando);

                                //busca o client de destino pelo nome
                                client_struct = search_client_name(lista_clients, buf_client_name);

                                if (client_struct != NULL) { //client de destino encontrado

                                    //salva sockfd do client de destino para enviar mensagem                                    
                                    sendto_sockfd = client_struct->sock_fd;

                                    //busca client de origem pelo sockfd para pegar nome dele
                                    client_struct = search_client_sockfd(lista_clients, i);

                                    if (client_struct != NULL) {


                                        //monta o buf_send para envio
                                        sprintf(buf_send, "%s: %s", client_struct->name, buf_message_sendto);

                                        printf("1\n");

                                        string_length = strlen(buf_send);

                                        printf("4\n");

                                        bytes_sent = send(sendto_sockfd, buf_send, string_length, 0);

                                        if (bytes_sent == -1) {
                                            perror("send");
                                        }

                                        printf("buf_send: '%s'\n", buf_send);


                                    } else {//erro: lista vazia ou não encontrado

                                        printf("ERRO: Socket file descriptor não encontrado na lista\n");

                                    }



                                } else { //client não está na lista dos conectados. retorna menssagem de erro

                                    printf("5\n");

                                    memset(buf_send, '\0', sizeof buf_send);

                                    printf("buf_send: '%s'\n", buf_send);

                                    printf("6\n");

                                    sprintf(buf_send, "ERRO: Client %s não está conectado", buf_client_name);

                                    printf("buf_send: '%s'\n", buf_send);

                                    printf("7\n");

                                    string_length = strlen(buf_send);

                                    printf("buf_send length: '%d'\n", string_length);

                                    printf("8\n");

                                    bytes_sent = send(i, buf_send, string_length, 0);

                                    printf("9\n");

                                    if (bytes_sent == -1) {
                                        perror("send");
                                    }

                                    printf("buf_send: '%s'\n", buf_send);

                                }

                            } else if (strcmp(comando, "WHO") == 0) {

                                strcpy(buf_send, "======== CLIENTS CONECTADOS ========");

                                string_length = strlen(buf_send);

                                bytes_sent = send(i, buf_send, string_length, 0);

                                if (bytes_sent == -1) {
                                    perror("send");
                                }

                                printf("buf_send %s\n", buf_send);
                                printf("%d bytes enviados\n", bytes_sent);

                                client_struct = lista_clients->inicio;

                                while (client_struct != NULL) {

                                    memset(buf_send, '\0', sizeof buf_send);

                                    sprintf(buf_send, "%s", client_struct->name);

                                    string_length = strlen(buf_send);

                                    bytes_sent = send(i, buf_send, string_length, 0);

                                    if (bytes_sent == -1) {
                                        perror("send");
                                    }

                                    printf("buf_send: '%s'\n", buf_send);

                                    client_struct = client_struct->posterior;

                                }



                            } else { // erro: comando não suportado

                                printf("ERRO: Comando não suportado\n");
                                printf("Comando HELP para lista de comandos suportados\n");

                            }

                            memset(comando, '\0', sizeof comando);
                            printf("comando memset: '%s'\n", comando);

                            memset(buf_message, '\0', sizeof buf_message);
                            printf("buf_message memset: '%s'\n", buf_message);

                            memset(buf_recv, '\0', sizeof buf_recv);
                            printf("buf_recv memset: '%s'\n", buf_recv);

                        }

                        //memset(buf_recv, '\0', sizeof buf_recv);

                    } else { //mensagem no pipe

                        ret_val = read(pipe_fd[0], pipe_message, 23);
                        if (ret_val != 23) {
                            perror("read");
                            exit(3);
                        }

                        printf("thread_t2 - Pipe: %s\n", pipe_message);
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
    printf("main - pipe_fd[0] = %d\n", pipe_fd[0]);
    printf("main - max_fd = %d\n", max_fd);

    pthread_create(&id_t1_t, NULL, thread_t1, argv[1]);
    pthread_create(&id_t2_t, NULL, thread_t2, NULL);

    pthread_join(id_t1_t, NULL);
    pthread_join(id_t2_t, NULL);

}