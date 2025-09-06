#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/module.h>
#include <net/tcp.h>
#include <linux/inet_diag.h>
#include <linux/inet.h>
#include <linux/random.h>
#include <linux/win_minmax.h>

#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)

#define LEOCC_SCALE 8
#define LEOCC_UNIT (1 << LEOCC_SCALE)

enum leocc_mode {
	LEOCC_STARTUP,
	LEOCC_DRAIN,
	LEOCC_DYNAMIC_CRUISE,	
	LEOCC_PROBE_RTT,
};

struct leocc {
	u32	min_rtt_us;
	u32	min_rtt_stamp;	
	u32	probe_rtt_done_stamp; 
	struct minmax bw;
	u32	rtt_cnt;
	u32 next_rtt_delivered; 
	u64	cycle_mstamp;	
	u32 mode:3,		 
		prev_ca_state:3,    
		packet_conservation:1,
		round_start:1,	   
		idle_restart:1,	     
		probe_rtt_round_done:1,  
		unused:10, 
		use_max_filter:1,
		p_post_bw:5,
		p_post_rtt:5,
		reconfiguration_trigger:1;

    u32	reconfiguration_max_bw;	        
	u32	rtt_cnt_max_bw; 		 
	u32 latest_bw;    

	u32	bw_hat_post;
	u32 rtt_hat_post;	       
    u32	kalman_gain_bw;
	u32 kalman_gain_rtt;   

	u32	pacing_gain:10,
		cwnd_gain:10,	
		full_bw_reached:1,   
		full_bw_cnt:2,	
		cycle_idx:3,	
		has_seen_rtt:1, 
		unused_b:5;
	u32	prior_cwnd;	
	u32	full_bw;	

	u64	ack_epoch_mstamp;	
	u16	extra_acked[2];		
	u32	ack_epoch_acked:20,	
		extra_acked_win_rtts:5,	
		extra_acked_win_idx:1,	
		unused_c:6;
};

#define CYCLE_LEN	8

u32 delta_rtt = 0;
module_param(delta_rtt, uint, 0644);
MODULE_PARM_DESC(delta_rtt, "delta RTT adjustment for LeoCC (default 0)");

u32 delta_thresh = 45000; 
module_param(delta_thresh, uint, 0644);
MODULE_PARM_DESC(delta_thresh, "delta threshold for reconfiguration");

u32 offset = 12000; 
module_param(offset, uint, 0644);
MODULE_PARM_DESC(offset, "Offset value");

u32 min_rtt_fluctuation = 10000; 
module_param(min_rtt_fluctuation, uint, 0644);
MODULE_PARM_DESC(min_rtt_fluctuation, "minRTT fluctuation value");

static u32 init_stamp = 0;

static const u32 var_R = 4;
static const u32 var_Q = 4;
static const u32 var_R_rtt = 4;
static const u32 var_Q_rtt = 4;

static const int PERIOD = 15000;
static const u32 leocc_tso_rtt_shift = 9;
static const u32 leocc_probe_rtt_cwnd_gain = LEOCC_UNIT * 1 / 2;
static int leocc_bw_rtts = CYCLE_LEN + 2;
static const u32 leocc_min_rtt_win_sec = 20;
static const u32 leocc_probe_rtt_mode_ms = 200;
static const int leocc_min_tso_rate = 1200000;
static const int leocc_pacing_margin_percent = 1;
static const int leocc_high_gain  = LEOCC_UNIT * 2885 / 1000 + 1;
static const int leocc_drain_gain = LEOCC_UNIT * 1000 / 2885;
static const int leocc_cwnd_gain  = LEOCC_UNIT * 2;
static int leocc_pacing_gain[] = {
	LEOCC_UNIT * 5 / 4,	
	LEOCC_UNIT * 3 / 4,	
	LEOCC_UNIT, LEOCC_UNIT, LEOCC_UNIT,
	LEOCC_UNIT, LEOCC_UNIT, LEOCC_UNIT	
};

