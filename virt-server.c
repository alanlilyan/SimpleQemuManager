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
#include <string.h>

#define __USE_GNU
#include <sched.h>

/* Modify this to your own environment path. */
#define LIBVIRT_LOG_FILE "/home/alan/libvirt/log/libvirtd.log"
#define LIBVIRT_PID_FILE "/home/alan/libvirt/libvirtd.pid"

/* local socket connect */
#define LIBVIRTD_SOCKET "/home/alan/libvirt/libvirtd.socket"
#define SINGLE 1 // only support single client
#define MAXCONN SINGLE

#define MAX_VM_NUM 20

#define QEMU_BIN "/usr/local/bin/qemu-system-x86_64"

#define ERR_EXIT(m, ...) \
do \
{ \
    logout(m, ##__VA_ARGS__); \
    exit(EXIT_FAILURE); \
} \
while (0); \

#define ATTR_UNUSED __attribute__((unused))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

typedef struct qemu_proc {
    int vm_id;
    // char vm_name[64];
    pid_t pid;
    // bool running;
    struct qemu_proc * next;
} qemu_proc_t;

typedef struct libvirt_server {
    int listenfd;
    int connfd;
    fd_set listen_set;
    int log_fd;
    char log_buf[1024];
    bool connected;
    qemu_proc_t *qemu_head;
} libvirt_server_t;

typedef enum MESSAGE_TYPE {
    MES_QUREY_QEMU,
    MES_LAUNCH_QEMU,
    MES_KILL_QEMU,
    MES_GET_CPU_AFFINITY,
    MES_ACK,
} MESSAGE_TYPE_T;

typedef enum OPTION_TYPE {
    OPT_NULL,
    OPT_QEMU_NUM,
    // OPT_QEMU_CPU,
    // OPT_QEMU_MEM,
} OPTION_TYPE_T;

typedef ATTR_UNUSED struct qemu_option {
    OPTION_TYPE_T type;
    uint32_t size;
    char *value;
} qemu_option_t;

static libvirt_server_t virt_server;

static void logout(char *fmt, ...);
static void create_daemon(void);
static void init_log(void);
static void init_pid_file(void);
static int new_connect(void);
static int recv_message(int * message_type);
static int recv_vm_id(void);
static qemu_proc_t * create_qemu_proc(void);
static void fill_arglist(qemu_proc_t *qemu_proc);
static void try_launch_qemu(void);
static void query_qemu(void);
static void loop_event(void);
static int server_init(void);

static char *message_str[] = {
    "Message query qemu",
    "Message launch qemu",
    "Message kill qemu",
    "Message get process cpu affinity",
};

#define INSTALL_GUEST_OS 0
static char * qemu_common_option[] = {
    "qemu-system-x86_64",  // arg[0] is the name of process
    "-enable-kvm",
    "-machine", "pc-i440fx-2.9,accel=kvm,usb=off",
    "-cpu", "host",
    "-realtime", "mlock=off",
    /* "-uuid", "1fd24501-427f-42a2-8580-4804ace5b179", */
    "-no-user-config",
    "-nodefaults",
    "-rtc", "base=localtime,driftfix=slew",
    "-no-hpet",
    "-boot", "strict=on",
    /* usb host control */
    "-device", "ich9-usb-ehci1,id=usb,bus=pci.0,addr=0x7.0x7",
    "-device", "ich9-usb-uhci1,masterbus=usb.0,firstport=0,bus=pci.0,multifunction=on,addr=0x7",
    "-device", "ich9-usb-uhci2,masterbus=usb.0,firstport=2,bus=pci.0,addr=0x7.0x1",
    "-device", "ich9-usb-uhci2,masterbus=usb.0,firstport=4,bus=pci.0,addr=0x7.0x2",
    "-device", "virtio-serial-pci,id=virtio-serial0,bus=pci.0,addr=0x5",
    /* boot device */
#if INSTALL_GUEST_OS
    // "-cdrom", ISO_FILE,
    "-drive", "file=" WIN7_ISO_FILE ",if=none,media=cdrom,id=drive-ide0-0-0,readonly=on,format=raw",
    "-device", "ide-drive,bus=ide.0,unit=0,drive=drive-ide0-0-0,id=ide0-0-0",
    "-drive", "file=" VIRTIO_ISO_FILE ",if=none,media=cdrom,id=drive-ide0-1-0,readonly=on,format=raw",
    "-device", "ide-drive,bus=ide.0,unit=1,drive=drive-ide0-1-0,id=ide0-1-0",
#endif
    /* tap net */
    "-net", "nic,model=virtio",
    "-net", "user,hostname=alan",
    "-chardev", "spicevmc,id=charchannel0,name=vdagent",
    "-device", "virtserialport,bus=virtio-serial0.0,nr=1,chardev=charchannel0,id=channel0,name=com.redhat.spice.0",
    "-k", "en-us",
    "-device", "qxl-vga,id=video0,ram_size=67108864,vram_size=67108864,vgamem_mb=16,bus=pci.0",
    "-device", "intel-hda,id=sound0,bus=pci.0,addr=0x4",
    "-device", "hda-duplex,id=sound0-codec0,bus=sound0.0,cad=0",
    "-chardev", "spicevmc,name=usbredir,id=usbredirchardev1",
    "-device", "usb-redir,chardev=usbredirchardev1,id=usbredirdev1,bus=usb.0,port=1",
    "-chardev", "spicevmc,name=usbredir,id=usbredirchardev2",
    "-device", "usb-redir,chardev=usbredirchardev2,id=usbredirdev2,bus=usb.0,port=2",
    "-chardev", "spicevmc,name=usbredir,id=usbredirchardev3",
    "-device", "usb-redir,chardev=usbredirchardev3,id=usbredirdev3,bus=usb.0,port=3",
    "-device", "virtio-balloon-pci,id=balloon0,bus=pci.0,addr=0x6",
    /* "-msg", "timestamp=on", */
};

static void logout(char *fmt, ...)
{
    int lenth;

    va_list args;

    va_start (args, fmt);
    lenth = vsprintf(virt_server.log_buf, fmt, args);
    va_end(args);

    if (lenth > 1024) {
        lenth = 1024;
        virt_server.log_buf[lenth - 1] = '\0';
    }

    if (write(virt_server.log_fd, virt_server.log_buf, lenth) == -1) {
       ERR_EXIT("Error: write to debug file error.\n");
    }
}

static ATTR_UNUSED void create_daemon(void)
{
    pid_t pid;

    pid = fork();
    if (pid == -1) {
        ERR_EXIT("Error: fork error\n");
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() == -1) {
        ERR_EXIT("Error: setsid error\n");
    }

    chdir("/");

    int i;
    for ( i = 0; i < 3; i++) {
        close(i);
        open("/dev/null", O_RDWR);
        dup(0);
        dup(0);
    }

    umask(0);

    return;
}


static void init_log(void)
{
    virt_server.log_fd = open(LIBVIRT_LOG_FILE, O_RDWR | O_CLOEXEC | O_CREAT | O_APPEND, 0660);
    if (virt_server.log_fd == -1) {
        ERR_EXIT("Error: Open libvirt log file error\n");
    }
    logout("\n\n===========   start   ==============\n");
}

static void init_pid_file()
{
    char buf[16];
    int len;
    int pid_fd = open(LIBVIRT_PID_FILE, O_RDWR | O_CLOEXEC | O_CREAT, 0660);
    pid_t pid = getpid();

    logout("libvirtd pid is %d\n", pid);

    len = sprintf(buf, "%d", pid);
    if (write(pid_fd, buf, len) == -1) {
        ERR_EXIT("Error: write pid to file failed\n");
    }
}

static int new_connect(void)
{
    int flags;
    virt_server.connfd = accept(virt_server.listenfd, NULL, NULL);
    if (virt_server.connfd == -1) {
        logout("Error: accept error\n");
        return -1;
    }
    flags = fcntl(virt_server.connfd, F_GETFD);
    flags |= FD_CLOEXEC;
    fcntl(virt_server.connfd, F_SETFD);
    logout("virt-client connect\n");
    virt_server.connected = true;

    return 0;
}

static int send_ack(void)
{
    int ack = MES_ACK;
    int ret = write(virt_server.connfd, &ack, sizeof(ack));
    if (ret == -1) {
        ERR_EXIT("Error: send ack error\n");
    }
    return 0;
}

static void do_recv_check(int ret)
{
    if (ret <= 0) {
        if (virt_server.connected) {
            close(virt_server.connfd);
            virt_server.connected = false;
            virt_server.connfd = -1;
            logout("virt-client disconnect\n");
        } else {
            logout("virt-client disconnect already.\n");
        }
    }
    /* no error */
}

static int recv_message(int * message_type)
{
    int ret;
    ret = read(virt_server.connfd, message_type, sizeof(int));

    if (ret <= 0) {
        logout("recv message error\n");
        do_recv_check(ret);
        return -1;
    }

    send_ack();

    logout("%s\n", message_str[*message_type]);
    return 0;
}

static int recv_vm_id(void)
{
    int ret, vm_id;
    ret = read(virt_server.connfd, &vm_id, sizeof(int));

    if (ret <= 0) {
        do_recv_check(ret);
        return -1;
    }

    if (vm_id < 0 || vm_id >= MAX_VM_NUM) {
        logout("vm_id range error\n");
        return -1;
    }

    send_ack();

    return vm_id;
}

#if 0
static int recv_option(qemu_option_t * opt) ATTR_UNUSED
{
    int ret;
    ret = read(virt_server.connfd, &(opt->type), sizeof(opt->type));
    if (ret == -1) {
        ERR_EXIT("Error: socket read error\n");
    }

    if (opt->type == OPT_NULL) {
        return 0;
    }

    ret = read(virt_server.connfd, &(opt->size), sizeof(opt->size));
    if (ret == -1) {
        ERR_EXIT("Error: socket read error\n");
    }

    opt->value = calloc(1, opt->size);

    return ret;
}
#endif

static qemu_proc_t* create_qemu_proc(void)
{
    qemu_proc_t ** item;

    int vm_id = recv_vm_id();
    if (vm_id == -1) {
        return NULL;
    }

    for(item = &virt_server.qemu_head; *item != NULL; item = &(*item)->next) {
        if ((*item)->vm_id == vm_id) {
            logout("qemu %d has already launched\n", vm_id);
            return NULL;
        }
    };

    *item = (qemu_proc_t *)calloc(1, sizeof(qemu_proc_t));
    if (*item == NULL) {
        logout("malloc qemu_proc error (%s)\n", strerror(errno));
    }
    (*item)->vm_id = vm_id;
    (*item)->next = NULL;

    logout("create a new qemu_proc, vm_id %d\n", vm_id);

    return *item;
}

#define NAME_OPT 2
#define MEM_OPT 2
#define SMP_OPT 2
#define IMAGE_OPT 2
#define SPICE_OPT 2
#define DEBUG_OPT 2
#define NULL_OPT 1

#define EXT_OPT_SIZE 13

static char buf[12][256];
static char **arglist;

#define BASE_PORT 9500
#define PER_CPU 2
static const uint32_t mem_defaut = 2048;
static const uint32_t smp_defaut = PER_CPU;

static void fill_arglist(qemu_proc_t * qemu_proc)
{
    int pos;
    int common_opt_size, i;
    int vm_id = qemu_proc->vm_id;
    common_opt_size = ARRAY_SIZE(qemu_common_option);

    int count = common_opt_size + EXT_OPT_SIZE;

    /* These options are just assigned once */
    if (arglist == NULL) {
        arglist = calloc(1, count * sizeof(char *));
        for (i = 0, pos = 0; i < common_opt_size; i++, pos++) {
            arglist[pos] = qemu_common_option[i];
        }

        for (i = 0; i < EXT_OPT_SIZE - 1; i++, pos++) {
            arglist[pos] = buf[i];
        }
        /* add extra NULL as the EOS of array */
        arglist[pos++] = NULL;
    }

    memset(buf, 0, sizeof(buf));
    sprintf(buf[0], "-name");
    sprintf(buf[1], "qemu-%03d", vm_id);

    sprintf(buf[2], "-m");
    sprintf(buf[3], "%u", mem_defaut);

    sprintf(buf[4], "-smp");
    sprintf(buf[5], "%u", smp_defaut);

    sprintf(buf[6], "-drive");
    sprintf(buf[7], "file=/home/alan/libvirt/images/vm-%03d,if=none,id=drive-virtio-disk0-0-0,format=qcow2,cache=none", vm_id);

    sprintf(buf[8], "-device");
    sprintf(buf[9], "virtio-blk,bus=pci.0,addr=0x8,drive=drive-virtio-disk0-0-0,id=virtio-disk0-0-0");

    int port = BASE_PORT + vm_id;
    sprintf(buf[10], "-spice");
    sprintf(buf[11], "port=%d,disable-ticketing,jpeg-wan-compression=auto,streaming-video=all", port);

    // sprintf(buf[12], "-D");
    // sprintf(buf[13], "/home/alan/libvirt/log/vm-%d", qemu_proc->vm_id);
}


static void get_cpu_affintiy_status(void)
{
    cpu_set_t mask;
    char buf[1024];
    int len ,i;
    pid_t pid;
    bool find_vm_id = false;

    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);

    len = sprintf(buf, "cpu affinity:");

    int vm_id = recv_vm_id();
    if (vm_id == -1) {
        goto send;
    }
    qemu_proc_t *current;

    current = virt_server.qemu_head;
    if(current == NULL) {
        goto send;
    }

    for (; current != NULL; current = current->next) {
        if (current->vm_id == vm_id) {
            pid = current->pid;
            find_vm_id = true;
            break;
        } else {
            continue;
        }
    }

    if (!find_vm_id) {
        logout("%s(%d): can't find vm id %d\n", __func__, __LINE__, vm_id);
        goto send;
    }

    CPU_ZERO(&mask);
    if (sched_getaffinity(pid, sizeof(mask), &mask) == -1) {
        logout("get %d cpu affinity failed\n", pid);
        goto send;
    }

    for(i = 0; i < cpu_num; i++) {
        if (CPU_ISSET(i, &mask)) {
            buf[len + i] = 'y';
        } else {
            buf[len + i] = '-';
        }
    }

send:
    len += cpu_num;
    if (len > 1024) {
        len = 1024;
        buf[len - 2] = '\n';
        buf[len - 1] = '\0';
    }

    buf[len] = '\n';
    len++;

    /* 1. send buffer lenth first */
    if (write(virt_server.connfd, &len, sizeof(int)) == -1) {
        ERR_EXIT("Error: socket error\n");
    }

    /* 2. then send real data */
    if (write(virt_server.connfd, buf, len) == -1) {
        ERR_EXIT("Error: socket error\n");
    }
}

