#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define SIZE 512

char *multi_tok(char *input, char *delimiter)
{
    static char *string;
    if (input != NULL)
        string = input;

    if (string == NULL)
        return string;

    char *end = strstr(string, delimiter);
    if (end == NULL)
    {
        char *temp = string;
        string = NULL;
        return temp;
    }

    char *temp = string;

    *end = '\0';
    string = end + strlen(delimiter);
    return temp;
}

struct command_msg
{
    long mtype;
    char commands[SIZE];
    int response_id;
};

struct response_msg
{
    long mtype;
    char response[2048];
};

char *append_to_str(char *str, char c)
{
    char *added = (char *)malloc(sizeof(char) * (strlen(str) + 1));
    int i = 0;
    for (i = 0; i < strlen(str); i++)
    {
        added[i] = str[i];
    }
    added[strlen(str)] = c;
    added[strlen(str) + 1] = '\0';
    return &(added[0]);
}

char *search_config(char *key)
{
    int fd;
    fd = open("config.xd", O_RDONLY);
    if (fd == -1)
    {
        printf("Plik konfiguracyjny nie istnieje\n");
        return "nie istnieje";
    }
    char buf[20];
    char *line = "";
    char *substr_ptr;
    char *temp = "";
    int i, len;
    while (read(fd, buf, 1) > 0)
    {
        if (buf[0] != '\n')
        {
            line = append_to_str(line, buf[0]);
        }
        else
        {
            substr_ptr = strstr(line, key);
            if (substr_ptr != NULL)
            {
                len = strlen(line);
                // printf("%s %d",line,len);
                for (i = 1; i < len; i++)
                {
                    if (line[i] == ' ')
                    {
                        break;
                    }
                }
                i++;
                while (i < len)
                {
                    temp = append_to_str(temp, line[i]);
                    i++;
                }
                return temp;
            }
            line = "";
        }
    }
    printf("Nie znaleziono podanego klucza w pliku konfiguracyjnym\n");
    return 0;
}

int read_configuration()
{
    return 0;
}

int handle(int receive_q)
{
    struct command_msg command;
    printf("Nasluchiwanie komend do wykonania na kolejce komunikatow %d\n", receive_q);
    while (1)
    {

        msgrcv(receive_q, &command, sizeof(command), 1, 0);
        printf("Otrzymano polecenie %s\n", command.commands);

        FILE *com = popen(command.commands, "r");
        if (com == NULL)
        {
            fprintf(stderr, "popen(3) error");
            exit(EXIT_FAILURE);
        }

        static char buff[SIZE];
        size_t n;

        while ((n = fread(buff, 1, sizeof(buff) - 1, com)) > 0)
        {
            buff[n] = '\0';
        }

        if (pclose(com) < 0)
            perror("pclose(3) error");

        struct response_msg response = {
            .mtype = 1, .response = "xd"};

        strncpy(response.response, buff, sizeof(response.response));
        msgsnd(command.response_id, &response, sizeof(response), 0);
    }
    printf("Zamykanie kolejki: %d\n", receive_q);
    msgctl(receive_q, IPC_RMID, NULL);

    return 0;
}

int client(int receive_q, pid_t handler_pid)
{
    printf("Oczekiwanie na polecenia\n");
    // char input[SIZE];

    char r_id[SIZE];
    char commands[SIZE];
    char s_id[SIZE];
    char split[SIZE];
    int length;
    struct response_msg response;
    char *input = NULL;
    size_t line_buf_size = 0;
    int characters_read;
    while (1)
    {
        printf("> ");
        characters_read = getline(&input, &line_buf_size, stdin);
        if (characters_read < 1)
        {
            printf("Doszlo do bledu podczas pobierania polecenia\n");
            continue;
        }

        int len = strlen(input);
        if (input[len - 1] == '\n')
        {
            input[len - 1] = '\0';
        }

        if (strcmp(input, "exit") == 0)
        {
            printf("Zakonczenie dzialania programu.\n");
            kill(handler_pid, SIGKILL);
            msgctl(receive_q, IPC_RMID, NULL);
            exit(0);
        }

        strcpy(split, input);
        char *token = multi_tok(split, " || ");
        int i = 0;
        while (token != NULL)
        {

            if (i == 0)
            {
                strcpy(r_id, token);
            }
            else if (i == 1)
            {
                strcpy(commands, token);
            }
            else if (i == 2)
            {
                strcpy(s_id, token);
            }
            else
            {
                printf("Powstala za duza ilosc tokenow\n");
                printf("Polecenie powinno zawierac 3 czlony odzielone sekwencja znakow ' || '\n");
            }

            // printf("%s,",token);
            token = multi_tok(NULL, " || ");
            i++;
        }

        char *config_id_txt = search_config(r_id);
        if (config_id_txt == 0)
        {
            printf("Nie znalezionow odpowiedniego ID w pliku konfiguracyjnym");
            continue;
        }

        int client_q_id = atoi(config_id_txt);
        int client_q = msgget(client_q_id, 0666);
        if (client_q == -1)
        {
            printf("Blad przy otwieraniu kolejki komunikatow: %d\n", client_q_id);
            continue;
        }

        int send_id_int = atoi(s_id);

        if (client_q_id == send_id_int)
        {
            printf("ID kolejki komunikatow odpowiedzi pokrywa sie z ID kolejki pokrywa sie z ID kolejki wysylajacej polecnie\n");
            continue;
        }

        int response_q = msgget(send_id_int, 0666 | IPC_CREAT | IPC_EXCL);
        if (response_q == -1)
        {
            printf("Blad przy tworzeniu kolejki komunikatow\n");
            continue;
        }

        struct command_msg command_struct = {
            .mtype = 1,
            .response_id = response_q};

        strncpy(command_struct.commands, commands, sizeof(command_struct.commands));

        printf("Wysylanie komendy %s uzywajac z kolejki komunikatow %d\n", commands, client_q);
        msgsnd(client_q, &command_struct, sizeof(command_struct), 0);
        msgrcv(response_q, &response, sizeof(response), 1, 0);

        printf("Otrzymano odpowiedz:\n%s\n", response.response);
        msgctl(response_q, IPC_RMID, NULL);
    }
    return 0;
}

int main(int argc, char const *argv[])
{
    if (argc < 2)
    {
        printf("Niewystarczajaca ilosc argumentow\n");
        return 1;
    }

    char *config_id_txt = search_config((char *)argv[1]);
    if (config_id_txt == 0)
    {
        printf("Nie znaleziono konfiguracji\n");
        return 1;
    }

    printf("ID konfiguracji dla %s: %s\n", argv[1], config_id_txt);

    key_t config_id = atoi(config_id_txt);
    printf("Tworzenie kolejki komunikatow pod id: %d\n", config_id);

    int msgid = msgget(config_id, 0666 | IPC_CREAT | IPC_EXCL);
    if (msgid == -1)
    {
        printf("Blad przy tworzeniu kolejki komunikatow\n");
        return 1;
    }

    printf("Stworzono kolejke komunikatow o id: %d", msgid);

    pid_t child_pid = fork();
    if (child_pid != 0)
    {
        client(msgid, child_pid);
    }
    else
    {
        handle(msgid);
    }

    return 0;
}