static const u32 leocc_cycle_rand = 7;
static const u32 leocc_cwnd_min_target = 4;
static const u32 leocc_full_bw_thresh = LEOCC_UNIT * 5 / 4;
static const u32 leocc_full_bw_cnt = 3;
static const int leocc_extra_acked_gain = LEOCC_UNIT;
static const u32 leocc_extra_acked_win_rtts = 5;
static const u32 leocc_ack_epoch_acked_reset_thresh = 1U << 20;
static const u32 leocc_extra_acked_max_us = 100 * 1000;

static void leocc_check_probe_rtt_done(struct sock *sk);

static bool leocc_full_bw_reached(const struct sock *sk)
{
	const struct leocc *leocc = inet_csk_ca(sk);

	return leocc->full_bw_reached;
}

static u32 leocc_max_bw(const struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	return minmax_get(&leocc->bw);
}

static u32 leocc_bw(const struct sock *sk)
{
	return leocc_max_bw(sk);
}

static u16 leocc_extra_acked(const struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	return max(leocc->extra_acked[0], leocc->extra_acked[1]);
}

static u64 leocc_rate_bytes_per_sec(struct sock *sk, u64 rate, int gain)
{
	unsigned int mss = tcp_sk(sk)->mss_cache;

	rate *= mss;
	rate *= gain;
	rate >>= LEOCC_SCALE;
	rate *= USEC_PER_SEC / 100 * (100 - leocc_pacing_margin_percent);
	return rate >> BW_SCALE;
}

static unsigned long leocc_bw_to_pacing_rate(struct sock *sk, u32 bw, int gain)
{
	u64 rate = bw;

	rate = leocc_rate_bytes_per_sec(sk, rate, gain);
	rate = min_t(u64, rate, sk->sk_max_pacing_rate);
	return rate;
}

static void leocc_init_pacing_rate_from_rtt(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	u64 bw;
	u32 rtt_us;

	if (tp->srtt_us) {	
		rtt_us = max(tp->srtt_us >> 3, 1U);
		leocc->has_seen_rtt = 1;
	} else {	
		rtt_us = USEC_PER_MSEC;	
	}
	bw = (u64)tcp_snd_cwnd(tp) * BW_UNIT;
	do_div(bw, rtt_us);
	sk->sk_pacing_rate = leocc_bw_to_pacing_rate(sk, bw, leocc_high_gain);
}

static void leocc_set_pacing_rate(struct sock *sk, u32 bw, int gain)
{	
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	unsigned long rate = leocc_bw_to_pacing_rate(sk, bw, gain);

	if (unlikely(!leocc->has_seen_rtt && tp->srtt_us))
		leocc_init_pacing_rate_from_rtt(sk);
	if (leocc_full_bw_reached(sk) || rate > sk->sk_pacing_rate)
		sk->sk_pacing_rate = rate;
}

__bpf_kfunc static u32 leocc_min_tso_segs(struct sock *sk)
{
	return sk->sk_pacing_rate < (leocc_min_tso_rate >> 3) ? 1 : 2;
}

static u32 leocc_tso_segs_generic(struct sock *sk, unsigned int mss_now,
	u32 gso_max_size)
{
	struct leocc *leocc = inet_csk_ca(sk);
	u32 segs, r;
	u64 bytes;

	bytes = sk->sk_pacing_rate >> sk->sk_pacing_shift;

	if (leocc_tso_rtt_shift) {
		r = leocc->min_rtt_us >> leocc_tso_rtt_shift;
		if (r < BITS_PER_TYPE(u32))
			bytes += GSO_LEGACY_MAX_SIZE >> r;	
	}

	bytes = min_t(u32, bytes, gso_max_size - 1 - MAX_TCP_HEADER);
	segs = max_t(u32, bytes / mss_now,
		sock_net(sk)->ipv4.sysctl_tcp_min_tso_segs);
	return segs;
}

__bpf_kfunc static u32 leocc_tso_segs(struct sock *sk, unsigned int mss_now)
{
	return leocc_tso_segs_generic(sk, mss_now, sk->sk_gso_max_size);
}

static u32 leocc_tso_segs_goal(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 segs, bytes;

	bytes = min_t(unsigned long,
		      sk->sk_pacing_rate >> READ_ONCE(sk->sk_pacing_shift),
		      GSO_LEGACY_MAX_SIZE - 1 - MAX_TCP_HEADER);
	segs = max_t(u32, bytes / tp->mss_cache, leocc_min_tso_segs(sk));

	return min(segs, 0x7FU);
}

