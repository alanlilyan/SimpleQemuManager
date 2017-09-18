#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#define LIBVIRTD_SOCKET "/home/alan/libvirt/libvirtd.socket"

#define ERR_EXIT(m) \
do \
{ \
    perror(m); \
    exit(EXIT_FAILURE); \
} \
while (0); \

static int client_sockfd;

typedef enum MESSAGE_TYPE {
    MES_QUREY_QEMU,
    MES_LAUNCH_QEMU,
    MES_KILL_QEMU,
    MES_GET_CPU_AFFINITY,
    MES_ACK,
} MESSAGE_TYPE_T;

static void print_intro()
{
    printf( "Only support these functions:\n"
            "\ta- launch a qemu\n"
            "\tb- kill a qemu\n"
            "\tc- query qemu status\n"
            "\td- get vm cpu affinity\n"
            "Please follow the tips and type correct choice.\n\n");
}

static void print_message_option()
{
    printf( "========== Options ===========\n"
            "|    l.query qemu status     |\n"
            "|    s.launch qemu           |\n"
            "|    k.kill qemu             |\n"
            "|    c.get vm cpu affinity   |\n"
            "|    h.print options         |\n"
            "|    q.quit                  |\n"
            "========== Options ===========\n\n\n");
}

static void recv_ack(void)
{
    int ack, ret;
    ret = read(client_sockfd, &ack, sizeof(ack));
    if (ret == -1) {
        ERR_EXIT("recv ack error");
    }

    if (ack != MES_ACK) {
        ERR_EXIT("Message ACK error");
    }
}

static void send_message(int mes_type)
{
    int ret;
    ret = write(client_sockfd, &mes_type, sizeof(mes_type));
    if (ret == -1) {
        ERR_EXIT("send_message error");
    }

    recv_ack();
    // printf("send_message successfully.\n\n");
}

static void handle_query_qemu(void)
{
    int lenth, ret;
    char query_buf[1024];

    /* printf("query qemu\n"); */
    ret = read(client_sockfd, &lenth, sizeof(lenth));
    if (ret == -1) {
        ERR_EXIT("recv ack error");
    }

    if (lenth > 1024) {
        lenth = 1024;
    }

    memset(query_buf, 0, 1024);
    ret = read(client_sockfd, query_buf, lenth);
    if (ret == -1) {
        ERR_EXIT("recv ack error");
    }

    printf("%s\n", query_buf);
}

static int get_vm_id(void)
{
    int num;

    printf("Enter vm_id: ");
    fscanf(stdin, "%d", &num);
    while(fgetc(stdin) != '\n');
    return num;
}

static void handle_launch_qemu(void)
{
    int ret;
    int num = get_vm_id();
    ret = write(client_sockfd, &num, sizeof(num));
    if (ret == -1) {
        ERR_EXIT("send_message error");
    }

    recv_ack();
}


static void handle_kill_qemu(void)
{
    int ret;
    int num = get_vm_id();
    if (num == -1) {
        return;
    }

    ret = write(client_sockfd, &num, sizeof(num));
    if (ret == -1) {
        ERR_EXIT("send_message error");
    }

    recv_ack();
}

static void handle_get_cpu_affinity(void)
{
    int lenth, ret;
    char cpu_buf[1024];

    int num = get_vm_id();
    if (num == -1) {
        return;
    }

    ret = write(client_sockfd, &num, sizeof(num));
    if (ret == -1) {
        ERR_EXIT("send_message error");
    }

    recv_ack();

    ret = read(client_sockfd, &lenth, sizeof(lenth));
    if (ret == -1) {
        ERR_EXIT("recv ack error");
    }

    if (lenth > 1024) {
        lenth = 1024;
    }

    memset(cpu_buf, 0, 1024);
    ret = read(client_sockfd, cpu_buf, lenth);
    if (ret == -1) {
        ERR_EXIT("recv ack error");
    }

    printf("%s\n", cpu_buf);
}

static void loop_event()
{
    char ch;

    print_intro();
    print_message_option();
    while (1) {
        printf("Enter Option [l/s/k/c/h/q]: ");

        ch = fgetc(stdin);
        /* discard all rest characters until the '\n' (include) */
        while(fgetc(stdin) != '\n');

        switch (ch) {
            case 'l':
                send_message(MES_QUREY_QEMU);
                printf("--->> query qemu status\n");
                handle_query_qemu();
                continue;
            case 's':
                send_message(MES_LAUNCH_QEMU);
                printf("--->> launch qemu with vm id\n");
                handle_launch_qemu();
                continue;
            case 'k':
                send_message(MES_KILL_QEMU);
                printf("--->> kill qemu with vm id\n");
                handle_kill_qemu();
                continue;
            case 'c':
                send_message(MES_GET_CPU_AFFINITY);
                printf("--->> get cpu affinity with vm id\n");
                handle_get_cpu_affinity();
                continue;
            case 'h':
                print_message_option();
                continue;
            case 'q':
                printf("Exit now.\n");
                exit(EXIT_SUCCESS);
                break;
            default:
                printf("Uncorrect input,try again\n");
                continue;
        }
    }
}

static void init_socket()
{
    struct sockaddr_un server_addr;
    int ret;
    int lenth;

    client_sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (client_sockfd == -1) {
        ERR_EXIT("socket error\n");
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, LIBVIRTD_SOCKET);

    lenth = strlen(server_addr.sun_path) + sizeof(server_addr.sun_family);
    ret = connect(client_sockfd, (struct sockaddr *)&server_addr, lenth);
    if (ret == -1) {
       ERR_EXIT("connect error");
    }

    printf("connected, please follow hint\n");
}

int main(int argc, char *argv[])
{
    init_socket();

    loop_event();

    return 0;
}
