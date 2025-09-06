#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#define NETLINK_USER 30
typedef uint32_t u32;
typedef uint64_t u64;

struct rtt_data {
    u64 sec;    
    u32 usec;   
    char rtt_value_microseconds[16];
    u32 is_reconfig;
};

int send_rtt_to_kernel(int sock_fd, struct rtt_data *data) {
    struct sockaddr_nl dest_addr;
    struct nlmsghdr *nlh;
    struct iovec iov;
    struct msghdr msg;

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = 0;

    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(sizeof(struct rtt_data)));
    if (!nlh) return -1;
    memset(nlh, 0, NLMSG_SPACE(sizeof(struct rtt_data)));
    nlh->nlmsg_len = NLMSG_SPACE(sizeof(struct rtt_data));
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;

    memcpy(NLMSG_DATA(nlh), data, sizeof(struct rtt_data));

    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int ret = sendmsg(sock_fd, &msg, 0);
    free(nlh);
    return ret;
}

int main(int argc, char *argv[])
{
    struct timeval now, last_time;
    gettimeofday(&last_time, NULL);

    int sock_fd;
    struct sockaddr_nl src_addr;
    char buffer[128];
    FILE *ping_output;
    struct rtt_data data;

    int THRESHOLD = 45;

    if (argc < 2) {
        printf("Usage: %s <destination_ip>\n", argv[0]);
        return -1;
    } else if (argc == 3) {
        THRESHOLD = atoi(argv[2]);
    }

    char ping_cmd[256];
    snprintf(ping_cmd, sizeof(ping_cmd), "ping -i 0.01 %s", argv[1]);

    sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid();
    bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    ping_output = popen(ping_cmd, "r");
    if (!ping_output) {
        perror("popen");
        close(sock_fd);
        return -1;
    }

    while (fgets(buffer, sizeof(buffer), ping_output)) {
        int rtt_value_final = 0;
        char *rtt_str = strstr(buffer, "time=");
        if (rtt_str) {
            rtt_str += strlen("time=");
            char *rtt_end = strstr(rtt_str, " ms");
            if (rtt_end) {
                *rtt_end = '\0';
                rtt_value_final = atof(rtt_str) * 1000;
            }
        }

        gettimeofday(&now, NULL);
        double diff_in_ms = (now.tv_sec - last_time.tv_sec) * 1000.0 + (now.tv_usec - last_time.tv_usec) / 1000.0;

        if (diff_in_ms > THRESHOLD) {
            memset(&data, 0, sizeof(data));
            data.sec = (u64)now.tv_sec;
            data.usec = (u32)now.tv_usec;
            snprintf(data.rtt_value_microseconds, sizeof(data.rtt_value_microseconds), "%d", rtt_value_final);
            data.is_reconfig = 1;
            send_rtt_to_kernel(sock_fd, &data);
        } else if (rtt_value_final > 0) {
            memset(&data, 0, sizeof(data));
            data.sec = (u64)now.tv_sec;
            data.usec = (u32)now.tv_usec;
            snprintf(data.rtt_value_microseconds, sizeof(data.rtt_value_microseconds), "%d", rtt_value_final);
            data.is_reconfig = 0;
            send_rtt_to_kernel(sock_fd, &data);
        }

        last_time = now;
    }
    pclose(ping_output);
    close(sock_fd);
    return 0;
}