static void leocc_save_cwnd(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);

	if (leocc->prev_ca_state < TCP_CA_Recovery && leocc->mode != LEOCC_PROBE_RTT)
		leocc->prior_cwnd = tcp_snd_cwnd(tp); 
	else  
		leocc->prior_cwnd = max(leocc->prior_cwnd, tcp_snd_cwnd(tp));
}

__bpf_kfunc static void leocc_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);

	if (event == CA_EVENT_TX_START && tp->app_limited) {
		leocc->idle_restart = 1;
		leocc->ack_epoch_mstamp = tp->tcp_mstamp;
		leocc->ack_epoch_acked = 0;
		if (leocc->mode == LEOCC_DYNAMIC_CRUISE)
			leocc_set_pacing_rate(sk, leocc_bw(sk), LEOCC_UNIT);
		else if (leocc->mode == LEOCC_PROBE_RTT)
			leocc_check_probe_rtt_done(sk);
	}
}

static u32 leocc_bdp(struct sock *sk, u32 bw, int gain)
{
	struct leocc *leocc = inet_csk_ca(sk);
	u32 bdp;
	u64 w;

	if (unlikely(leocc->min_rtt_us == ~0U))	
		return TCP_INIT_CWND; 

	w = (u64)bw * leocc->min_rtt_us;

	bdp = (((w * gain) >> LEOCC_SCALE) + BW_UNIT - 1) / BW_UNIT;

	return bdp;
}

static u32 leocc_quantization_budget(struct sock *sk, u32 cwnd)
{
	struct leocc *leocc = inet_csk_ca(sk);

	cwnd += 3 * leocc_tso_segs_goal(sk);
	cwnd = (cwnd + 1) & ~1U;
	if (leocc->mode == LEOCC_DYNAMIC_CRUISE && leocc->cycle_idx == 0)
		cwnd += 2;

	return cwnd;
}

static u32 leocc_inflight(struct sock *sk, u32 bw, int gain)
{
	u32 inflight;

	inflight = leocc_bdp(sk, bw, gain);
	inflight = leocc_quantization_budget(sk, inflight);

	return inflight;
}

static u32 leocc_packets_in_net_at_edt(struct sock *sk, u32 inflight_now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	u64 now_ns, edt_ns, interval_us;
	u32 interval_delivered, inflight_at_edt;

	now_ns = tp->tcp_clock_cache;
	edt_ns = max(tp->tcp_wstamp_ns, now_ns);
	interval_us = div_u64(edt_ns - now_ns, NSEC_PER_USEC);
	interval_delivered = (u64)leocc_bw(sk) * interval_us >> BW_SCALE;
	inflight_at_edt = inflight_now;
	if (leocc->pacing_gain > LEOCC_UNIT)      
		inflight_at_edt += leocc_tso_segs_goal(sk);
	if (interval_delivered >= inflight_at_edt)
		return 0;
	return inflight_at_edt - interval_delivered;
}

static u32 leocc_ack_aggregation_cwnd(struct sock *sk)
{
	u32 max_aggr_cwnd, aggr_cwnd = 0;

	if (leocc_extra_acked_gain && leocc_full_bw_reached(sk)) {
		max_aggr_cwnd = ((u64)leocc_bw(sk) * leocc_extra_acked_max_us)
				/ BW_UNIT;
		aggr_cwnd = (leocc_extra_acked_gain * leocc_extra_acked(sk))
			     >> LEOCC_SCALE;
		aggr_cwnd = min(aggr_cwnd, max_aggr_cwnd);
	}

	return aggr_cwnd;
}