static void set_cpu_affinity(int vm_id, pid_t pid)
{
    cpu_set_t mask;
    int cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    int cpu_id = vm_id * PER_CPU;
    int i;

    if (cpu_id < 0 || cpu_id >= cpu_num) {
        logout("cpu_id must be in the range [0-%d]\n", cpu_num);
        return;
    }

    CPU_ZERO(&mask);
    for (i = 0; i < PER_CPU; i++, cpu_id++) {
        CPU_SET(cpu_id, &mask);
    }
    if (sched_setaffinity(pid, sizeof(mask), &mask) == -1) {
        logout("bind process %d to cpu %d  failed\n", pid, cpu_id);
        return;
    }
}

static void try_launch_qemu(void)
{
    /* int stat; */
    /* int s_pid; */

    qemu_proc_t * qemu_proc = create_qemu_proc();
    if ( qemu_proc == NULL) {
        logout("launch qemu failed\n");
        return;
    }

    fill_arglist(qemu_proc);

    pid_t pid = fork();

    switch (pid) {
        case -1: // error
            ERR_EXIT("Error: fork error\n");
            break;
        case 0:  // sub-process
            if (execv(QEMU_BIN, arglist) == -1) {
                ERR_EXIT("Error: execute Qemu error.\n");
            }
            break;
        default: // parent-process
            qemu_proc->pid = pid;
            set_cpu_affinity(qemu_proc->vm_id, pid);
            logout("Launch Qemu, pid is %d\n", pid);
            break;
    }
}

