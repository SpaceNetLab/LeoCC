#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/sort.h>

#define NETLINK_USER 30
#define RTT_SAMPLE_MAX 100

struct sock *nl_sk = NULL;

static u32 rtt_samples[RTT_SAMPLE_MAX];
static u64 reconfiguration_trigger_time_ms;
static u32 reconfiguration_rtt_ms;
static u32 rtt_sample_count = 0;
static u32 local_rtt_sample_max = 0;
static u32 local_rtt_sample_min = U32_MAX;
static const u32 global_reconfiguration_trigger_duration = 200; // ms
static bool min_rtt_fluctuation_collection;

extern bool global_reconfiguration_trigger;
extern u32 min_rtt_fluctuation;
static u32 reconfiguration_min_rtt;
static u32 reconfiguration_max_rtt;

struct rtt_data {
    u64 sec;    
    u32 usec;   
    char rtt_value_microseconds[16];
    u32 is_reconfig;
};

static int compare_func(const void *a, const void *b)
{
    u32 val_a = *(const u32 *)a;
    u32 val_b = *(const u32 *)b;

    if (val_a < val_b)
        return -1;
    else if (val_a > val_b)
        return 1;
    else
        return 0;
}

static void mpf(u32 percentile_low, u32 percentile_high)
{
    min_rtt_fluctuation_collection = false;
    if (rtt_sample_count == 0) {
        // printk(KERN_ERR "No RTT samples collected yet\n");
        return;
    }
    sort(rtt_samples, rtt_sample_count, sizeof(u32), compare_func, NULL);
    reconfiguration_min_rtt  = rtt_samples[rtt_sample_count * percentile_low / 100];
    reconfiguration_max_rtt = rtt_samples[rtt_sample_count * percentile_high / 100];
    min_rtt_fluctuation = reconfiguration_max_rtt - reconfiguration_min_rtt;
}

static void netlink_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
    struct rtt_data *data;

    nlh = (struct nlmsghdr *)skb->data;
    data = (struct rtt_data *)nlmsg_data(nlh);

    u64 cur_time_ms = data->sec * 1000 + data->usec / 1000;

    if (global_reconfiguration_trigger && reconfiguration_trigger_time_ms > 0 && cur_time_ms >= reconfiguration_trigger_time_ms + global_reconfiguration_trigger_duration) {
        global_reconfiguration_trigger = false;
    }

    if (!min_rtt_fluctuation_collection && reconfiguration_trigger_time_ms > 0 && cur_time_ms >= reconfiguration_trigger_time_ms + reconfiguration_rtt_ms) {
        min_rtt_fluctuation_collection = true;
        rtt_sample_count = 0;
        reconfiguration_min_rtt = U32_MAX;
	    reconfiguration_max_rtt = 0;
        local_rtt_sample_min = U32_MAX;
        local_rtt_sample_max = 0;
        // printk(KERN_INFO "[START COLLECTION] RTT fluctuation collection STARTED at %llu ms\n", cur_time_ms);
    }

    if (min_rtt_fluctuation_collection) {
        u32 rtt_value = 0;
        if (kstrtouint(data->rtt_value_microseconds, 10, &rtt_value) == 0) {
            if (rtt_sample_count < RTT_SAMPLE_MAX) {
                rtt_samples[rtt_sample_count++] = rtt_value;
                if (rtt_value < local_rtt_sample_min)
                    local_rtt_sample_min = rtt_value;
                if (rtt_value > local_rtt_sample_max)
                    local_rtt_sample_max = rtt_value;
                // printk(KERN_INFO "RTT value: %u, min: %u, max: %u\n", rtt_value, local_rtt_sample_min, local_rtt_sample_max);
            } else {
                mpf(5, 95);
                reconfiguration_trigger_time_ms = 0;
                // printk(KERN_INFO "RTT sample buffer full, skipping new sample\n");
            }
        } else {
            printk(KERN_ERR "Invalid RTT value received\n");
        }
    }

    if (data->is_reconfig == 1) {
        global_reconfiguration_trigger = true;
        u32 rtt_value_us = 0;
        if (kstrtouint(data->rtt_value_microseconds, 10, &rtt_value_us) == 0) {
            reconfiguration_trigger_time_ms = cur_time_ms;
            reconfiguration_rtt_ms = rtt_value_us / 1000;
            printk(KERN_INFO "[PREPARE COLLECTION] Reconfiguration detected! Set flag to true. sec: %llu, usec: %u, rtt_value_microseconds: %s. Will start RTT collection after %u ms\n", data->sec % 60, data->usec, data->rtt_value_microseconds, reconfiguration_rtt_ms);
        } else {
            printk(KERN_ERR "Invalid RTT value during reconfig\n");
        }
        return;
    }
}

static int netlink_init(void)
{
    struct netlink_kernel_cfg cfg = {
        .input = netlink_recv_msg,
    };

    nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating Netlink socket.\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "Netlink socket created for LeoCC.\n");
    return 0;
}

static void netlink_exit(void)
{
    if (nl_sk) {
        netlink_kernel_release(nl_sk);
        printk(KERN_INFO "Netlink socket closed for LeoCC.\n");
    }
}