static bool leocc_set_cwnd_to_recover_or_restore(
	struct sock *sk, const struct rate_sample *rs, u32 acked, u32 *new_cwnd)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	u8 prev_state = leocc->prev_ca_state, state = inet_csk(sk)->icsk_ca_state;
	u32 cwnd = tcp_snd_cwnd(tp);

	if (rs->losses > 0)
		cwnd = max_t(s32, cwnd - rs->losses, 1);

	if (state == TCP_CA_Recovery && prev_state != TCP_CA_Recovery) {
		leocc->packet_conservation = 1;
		leocc->next_rtt_delivered = tp->delivered; 
		cwnd = tcp_packets_in_flight(tp) + acked;
	} else if (prev_state >= TCP_CA_Recovery && state < TCP_CA_Recovery) {
		cwnd = max(cwnd, leocc->prior_cwnd);
		leocc->packet_conservation = 0;
	}
	leocc->prev_ca_state = state;

	if (leocc->packet_conservation) {
		*new_cwnd = max(cwnd, tcp_packets_in_flight(tp) + acked);
		return true;
	}
	*new_cwnd = cwnd;
	return false;
}

static u32 leocc_probe_rtt_cwnd(struct sock *sk)
{
	return max_t(u32, leocc_cwnd_min_target,
		     leocc_bdp(sk, leocc_bw(sk), leocc_probe_rtt_cwnd_gain));
}

static void leocc_set_cwnd(struct sock *sk, const struct rate_sample *rs,
			 u32 acked, u32 bw, int gain)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	u32 cwnd = tcp_snd_cwnd(tp), target_cwnd = 0;

	if (!acked)
		goto done;  

	if (leocc_set_cwnd_to_recover_or_restore(sk, rs, acked, &cwnd))
		goto done;

	target_cwnd = leocc_bdp(sk, bw, gain);

	target_cwnd += leocc_ack_aggregation_cwnd(sk);
	target_cwnd = leocc_quantization_budget(sk, target_cwnd);

	if (leocc_full_bw_reached(sk))
		cwnd = min(cwnd + acked, target_cwnd);
	else if (cwnd < target_cwnd || tp->delivered < TCP_INIT_CWND)
		cwnd = cwnd + acked;
	cwnd = max(cwnd, leocc_cwnd_min_target);

done:
	tcp_snd_cwnd_set(tp, min(cwnd, tp->snd_cwnd_clamp));	
	if (leocc->mode == LEOCC_PROBE_RTT)  
        tcp_snd_cwnd_set(tp, min_t(u32, tcp_snd_cwnd(tp), leocc_probe_rtt_cwnd(sk)));
}

static bool leocc_is_next_cycle_phase(struct sock *sk,
				    const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	bool is_full_length =
		tcp_stamp_us_delta(tp->delivered_mstamp, leocc->cycle_mstamp) >
		leocc->min_rtt_us;
	u32 inflight, bw;

	if (leocc->pacing_gain == LEOCC_UNIT)
		return is_full_length;	

	inflight = leocc_packets_in_net_at_edt(sk, rs->prior_in_flight);
	bw = leocc_max_bw(sk);

	if (leocc->pacing_gain > LEOCC_UNIT) 
		return is_full_length &&
                        (rs->losses || 
                         inflight >= leocc_inflight(sk, bw, leocc->pacing_gain));	

	return is_full_length ||
		inflight <= leocc_inflight(sk, bw, LEOCC_UNIT);
}

static void leocc_advance_cycle_phase(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);

	leocc->cycle_idx = (leocc->cycle_idx + 1) & (CYCLE_LEN - 1);
	leocc->cycle_mstamp = tp->delivered_mstamp;
}

static void leocc_update_cycle_phase(struct sock *sk,
				   const struct rate_sample *rs)
{
	struct leocc *leocc = inet_csk_ca(sk);

	if (leocc->mode == LEOCC_DYNAMIC_CRUISE && leocc_is_next_cycle_phase(sk, rs))
		leocc_advance_cycle_phase(sk);
}

static void leocc_reset_startup_mode(struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	leocc->full_bw_reached = 0;
	leocc->full_bw = 0;
	leocc->full_bw_cnt = 0;

	leocc->mode = LEOCC_STARTUP;
}

static void leocc_reset_probe_bw_mode(struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	leocc->mode = LEOCC_DYNAMIC_CRUISE;
	leocc->cycle_idx = CYCLE_LEN - 1 - get_random_u32_below(leocc_cycle_rand);
	leocc_advance_cycle_phase(sk);
}