static void query_qemu(void)
{
    char buf[1024];
    qemu_proc_t *item;
    int pos = 0;
    int ret;

    pos = sprintf(buf, "\nNow running vm:\n");
    pos += sprintf(buf + pos, "\tvm_id\t pid\n");

    for (item = virt_server.qemu_head; item != NULL; item = item->next) {
        pos += sprintf(buf + pos, "\t%d\t %d\n", item->vm_id, item->pid);
        if (pos >= 1024) {
            pos = 1024;
            buf[pos - 1] = '\0';
            break;
        }
    }

    ret = write(virt_server.connfd, &pos, sizeof(pos));
    if (ret == -1) {
        ERR_EXIT("Error: socket error\n");
    }

    ret = write(virt_server.connfd, buf, pos);
    if (ret == -1) {
        ERR_EXIT("Error: socket error\n");
    }
}

static int free_qemu_with_pid(pid_t pid)
{
    bool find_pid = false;
    qemu_proc_t *prev, *current;

    prev = current = virt_server.qemu_head;
    if(current == NULL) {
        return 0;
    }

    for (; current != NULL; prev = current, current = current->next) {
        if (current->pid == pid) {
            find_pid = true;
            break;
        } else {
            continue;
        }
    }

    if (find_pid) {
        logout("find and free vm pid %d\n", current->pid);

        if (prev == current) {
            free(current);
            virt_server.qemu_head = NULL;
        } else {
            prev->next = current->next;
            free(current);
        }
    } else {
        logout("not find running qemu with pid %d\n", pid);
    }

    return 0;
}

