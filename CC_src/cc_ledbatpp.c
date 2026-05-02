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

#define LEDBAT_MIN_CWND_PKTS 2
#define SCALE 1024
#define CONSTANT 1

struct ledbatpp {
    uint32_t ack_count;
    int slow_start;
    int initial_ss_done;
    uint32_t min_rtt;

    enum { LB_NORMAL, LB_DRAINING, LB_RECOVERING } state;
    uint32_t drain_rtt_count;
    tcp_seq  next_rtt_seq;
    uint32_t rtts_since_event;

    uint64_t slowdown_enter_ms;
    uint64_t slowdown_duration_ms;
    uint64_t next_slowdown_ts_ms;
    uint32_t last_ssthresh;
};

VNET_DEFINE_STATIC(uint32_t, ledbatpp_target) = 30;
#define V_ledbatpp_target VNET(ledbatpp_target)

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

static void
ledbatpp_ack_received(struct cc_var *ccv, ccsignal_t type)
{
    struct ertt *e_t;
    struct ledbatpp *d;
    uint32_t mss, target, gain, current_rtt;
    int64_t queue_delay;
    tcp_seq ack;
    struct timeval tv_now;
    uint16_t current_port;

    d = ccv->cc_data;
    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);

    if (d == NULL || e_t == NULL)
        return;

    mss = tcp_fixed_maxseg(ccv->tp);
    target = V_ledbatpp_target;
    ack = ccv->tp->snd_una;

    microuptime(&tv_now);
    uint64_t now_ms = (uint64_t)tv_now.tv_sec * 1000 + (tv_now.tv_usec / 1000);

    if (!(e_t->flags & ERTT_NEW_MEASUREMENT))
        return;

    current_rtt = (uint32_t)e_t->rtt;

    if (current_rtt > 0) {
        if (d->min_rtt == 0 || current_rtt < d->min_rtt)
            d->min_rtt = current_rtt;
    }

    if (d->min_rtt == 0 || current_rtt == 0)
        goto end;

    queue_delay = (int64_t)current_rtt - (int64_t)d->min_rtt;
    gain = compute_gain(target, d->min_rtt);

    /* --- GLOBAL TRIGGER: PERIODIC SLOWDOWN (HISTORY-AWARE) --- */
    if (d->next_slowdown_ts_ms != 0 && now_ms >= d->next_slowdown_ts_ms) {
        if (d->state == LB_NORMAL) {
            d->state = LB_DRAINING;
            d->drain_rtt_count = 0;
            d->slowdown_enter_ms = now_ms;
            d->next_rtt_seq = ccv->tp->snd_max;

            uint32_t current_cwnd = CCV(ccv, snd_cwnd);
            uint32_t floor = LEDBAT_MIN_CWND_PKTS * mss;

            /* Use last_ssthresh as a stability anchor */
            uint32_t anchor = (d->last_ssthresh > 0) ? d->last_ssthresh : current_cwnd;
            
            /* Safety: Ensure delay isn't negative for unsigned math */
            uint64_t safe_delay = (queue_delay > 0) ? (uint64_t)queue_delay : 0;
            
            /* Calculate reduction factor: anchor * (delay / (target + delay)) */
            uint64_t delay_factor = (safe_delay * 1024) / (target + (uint32_t)safe_delay);
            uint32_t reduction = (anchor * delay_factor) / 1024;

            CCV(ccv, snd_ssthresh) = current_cwnd;
            d->last_ssthresh = current_cwnd; 

            if (anchor > reduction + floor) {
                CCV(ccv, snd_cwnd) = anchor - reduction;
            } else {
                CCV(ccv, snd_cwnd) = floor;
            }
            goto log;
        }
    }

    if (d->next_rtt_seq == 0)
        d->next_rtt_seq = ccv->tp->snd_max;