static void leocc_reset_mode(struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);
	if (!leocc_full_bw_reached(sk)  || leocc->reconfiguration_trigger)
		leocc_reset_startup_mode(sk);
	else
		leocc_reset_probe_bw_mode(sk);
}

static void leocc_update_bw(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	u64 bw;

	leocc->round_start = 0;
	if (rs->delivered < 0 || rs->interval_us <= 0)
		return;

	if (!before(rs->prior_delivered, leocc->next_rtt_delivered)) {
		leocc->next_rtt_delivered = tp->delivered;
		leocc->rtt_cnt++;
        leocc->rtt_cnt_max_bw = 0;
		leocc->round_start = 1;
		leocc->packet_conservation = 0;
	}

	bw = div64_long((u64)rs->delivered * BW_UNIT, rs->interval_us);
	if (bw > leocc->rtt_cnt_max_bw)
        leocc->rtt_cnt_max_bw = bw;

	if (leocc->mode == LEOCC_PROBE_RTT && rs->rtt_us > 0 && leocc->rtt_hat_post > rs->rtt_us + delta_thresh) {
		leocc->reconfiguration_max_bw = leocc->latest_bw;
	}

	if (!rs->is_app_limited || bw >= leocc_max_bw(sk)) {
		minmax_running_max(&leocc->bw, leocc_bw_rtts, leocc->rtt_cnt, bw);
	}
	leocc->latest_bw = bw;
}

static void leocc_update_ack_aggregation(struct sock *sk,
				       const struct rate_sample *rs)
{
	u32 epoch_us, expected_acked, extra_acked;
	struct leocc *leocc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (!leocc_extra_acked_gain || rs->acked_sacked <= 0 ||
	    rs->delivered < 0 || rs->interval_us <= 0)
		return;

	if (leocc->round_start) {
		leocc->extra_acked_win_rtts = min(0x1F,
						leocc->extra_acked_win_rtts + 1);
		if (leocc->extra_acked_win_rtts >= leocc_extra_acked_win_rtts) {
			leocc->extra_acked_win_rtts = 0;
			leocc->extra_acked_win_idx = leocc->extra_acked_win_idx ?
						   0 : 1;
			leocc->extra_acked[leocc->extra_acked_win_idx] = 0;
		}
	}

	epoch_us = tcp_stamp_us_delta(tp->delivered_mstamp,
				      leocc->ack_epoch_mstamp);
	expected_acked = ((u64)leocc_bw(sk) * epoch_us) / BW_UNIT;

	if (leocc->ack_epoch_acked <= expected_acked ||
	    (leocc->ack_epoch_acked + rs->acked_sacked >=
	     leocc_ack_epoch_acked_reset_thresh)) {
		leocc->ack_epoch_acked = 0;
		leocc->ack_epoch_mstamp = tp->delivered_mstamp;
		expected_acked = 0;
	}

	leocc->ack_epoch_acked = min_t(u32, 0xFFFFF,
				     leocc->ack_epoch_acked + rs->acked_sacked);
	extra_acked = leocc->ack_epoch_acked - expected_acked;
	extra_acked = min(extra_acked, tcp_snd_cwnd(tp));
	if (extra_acked > leocc->extra_acked[leocc->extra_acked_win_idx])
		leocc->extra_acked[leocc->extra_acked_win_idx] = extra_acked;
}

static void leocc_check_full_bw_reached(struct sock *sk,
				      const struct rate_sample *rs)
{
	struct leocc *leocc = inet_csk_ca(sk);
	u32 bw_thresh;

	if (leocc_full_bw_reached(sk) || !leocc->round_start || rs->is_app_limited)
		return;

	bw_thresh = (u64)leocc->full_bw * leocc_full_bw_thresh >> LEOCC_SCALE;
	if (leocc_max_bw(sk) >= bw_thresh) {
		leocc->full_bw = leocc_max_bw(sk);
		leocc->full_bw_cnt = 0;
		return;
	}
	++leocc->full_bw_cnt;
	leocc->full_bw_reached = leocc->full_bw_cnt >= leocc_full_bw_cnt;
}