static int kill_qemu_with_vm_id(int vm_id)
{
    bool find_vm_id = false;
    qemu_proc_t *prev, *current;

    prev = current = virt_server.qemu_head;
    if(current == NULL) {
        return 0;
    }

    for (; current != NULL; prev = current, current = current->next) {
        if (current->vm_id == vm_id) {
            find_vm_id = true;
            break;
        } else {
            continue;
        }
    }

    if (find_vm_id) {
        pid_t pid = current->pid;
        logout("Find and kill vm, pid is %d\n", pid);

        if (prev == current) {
            free(current);
            virt_server.qemu_head = NULL;
        } else {
            prev->next = current->next;
            free(current);
        }

        kill(pid, 9);
    } else {
        logout("Can't find running qemu with vm_id %d\n", vm_id);
    }

    return 0;
}

static void kill_qemu(void)
{
    int vm_id = recv_vm_id();

    if (vm_id == -1) {
        return;
    }

    kill_qemu_with_vm_id(vm_id);
}

static int handle_message(void)
{
    int message_type;

    if (recv_message(&message_type) == -1) {
        return -1;
    }

    switch (message_type) {
        case MES_QUREY_QEMU:
            query_qemu();
            break;
        case MES_LAUNCH_QEMU:
            try_launch_qemu();
            break;
        case MES_KILL_QEMU:
            kill_qemu();
            break;
        case MES_GET_CPU_AFFINITY:
            get_cpu_affintiy_status();
            break;
        default:
            logout("unknown message type %d\n", message_type);
            break;
    }

    return 0;
}

