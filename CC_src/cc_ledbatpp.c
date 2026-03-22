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
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

static int32_t ertt_id;

/* =========================
   LEDBAT++ State
   ========================= */
struct ledbatpp {
    uint32_t ack_count;
    int slow_start;
};

/* =========================
   Target Delay (default 60ms from draft)
   ========================= */
VNET_DEFINE_STATIC(uint32_t, ledbatpp_target) = 60000;
#define V_ledbatpp_target VNET(ledbatpp_target)

/* =========================
   Compute Dynamic GAIN (Section 4.1)
   ========================= */
static double
compute_gain(uint32_t target, uint32_t base)
{
    if (base == 0)
        return 1.0;

    int val = (2 * target) / base;
    if ((2 * target) % base != 0)
        val++;

    if (val > 16)
        val = 16;
    if (val < 1)
        val = 1;

    return 1.0 / val;
}

/* =========================
   Init / Destroy
   ========================= */
static size_t
ledbatpp_data_sz(void)
{
    return sizeof(struct ledbatpp);
}

static int
ledbatpp_cb_init(struct cc_var *ccv, void *ptr)
{
    struct ledbatpp *d;

    if (ptr == NULL) {
        d = malloc(sizeof(struct ledbatpp), M_CC_MEM, M_NOWAIT);
        if (d == NULL)
            return ENOMEM;
    } else
        d = ptr;

    d->ack_count = 0;
    d->slow_start = 1;

    ccv->cc_data = d;
    return 0;
}

static void
ledbatpp_cb_destroy(struct cc_var *ccv)
{
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
    uint32_t mss;
    int64_t queue_delay;
    double gain;
    uint32_t target;

    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);
    d = ccv->cc_data;
    mss = tcp_fixed_maxseg(ccv->tp);
    target = V_ledbatpp_target;

    if (!(e_t->flags & ERTT_NEW_MEASUREMENT))
        return;

    if (e_t->minrtt && e_t->markedpkt_rtt) {

        /* Section 4.5: RTT-based delay */
        queue_delay = e_t->markedpkt_rtt - e_t->minrtt;

        gain = compute_gain(target, e_t->minrtt);

        /* =========================
           Section 4.3: Modified Slow Start
           ========================= */
        if (d->slow_start) {

            CCV(ccv, snd_cwnd) += gain * mss;

            /* Exit slow start if delay > 3/4 target */
            if (queue_delay > (3 * target) / 4) {
                d->slow_start = 0;
                CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
            }

        } else {

            /* =========================
               Section 4.2: AIMD
               ========================= */

            if (queue_delay < target) {

                /* Additive increase */
                CCV(ccv, snd_cwnd) += gain * mss;

            } else {

                /* Multiplicative decrease */
                double ratio = ((double)queue_delay / target) - 1.0;

                int32_t change =
                    gain * mss -
                    (CCV(ccv, snd_cwnd) * ratio);

                /* Cap decrease to W/2 */
                int32_t min_change = -(CCV(ccv, snd_cwnd) / 2);

                if (change < min_change)
                    change = min_change;

                CCV(ccv, snd_cwnd) += change;

                /* Minimum CWND = 2 MSS */
                if (CCV(ccv, snd_cwnd) < 2 * mss)
                    CCV(ccv, snd_cwnd) = 2 * mss;
            }
        }

        /* =========================
           Logging
           ========================= */
        d->ack_count++;
        if ((d->ack_count % 50) == 0) {
            struct timeval tv;
            microuptime(&tv);

            printf("LEDBATPP_TRACE,%jd.%06ld,%jd,%u\n",
                   (intmax_t)tv.tv_sec,
                   (long)tv.tv_usec,
                   (intmax_t)queue_delay,
                   CCV(ccv, snd_cwnd));
        }
    }

    e_t->flags &= ~ERTT_NEW_MEASUREMENT;
}

/* =========================
   Congestion Signals
   ========================= */
static void
ledbatpp_cong_signal(struct cc_var *ccv, ccsignal_t type)
{
    newreno_cc_cong_signal(ccv, type);
}

/* =========================
   Connection Init
   ========================= */
static void
ledbatpp_conn_init(struct cc_var *ccv)
{
    struct ledbatpp *d = ccv->cc_data;
    d->slow_start = 1;

    /* Initial CWND = 2 packets (Section 4.3) */
    CCV(ccv, snd_cwnd) = 2 * tcp_fixed_maxseg(ccv->tp);
}

/* =========================
   Module Init
   ========================= */
static int
ledbatpp_mod_init(void)
{
    ertt_id = khelp_get_id("ertt");
    if (ertt_id <= 0) {
        printf("ledbatpp: ertt module not found\n");
        return ENOENT;
    }
    return 0;
}

/* =========================
   Algorithm Registration
   ========================= */
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