static void leocc_check_drain(struct sock *sk, const struct rate_sample *rs)
{
	struct leocc *leocc = inet_csk_ca(sk);

	if (leocc->mode == LEOCC_STARTUP && leocc_full_bw_reached(sk)) {
		leocc->reconfiguration_max_bw = 0;
        leocc->reconfiguration_trigger = 0;
		leocc->mode = LEOCC_DRAIN;	
		tcp_sk(sk)->snd_ssthresh =
				leocc_inflight(sk, leocc_max_bw(sk), LEOCC_UNIT);
	}
	if (leocc->mode == LEOCC_DRAIN &&
	    leocc_packets_in_net_at_edt(sk, tcp_packets_in_flight(tcp_sk(sk))) <=
	    leocc_inflight(sk, leocc_max_bw(sk), LEOCC_UNIT))
		leocc_reset_probe_bw_mode(sk);
}

static void leocc_check_probe_rtt_done(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);

	if (!(leocc->probe_rtt_done_stamp &&
	      after(tcp_jiffies32, leocc->probe_rtt_done_stamp)))
		return;

    tcp_snd_cwnd_set(tp, max(tcp_snd_cwnd(tp), leocc->prior_cwnd));
	leocc_reset_mode(sk);

	if (leocc->reconfiguration_trigger) {
		minmax_reset(&leocc->bw, leocc->rtt_cnt, leocc->reconfiguration_max_bw);
	}
}

static void leocc_update_min_rtt(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);
	bool filter_expired;

	filter_expired = after(tcp_jiffies32,
			       leocc->min_rtt_stamp + leocc_min_rtt_win_sec * HZ);

	if (rs->rtt_us >= 0 &&
	    (rs->rtt_us < leocc->min_rtt_us ||
	     (filter_expired && !rs->is_ack_delayed))) {
		leocc->min_rtt_us = rs->rtt_us;
		leocc->min_rtt_stamp = tcp_jiffies32;
	}

	if (!leocc->idle_restart && leocc->mode == LEOCC_DYNAMIC_CRUISE && 
		((leocc_probe_rtt_mode_ms > 0 && filter_expired) || leocc->reconfiguration_trigger)) {
		leocc->mode = LEOCC_PROBE_RTT; 
		leocc->probe_rtt_done_stamp = 0;
		leocc_save_cwnd(sk); 
	}

	if (leocc->mode == LEOCC_PROBE_RTT) {
		tp->app_limited =
			(tp->delivered + tcp_packets_in_flight(tp)) ? : 1;
		if (!leocc->probe_rtt_done_stamp){
			if (tcp_packets_in_flight(tp) <= leocc_probe_rtt_cwnd(sk)) {
                leocc->probe_rtt_done_stamp = tcp_jiffies32 +
                    msecs_to_jiffies(leocc_probe_rtt_mode_ms);
                leocc->probe_rtt_round_done = 0;
                leocc->next_rtt_delivered = tp->delivered;
			}
		}else if (leocc->probe_rtt_done_stamp) {
			if (leocc->round_start)
				leocc->probe_rtt_round_done = 1;
			if (leocc->probe_rtt_round_done) {
				if (after(tcp_jiffies32, leocc->probe_rtt_done_stamp)) {
					leocc->min_rtt_us = rs->rtt_us;
					leocc->min_rtt_stamp = tcp_jiffies32;
				}
		  		leocc_check_probe_rtt_done(sk);
			}
		}
	}
	if (rs->delivered > 0)
		leocc->idle_restart = 0;
}

static void leocc_update_gains(struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	switch (leocc->mode) {
	case LEOCC_STARTUP:
		leocc->pacing_gain = leocc_high_gain;
		leocc->cwnd_gain	 = leocc_high_gain;
		break;
	case LEOCC_DRAIN:
		leocc->pacing_gain = leocc_drain_gain;
		leocc->cwnd_gain	 = leocc_high_gain;	
		break;
	case LEOCC_DYNAMIC_CRUISE:
		leocc->pacing_gain = leocc_pacing_gain[leocc->cycle_idx];
		leocc->cwnd_gain	 = leocc_cwnd_gain;
		break;
	case LEOCC_PROBE_RTT:
		leocc->pacing_gain = LEOCC_UNIT;
		leocc->cwnd_gain	 = LEOCC_UNIT;
		break;
	default:
		WARN_ONCE(1, "LEOCC bad mode: %u\n", leocc->mode);
		break;
	}
}

