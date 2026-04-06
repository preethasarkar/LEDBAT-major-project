#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

static int32_t ertt_id;

/* =========================
   LEDBAT++ State & Config
   ========================= */
#define LEDBAT_MIN_CWND_PKTS 2
#define LEDBAT_PERIODIC_INTERVAL 300
#define SCALE 1  /* 2^10 for fixed-point math */
#define CONSTANT 1  /* multiplicative decrease constant, per draft Section 4.2 */
const char *state_names[] = {"NORMAL", "DRAINING", "RECOVERING"};

struct ledbatpp {
    uint32_t ack_count;
    int slow_start;
    int initial_ss_done;
    uint32_t min_rtt;      /* our own min RTT tracker, filters bogus early samples */

    /* Slowdown State Machine */
    enum { LB_NORMAL, LB_DRAINING, LB_RECOVERING } state;
    uint32_t drain_rtt_count;
    tcp_seq  next_rtt_seq;
    uint32_t rtts_since_event;
};

VNET_DEFINE_STATIC(uint32_t, ledbatpp_target) = 60; /* microseconds (60ms) */
#define V_ledbatpp_target VNET(ledbatpp_target)

/* Fixed-point gain: GAIN = 1 / min(16, CEIL(2*target/base)) * SCALE */
static uint32_t
compute_gain(uint32_t target, uint32_t base)
{
    if (base == 0) return SCALE;
    uint32_t val = (2 * target + base - 1) / base;  /* CEIL(2*target/base) */
    if (val > 16) val = 16;
    if (val < 1)  val = 1;
    return SCALE / val;
}

static size_t ledbatpp_data_sz(void) { return sizeof(struct ledbatpp); }

static int
ledbatpp_cb_init(struct cc_var *ccv, void *ptr)
{
    struct ledbatpp *d;
    if (ptr == NULL) {
        d = malloc(sizeof(struct ledbatpp), M_CC_MEM, M_NOWAIT | M_ZERO);
        if (d == NULL) return ENOMEM;
    } else d = ptr;
    ccv->cc_data = d;
    return 0;
}

static void ledbatpp_cb_destroy(struct cc_var *ccv) { 
    if (ccv->cc_data != NULL)
        free(ccv->cc_data, M_CC_MEM); 
}

/* =========================
   ACK Handling (CORE LOGIC)
   ========================= */
static void
ledbatpp_ack_received(struct cc_var *ccv, ccsignal_t type)
{
    struct ertt *e_t;
    struct ledbatpp *d;
    uint32_t mss, target, gain, current_rtt;
    int64_t queue_delay;
    tcp_seq ack;

    d = ccv->cc_data;
    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);

    /* NULL Guards to prevent Kernel Panic */
    if (d == NULL || e_t == NULL)
        return;

    mss = tcp_fixed_maxseg(ccv->tp);
    target = V_ledbatpp_target;
    ack = ccv->tp->snd_una;

    if (!(e_t->flags & ERTT_NEW_MEASUREMENT))
        return;

    current_rtt = (uint32_t)e_t->markedpkt_rtt;

    /* Update our min_rtt — ignore zero or implausibly small values */
    if (e_t->minrtt > 0)
    	d->min_rtt = (uint32_t)e_t->minrtt;

    /* Guard: skip until we have a valid baseline */
    if (d->min_rtt == 0 || e_t->minrtt == 0 || e_t->markedpkt_rtt == 0)
        goto end;

    queue_delay = (int64_t)current_rtt - (int64_t)d->min_rtt;
    gain = compute_gain(target, d->min_rtt);

    /* Bootstrap next_rtt_seq on first measurement */
    if (d->next_rtt_seq == 0)
        d->next_rtt_seq = ccv->tp->snd_max;

    /* Log: wall time (s.us), delay (ms), cwnd (bytes), min_rtt (us), current_rtt (us) */
    {
        struct timeval tv;
        microuptime(&tv);


printf("LEDBATPP_TRACE,%jd.%06ld,%ld,%u,%u,%u,%s,%d\n",
       (intmax_t)tv.tv_sec, (long)tv.tv_usec,
       (long)(queue_delay),
       CCV(ccv, snd_cwnd),
       d->min_rtt,
       current_rtt,
       state_names[d->state],
       d->slow_start
); /* Add the mapped string here */
    
    }

    /* ---------------------------------------------------------
       STATE: DRAINING
       --------------------------------------------------------- */
    if (d->state == LB_DRAINING) {
        CCV(ccv, snd_cwnd) = LEDBAT_MIN_CWND_PKTS * mss;

        if (SEQ_GEQ(ack, d->next_rtt_seq)) {
            d->drain_rtt_count++;
            d->next_rtt_seq = ccv->tp->snd_max; 
        }

        if (d->drain_rtt_count >= 2) {
            d->state = LB_RECOVERING;
            d->slow_start = 1; 
        }
        goto end;
    }

    /* ---------------------------------------------------------
       STATE: RECOVERING
       --------------------------------------------------------- */