#if 0
static ATTR_UNUSED void wait_child(void)
{
    int status;
    pid_t pid;
    do {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            logout("qemu sub-process (%d) exit, status (%d).\n", pid, status);
            free_qemu_with_pid(pid);
        } else if (pid == -1) {
            logout("waitpid error: %s\n", strerror(errno));
            break;
        }
    } while (pid > 0);
}
#endif

static void loop_event(void)
{
    int fd, maxfd;
    int status;
    pid_t pid;

    fd_set *listen_set = &virt_server.listen_set;

    struct timeval timeout = {
        .tv_sec = 60, // 1 min
        .tv_usec = 0,
    };

    logout("==== start loop ====\n");
    while (1) {
        /* Reset timeout caused of the signal will take effect on selec() function. */
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;

        /* Check child-process exiting in loop.
         * The signal handler is deperacted because of the multi-thread safe
         * problem.
         * */
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            logout("qemu sub-process (%d) exit, status (%d).\n", pid, status);
            free_qemu_with_pid(pid);
        }

        FD_ZERO(listen_set);
        FD_SET(virt_server.listenfd, listen_set);
        if (virt_server.connected) {
            FD_SET(virt_server.connfd, listen_set);
        }

        maxfd = virt_server.connfd > virt_server.listenfd ? virt_server.connfd + 1: virt_server.listenfd + 1;
        fd = select(maxfd, listen_set, NULL, NULL, &timeout);
        /* logout("---- test select ----\n"); */

        if (fd == -1) {
            if(errno == EINTR) {
                logout("Warning: %s\n", strerror(errno));
                continue;
            } else {
                ERR_EXIT("Error: %s\n", strerror(errno));
            }
        }

        if (FD_ISSET(virt_server.listenfd, listen_set)) {
            new_connect();
        }

        if (FD_ISSET(virt_server.connfd, listen_set)) {
            handle_message();
        }
    }
    logout("==== stop loop ====\n");
}

static int server_init(void)
{
    virt_server.connected = false;
    virt_server.qemu_head = NULL;
    virt_server.listenfd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (virt_server.listenfd == -1) {
        ERR_EXIT("Error: socket error\n");
    }

    unlink(LIBVIRTD_SOCKET);

    struct sockaddr_un servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family = AF_UNIX;
    strcpy(servaddr.sun_path, LIBVIRTD_SOCKET);

    if (bind(virt_server.listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        ERR_EXIT("Error: bind error\n");
    }

    if (listen(virt_server.listenfd, MAXCONN) == -1) {
        ERR_EXIT("Error: listen error\n");
    }

    return 0;
}

void wait4child(int signo)
{
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        logout("qemu sub-process (%d) exit, status (%d).\n", pid, status);
        free_qemu_with_pid(pid);
    }
    logout("Debug: exiting from signal handler, status(%d)\n",status);
}

static ATTR_UNUSED void set_signal(void)
{
    struct sigaction action;

    action.sa_handler = wait4child;
    sigemptyset(&action.sa_mask);
    action.sa_flags |= SA_RESTART;
    sigaction(SIGCHLD, &action, NULL);
}

int main(int argc, char *argv[])
{
    init_log();

    create_daemon();

    init_pid_file();

    /* set_signal(); */

    server_init();

    loop_event();

    return 0;
}