static void leocc_update_model(struct sock *sk, const struct rate_sample *rs)
{
	leocc_update_bw(sk, rs);
	leocc_update_ack_aggregation(sk, rs);
	leocc_update_cycle_phase(sk, rs);
	leocc_check_full_bw_reached(sk, rs);
	leocc_check_drain(sk, rs);
	leocc_update_min_rtt(sk, rs);
	leocc_update_gains(sk);
}

__bpf_kfunc static void leocc_main(struct sock *sk, const struct rate_sample *rs)
{
	struct leocc *leocc = inet_csk_ca(sk);
	u32 delta_since_start = (tcp_jiffies32 - init_stamp) * 1000 / HZ;

	u32 relative_time = delta_since_start % PERIOD;

	u32 magic_offet = 100; // 100ms time window to trigger reconfiguration
    if (!leocc->reconfiguration_trigger && relative_time >= offset - magic_offet && relative_time <= offset){
		leocc->reconfiguration_trigger = 1;
    }

	if (leocc->mode == LEOCC_DYNAMIC_CRUISE && !before(rs->prior_delivered, leocc->next_rtt_delivered)) {	
		leocc->p_post_bw = leocc->p_post_bw + var_Q;
		leocc->kalman_gain_bw = leocc->p_post_bw * LEOCC_UNIT / (leocc->p_post_bw + var_R);
		leocc->bw_hat_post = ((LEOCC_UNIT - leocc->kalman_gain_bw) * leocc->bw_hat_post + leocc->kalman_gain_bw * leocc->rtt_cnt_max_bw) / LEOCC_UNIT;
		leocc->p_post_bw = (LEOCC_UNIT - leocc->kalman_gain_bw) * leocc->p_post_bw / LEOCC_UNIT;
	}

	u32 bw;
	leocc_update_model(sk, rs);

	if (rs->rtt_us > 0) {	
		leocc->p_post_rtt = leocc->p_post_rtt + var_Q_rtt;
		leocc->kalman_gain_rtt = leocc->p_post_rtt * LEOCC_UNIT / (leocc->p_post_rtt + var_R_rtt);
		leocc->rtt_hat_post = ((LEOCC_UNIT - leocc->kalman_gain_rtt) * leocc->rtt_hat_post + leocc->kalman_gain_rtt * rs->rtt_us) / LEOCC_UNIT;
		leocc->p_post_rtt = (LEOCC_UNIT - leocc->kalman_gain_rtt) * leocc->p_post_rtt / LEOCC_UNIT;
	}

	leocc->use_max_filter = true;
	bw = leocc_bw(sk);

	if (leocc->rtt_hat_post >= delta_rtt + leocc->min_rtt_us + min_rtt_fluctuation && !leocc->reconfiguration_trigger && leocc->mode == LEOCC_DYNAMIC_CRUISE)
	{
		leocc->use_max_filter = false;
		bw = leocc->bw_hat_post;
		leocc_pacing_gain[0] = LEOCC_UNIT * 21 / 20;
	} else {
		leocc_pacing_gain[0] = LEOCC_UNIT * 5 / 4;
	}

	leocc_set_pacing_rate(sk, bw, leocc->pacing_gain);
	leocc_set_cwnd(sk, rs, rs->acked_sacked, bw, leocc->cwnd_gain);
}