if (d->state == LB_RECOVERING) {
        /* CORRECT Linear Growth: Divide by CWND */
        CCV(ccv, snd_cwnd) += (uint32_t)(((uint64_t)gain * mss * mss) / ((uint64_t)CCV(ccv, snd_cwnd) * SCALE));

        if (CCV(ccv, snd_cwnd) >= CCV(ccv, snd_ssthresh)) {
            d->state = LB_NORMAL;
            d->slow_start = 0;
            d->rtts_since_event = 0;
            d->next_rtt_seq = ccv->tp->snd_max;
        }
        goto end;
    }

    /* ---------------------------------------------------------
       STATE: NORMAL
       --------------------------------------------------------- */
    if (d->slow_start) {
        CCV(ccv, snd_cwnd) += (uint32_t)((mss));

        if (!d->initial_ss_done) {
            if (queue_delay > (int64_t)((3 * target) / 4)) {
                d->slow_start = 0;
                d->initial_ss_done = 1;
                CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
                /* First slowdown fires 2 RTTs after slow start exits */
                d->rtts_since_event = LEDBAT_PERIODIC_INTERVAL - 2;
                d->next_rtt_seq = ccv->tp->snd_max;
            }
        } else if (CCV(ccv, snd_cwnd) >= CCV(ccv, snd_ssthresh)) {
            d->slow_start = 0;
        }
    } else {
        if (queue_delay < (int64_t)target) {
            CCV(ccv, snd_cwnd) += (uint32_t)((gain * mss));
        } else {
	   int64_t add = ((int64_t)gain * mss) / SCALE;
	   int64_t sub = CONSTANT * (int64_t)CCV(ccv, snd_cwnd) * (queue_delay - (int64_t)target) / (int64_t)target;

           int64_t change = add - sub;
           change = MAX(change, -(int64_t)(CCV(ccv, snd_cwnd) / 2));

            CCV(ccv, snd_cwnd) = MAX((int64_t)CCV(ccv, snd_cwnd) + change, (int64_t)(LEDBAT_MIN_CWND_PKTS * mss));
        }
    }

    /* Track RTTs for Periodic 50-RTT Trigger */
    if (SEQ_GEQ(ack, d->next_rtt_seq)) {
        d->rtts_since_event++;
        d->next_rtt_seq = ccv->tp->snd_max;

        if (d->rtts_since_event >= LEDBAT_PERIODIC_INTERVAL) {
            d->state = LB_DRAINING;
            d->drain_rtt_count = 0;
            CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd); 
        }
    }

end:
    e_t->flags &= ~ERTT_NEW_MEASUREMENT;
}

static void
ledbatpp_cong_signal(struct cc_var *ccv, ccsignal_t type)
{
    struct ledbatpp *d = ccv->cc_data;
    if (d != NULL && type == CC_RTO) {
        d->slow_start = 1;
        d->state = LB_NORMAL; 
    }
    newreno_cc_cong_signal(ccv, type);
}

static void
ledbatpp_conn_init(struct cc_var *ccv)
{
    struct ledbatpp *d = ccv->cc_data;
    if (d == NULL) return;

    d->slow_start = 1;
    d->initial_ss_done = 0;
    d->state = LB_NORMAL;
    d->rtts_since_event = 0;
    d->next_rtt_seq = 0;
    d->min_rtt = 0;
    CCV(ccv, snd_cwnd) = LEDBAT_MIN_CWND_PKTS * tcp_fixed_maxseg(ccv->tp);
    printf("LEDBATPP_INIT: cwnd=%u ssthresh=%u\n",
           CCV(ccv, snd_cwnd), CCV(ccv, snd_ssthresh));
}

static int
ledbatpp_mod_init(void)
{
    ertt_id = khelp_get_id("ertt");
    return (ertt_id <= 0) ? ENOENT : 0;
}

struct cc_algo ledbatpp_cc_algo = {
    .name = "ledbatpp",
    .ack_received = ledbatpp_ack_received,
    .cb_destroy = ledbatpp_cb_destroy,
    .cb_init = ledbatpp_cb_init,
    .cong_signal = ledbatpp_cong_signal,
    .conn_init = ledbatpp_conn_init,
    .mod_init = ledbatpp_mod_init,
    .cc_data_sz = ledbatpp_data_sz,
};

DECLARE_CC_MODULE(ledbatpp, &ledbatpp_cc_algo);
MODULE_VERSION(ledbatpp, 1);
MODULE_DEPEND(ledbatpp, ertt, 1, 1, 1);