log:
    current_port = ntohs(ccv->tp->t_inpcb.inp_fport);
    
    printf("LEDBATPP_TRACE,%jd.%06ld,%ld,%u,%u,%u,%u\n",
       (intmax_t)tv_now.tv_sec, (long)tv_now.tv_usec,
       (long)(queue_delay),
       CCV(ccv, snd_cwnd),
       d->min_rtt,
       current_rtt,
       (unsigned int)current_port);

    /* --- STATE: DRAINING --- */
    if (d->state == LB_DRAINING) {
        /* cwnd is maintained at the dynamic value set in the trigger above */
        if (SEQ_GEQ(ack, d->next_rtt_seq)) {
            d->drain_rtt_count++;
            d->next_rtt_seq = ccv->tp->snd_max;
        }
        if (d->drain_rtt_count >= 2) {
            d->state = LB_RECOVERING;
        }
        goto end;
    }

    /* --- STATE: RECOVERING --- */
    if (d->state == LB_RECOVERING) {
        if (SEQ_GEQ(ack, d->next_rtt_seq)) {
            uint32_t incr = 2 * gain * mss / SCALE;
            if (incr == 0) incr = 1;
            CCV(ccv, snd_cwnd) = MIN(CCV(ccv, snd_cwnd) + incr, CCV(ccv, snd_ssthresh));
            d->next_rtt_seq = ccv->tp->snd_max;

            if (CCV(ccv, snd_cwnd) >= CCV(ccv, snd_ssthresh)) {
                uint64_t duration = now_ms - d->slowdown_enter_ms;
                d->next_slowdown_ts_ms = now_ms + (9 * duration);
                d->state = LB_NORMAL;
                d->slow_start = 0;
                d->next_rtt_seq = ccv->tp->snd_max;
            }
        }
        goto end;
    }

    /* --- STATE: NORMAL / INITIAL SS --- */
    if (d->slow_start) {
        CCV(ccv, snd_cwnd) += (mss);
        if (!d->initial_ss_done) {
            if (queue_delay > (int64_t)((3 * target) / 4)) {
                printf("LEDBAT_DEBUG: Initial SS Done. Delay: %ld\n", (long)queue_delay);
                d->initial_ss_done = 1;
                d->slow_start = 0;
                d->rtts_since_event = 0;
                CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
                d->next_rtt_seq = ccv->tp->snd_max;
            }
        }
    } else {
        if (queue_delay < (int64_t)target) {
            uint32_t incr = (gain * mss * mss) / (SCALE * CCV(ccv, snd_cwnd));
            if (incr == 0) incr = 1;
            CCV(ccv, snd_cwnd) += incr;
        } else {
            int64_t change = (int64_t)gain * mss / SCALE
                             - CONSTANT * (int64_t)CCV(ccv, snd_cwnd) * (queue_delay - (int64_t)target) / (int64_t)target;
            if (change < -(int64_t)(CCV(ccv, snd_cwnd) / 2))
                change = -(int64_t)(CCV(ccv, snd_cwnd) / 2);
            CCV(ccv, snd_cwnd) = MAX((int64_t)CCV(ccv, snd_cwnd) + change,
                                     (int64_t)(LEDBAT_MIN_CWND_PKTS * mss));
        }
    }

    /* Handle Initial Slowdown trigger */
    if (SEQ_GEQ(ack, d->next_rtt_seq)) {
        if (d->initial_ss_done && d->next_slowdown_ts_ms == 0 && d->state == LB_NORMAL) {
            d->rtts_since_event++;
            if (d->rtts_since_event >= 2) {
                printf("LEDBAT_DEBUG: Triggering Initial Slowdown cwnd=%u gain=%u\n",
                       CCV(ccv, snd_cwnd), gain);
                d->state = LB_DRAINING;
                d->drain_rtt_count = 0;
                d->slowdown_enter_ms = now_ms;
                d->next_rtt_seq = ccv->tp->snd_max;
                CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
                d->last_ssthresh = CCV(ccv, snd_cwnd);
            }
        }
        d->next_rtt_seq = ccv->tp->snd_max;
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
        d->initial_ss_done = 0;
        d->next_slowdown_ts_ms = 0;
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
    d->next_slowdown_ts_ms = 0;
    d->last_ssthresh = 0;

    CCV(ccv, snd_cwnd) = LEDBAT_MIN_CWND_PKTS * tcp_fixed_maxseg(ccv->tp);
}

static int
ledbatpp_mod_init(void)
{
    ertt_id = khelp_get_id("ertt");
    return (ertt_id <= 0) ? ENOENT : 0;
}

struct cc_algo ledbatpp_cc_algo = {
    .name         = "ledbatpp",
    .ack_received = ledbatpp_ack_received,
    .cb_destroy   = ledbatpp_cb_destroy,
    .cb_init      = ledbatpp_cb_init,
    .cong_signal  = ledbatpp_cong_signal,
    .conn_init    = ledbatpp_conn_init,
    .mod_init     = ledbatpp_mod_init,
    .cc_data_sz   = ledbatpp_data_sz,
};

DECLARE_CC_MODULE(ledbatpp, &ledbatpp_cc_algo);
MODULE_VERSION(ledbatpp, 1);
MODULE_DEPEND(ledbatpp, ertt, 1, 1, 1);