__bpf_kfunc static void leocc_init(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct leocc *leocc = inet_csk_ca(sk);

	init_stamp = tcp_jiffies32;
	leocc->reconfiguration_max_bw = 0;
	leocc->use_max_filter = true;
	leocc->latest_bw = 0;
    leocc->kalman_gain_bw = 0;
	leocc->kalman_gain_rtt = 0;
    leocc->bw_hat_post = 0;
	leocc->rtt_hat_post = 0;
    leocc->p_post_bw = 25;
	leocc->p_post_rtt = 25;
    leocc->rtt_cnt_max_bw = 0;
	leocc->min_rtt_stamp = tcp_jiffies32;
	leocc->prior_cwnd = 0;
	tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;
	leocc->rtt_cnt = 0;
	leocc->next_rtt_delivered = tp->delivered;
	leocc->prev_ca_state = TCP_CA_Open;
	leocc->packet_conservation = 0;

	leocc->probe_rtt_done_stamp = 0;
	leocc->probe_rtt_round_done = 0;
	leocc->min_rtt_us = tcp_min_rtt(tp);

	minmax_reset(&leocc->bw, leocc->rtt_cnt, 0); 

	leocc->has_seen_rtt = 0;
	leocc_init_pacing_rate_from_rtt(sk);

	leocc->round_start = 0;
	leocc->idle_restart = 0;
	leocc->full_bw_reached = 0;
	leocc->full_bw = 0;
	leocc->full_bw_cnt = 0;
	leocc->cycle_mstamp = 0;
	leocc->cycle_idx = 0;
	leocc_reset_startup_mode(sk);

	leocc->ack_epoch_mstamp = tp->tcp_mstamp;
	leocc->ack_epoch_acked = 0;
	leocc->extra_acked_win_rtts = 0;
	leocc->extra_acked_win_idx = 0;
	leocc->extra_acked[0] = 0;
	leocc->extra_acked[1] = 0;

	cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

__bpf_kfunc static u32 leocc_sndbuf_expand(struct sock *sk)
{
	return 3;
}

__bpf_kfunc static u32 leocc_undo_cwnd(struct sock *sk)
{
	struct leocc *leocc = inet_csk_ca(sk);

	leocc->full_bw = 0; 
	leocc->full_bw_cnt = 0;
	return tcp_snd_cwnd(tcp_sk(sk));
}

__bpf_kfunc static u32 leocc_ssthresh(struct sock *sk)
{
	leocc_save_cwnd(sk);
	return tcp_sk(sk)->snd_ssthresh;
}

__bpf_kfunc static void leocc_set_state(struct sock *sk, u8 new_state)
{
	struct leocc *leocc = inet_csk_ca(sk);

	if (new_state == TCP_CA_Loss) {
		leocc->prev_ca_state = TCP_CA_Loss;
		leocc->full_bw = 0;
		leocc->round_start = 1;
	}
}

static struct tcp_congestion_ops leocc_cong_ops __read_mostly = {
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "leocc",
	.owner		= THIS_MODULE,
	.init		= leocc_init,
	.cong_control	= leocc_main,
	.sndbuf_expand	= leocc_sndbuf_expand,
	.undo_cwnd	= leocc_undo_cwnd,
	.cwnd_event	= leocc_cwnd_event,
	.ssthresh	= leocc_ssthresh,
    .tso_segs	= leocc_tso_segs,
	// .min_tso_segs	= leocc_min_tso_segs,
	.set_state	= leocc_set_state,
};

BTF_SET8_START(leocc_check_kfunc_ids)
#ifdef CONFIG_X86
#ifdef CONFIG_DYNAMIC_FTRACE
BTF_ID_FLAGS(func, leocc_init)
BTF_ID_FLAGS(func, leocc_main)
BTF_ID_FLAGS(func, leocc_sndbuf_expand)
BTF_ID_FLAGS(func, leocc_undo_cwnd)
BTF_ID_FLAGS(func, leocc_cwnd_event)
BTF_ID_FLAGS(func, leocc_ssthresh)
BTF_ID_FLAGS(func, leocc_min_tso_segs)
BTF_ID_FLAGS(func, leocc_set_state)
#endif
#endif
BTF_SET8_END(leocc_check_kfunc_ids)

static const struct btf_kfunc_id_set leocc_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &leocc_check_kfunc_ids,
};

static int __init leocc_register(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct leocc) > ICSK_CA_PRIV_SIZE);

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &leocc_kfunc_set);
	if (ret < 0)
		return ret;
	return tcp_register_congestion_control(&leocc_cong_ops);
}

static void __exit leocc_unregister(void)
{
	tcp_unregister_congestion_control(&leocc_cong_ops);
}

module_init(leocc_register);
module_exit(leocc_unregister);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LEOCC");