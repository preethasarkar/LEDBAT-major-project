#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <net/vnet.h>

#include <net/route.h>
#include <net/route/nhop.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

static void rledbat_ack_received(struct cc_var *ccv, ccsignal_t ack_type);
static void rledbat_cb_destroy(struct cc_var *ccv);
static int  rledbat_cb_init(struct cc_var *ccv, void *ptr);
static void rledbat_cong_signal(struct cc_var *ccv, ccsignal_t signal_type);
static void rledbat_conn_init(struct cc_var *ccv);
static int  rledbat_mod_init(void);
static size_t rledbat_data_sz(void);

struct rledbat {
    int slow_start_toggle;
    uint32_t ack_count;
};

static int32_t ertt_id;

/* RFC 9840 default target queue delay (100 ms) */
VNET_DEFINE_STATIC(uint32_t, rledbat_target) = 100000;
#define V_rledbat_target VNET(rledbat_target)

static uint32_t rledbat_gain = 1;

struct cc_algo rledbat_cc_algo = {
    .name = "rledbat",
    .ack_received = rledbat_ack_received,
    .cb_destroy = rledbat_cb_destroy,
    .cb_init = rledbat_cb_init,
    .cong_signal = rledbat_cong_signal,
    .conn_init = rledbat_conn_init,
    .mod_init = rledbat_mod_init,
    .cc_data_sz = rledbat_data_sz,
    .after_idle = newreno_cc_after_idle,
    .post_recovery = newreno_cc_post_recovery,
};

static void
rledbat_ack_received(struct cc_var *ccv, ccsignal_t ack_type)
{
    struct ertt *e_t;
    struct rledbat *data;
    int64_t queue_delay, offset, cwnd_change;
    uint32_t mss = tcp_fixed_maxseg(ccv->tp);

    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);
    data = ccv->cc_data;

    if (e_t->flags & ERTT_NEW_MEASUREMENT) {

        if (e_t->minrtt && e_t->markedpkt_rtt) {

            /* Queue delay = RTT − base RTT */
            queue_delay = e_t->markedpkt_rtt - e_t->minrtt;

            /* Offset from target delay */
            offset = (int64_t)V_rledbat_target - queue_delay;

            /*
             * RFC 9840 window update rule
             *
             * cwnd += gain * (offset / target) * MSS
             */
            cwnd_change =
                (rledbat_gain * offset * mss) / V_rledbat_target;

            data->ack_count++;

            if ((data->ack_count % 50) == 0) {
                struct timeval tv;
                microuptime(&tv);

                printf("RLEDBAT_TRACE,%jd.%06ld,%jd,%u\n",
                       (intmax_t)tv.tv_sec,
                       (long)tv.tv_usec,
                       (intmax_t)queue_delay,
                       CCV(ccv, snd_cwnd));
            }

            if (CCV(ccv, snd_cwnd) <= CCV(ccv, snd_ssthresh)) {

                data->slow_start_toggle =
                    data->slow_start_toggle ? 0 : 1;

            } else {

                data->slow_start_toggle = 0;

                if (cwnd_change >= 0) {

                    CCV(ccv, snd_cwnd) =
                        min(CCV(ccv, snd_cwnd) + cwnd_change,
                            TCP_MAXWIN << CCV(ccv, snd_scale));

                } else {

                    uint32_t decrease = (uint32_t)(-cwnd_change);

                    CCV(ccv, snd_cwnd) =
                        max(2 * mss,
                            CCV(ccv, snd_cwnd) - decrease);
                }
            }
        }

        e_t->flags &= ~ERTT_NEW_MEASUREMENT;
    }

    if (data->slow_start_toggle)
        newreno_cc_ack_received(ccv, ack_type);
}

static void
rledbat_cb_destroy(struct cc_var *ccv)
{
    free(ccv->cc_data, M_CC_MEM);
}

static size_t
rledbat_data_sz(void)
{
    return sizeof(struct rledbat);
}

static int
rledbat_cb_init(struct cc_var *ccv, void *ptr)
{
    struct rledbat *data;

    INP_WLOCK_ASSERT(tptoinpcb(ccv->tp));

    if (ptr == NULL) {
        data = malloc(sizeof(struct rledbat),
                      M_CC_MEM, M_NOWAIT);

        if (data == NULL)
            return ENOMEM;
    } else
        data = ptr;

    data->slow_start_toggle = 1;
    data->ack_count = 0;

    ccv->cc_data = data;

    return 0;
}

static void
rledbat_cong_signal(struct cc_var *ccv, ccsignal_t signal_type)
{
    struct rledbat *data;
    int presignalrecov;

    data = ccv->cc_data;

    presignalrecov =
        IN_RECOVERY(CCV(ccv, t_flags)) ? 1 : 0;

    newreno_cc_cong_signal(ccv, signal_type);

    if (IN_RECOVERY(CCV(ccv, t_flags)) && !presignalrecov)
        data->slow_start_toggle =
            (CCV(ccv, snd_cwnd) <
             CCV(ccv, snd_ssthresh));
}

static void
rledbat_conn_init(struct cc_var *ccv)
{
    struct rledbat *data;

    data = ccv->cc_data;
    data->slow_start_toggle = 1;
}

static int
rledbat_mod_init(void)
{
    ertt_id = khelp_get_id("ertt");

    if (ertt_id <= 0) {
        printf("%s: h_ertt module not found\n",
               __func__);
        return ENOENT;
    }

    return 0;
}

SYSCTL_DECL(_net_inet_tcp_cc_rledbat);

SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, rledbat,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL,
    "RLEDBAT settings");

SYSCTL_UINT(_net_inet_tcp_cc_rledbat, OID_AUTO, target,
    CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(rledbat_target), 100000,
    "Target queue delay (microseconds)");

DECLARE_CC_MODULE(rledbat, &rledbat_cc_algo);
MODULE_VERSION(rledbat, 1);
MODULE_DEPEND(rledbat, ertt, 1, 1, 1);