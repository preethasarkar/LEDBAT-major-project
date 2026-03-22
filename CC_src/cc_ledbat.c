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

#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

static void ledbat_ack_received(struct cc_var *ccv, ccsignal_t ack_type);
static void ledbat_cb_destroy(struct cc_var *ccv);
static int  ledbat_cb_init(struct cc_var *ccv, void *ptr);
static void ledbat_cong_signal(struct cc_var *ccv, ccsignal_t signal_type);
static void ledbat_conn_init(struct cc_var *ccv);
static int  ledbat_mod_init(void);
static size_t ledbat_data_sz(void);

struct ledbat {
    int slow_start_toggle;
    uint32_t ack_count;
};

static int32_t ertt_id;


VNET_DEFINE_STATIC(uint32_t, ledbat_target) = 50;
#define V_ledbat_target VNET(ledbat_target)

struct cc_algo ledbat_cc_algo = {
    .name = "ledbat",
    .ack_received = ledbat_ack_received,
    .cb_destroy = ledbat_cb_destroy,
    .cb_init = ledbat_cb_init,
    .cong_signal = ledbat_cong_signal,
    .conn_init = ledbat_conn_init,
    .mod_init = ledbat_mod_init,
    .cc_data_sz = ledbat_data_sz,
    .after_idle = newreno_cc_after_idle,
    .post_recovery = newreno_cc_post_recovery,
};

static void
ledbat_ack_received(struct cc_var *ccv, ccsignal_t ack_type)
{
    struct ertt *e_t;
    struct ledbat *ledbat_data;
    int64_t queue_delay, offset, cwnd_change;
    uint32_t mss = tcp_fixed_maxseg(ccv->tp);

    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);
    ledbat_data = ccv->cc_data;

    if (e_t->flags & ERTT_NEW_MEASUREMENT) { 
        if (e_t->minrtt && e_t->markedpkt_rtt) {
            

            queue_delay = e_t->markedpkt_rtt - e_t->minrtt;


            if (queue_delay < 0) {
                queue_delay = 0;
            }
            

            offset = (int64_t)V_ledbat_target - queue_delay;
            cwnd_change = (offset * (int64_t)mss) / (int64_t)V_ledbat_target;


            struct timeval tv;
            microuptime(&tv);
            printf("LEDBAT_TRACE,%jd.%06ld,%jd,%u\n",
                   (intmax_t)tv.tv_sec, (long)tv.tv_usec,
                   (intmax_t)queue_delay, CCV(ccv, snd_cwnd));

            if (ledbat_data->slow_start_toggle == 1) {
                if (CCV(ccv, snd_cwnd) > CCV(ccv, snd_ssthresh)) {
                    /* We crossed the threshold. Lock the door. */
                    ledbat_data->slow_start_toggle = 0; 
                }
            }

            if (ledbat_data->slow_start_toggle == 0) {
                if (cwnd_change >= 0) {
                    /* Increase CWND, bounded by MAXWIN */
                    CCV(ccv, snd_cwnd) = min(CCV(ccv, snd_cwnd) + (uint32_t)cwnd_change,
                                         TCP_MAXWIN << CCV(ccv, snd_scale));
                } else {
                    /* Decrease CWND, floor at 2 * MSS */
                    uint32_t decrease = (uint32_t)(-cwnd_change);
                    if (CCV(ccv, snd_cwnd) > (2 * mss + decrease))
                        CCV(ccv, snd_cwnd) -= decrease;
                    else
                        CCV(ccv, snd_cwnd) = 2 * mss;
                }
            }
        }
        e_t->flags &= ~ERTT_NEW_MEASUREMENT;
    }

    if (ledbat_data->slow_start_toggle)
        newreno_cc_ack_received(ccv, ack_type);
}

static void
ledbat_cb_destroy(struct cc_var *ccv)
{
    free(ccv->cc_data, M_CC_MEM);
}

static size_t
ledbat_data_sz(void)
{
    return (sizeof(struct ledbat));
}

static int
ledbat_cb_init(struct cc_var *ccv, void *ptr)
{
    struct ledbat *ledbat_data;

    INP_WLOCK_ASSERT(tptoinpcb(ccv->tp));
    if (ptr == NULL) {
        ledbat_data = malloc(sizeof(struct ledbat), M_CC_MEM, M_NOWAIT);
        if (ledbat_data == NULL)
            return (ENOMEM);
    } else
        ledbat_data = ptr;

    ledbat_data->slow_start_toggle = 1;
    ledbat_data->ack_count = 0;
    ccv->cc_data = ledbat_data;

    return (0);
}

static void
ledbat_cong_signal(struct cc_var *ccv, ccsignal_t signal_type)
{
    struct ledbat *ledbat_data;
    int presignalrecov;

    ledbat_data = ccv->cc_data;

    if (IN_RECOVERY(CCV(ccv, t_flags)))
        presignalrecov = 1;
    else
        presignalrecov = 0;

    /* Hand-off standard congestion events (loss, ECN) to NewReno */
    newreno_cc_cong_signal(ccv, signal_type);

    if (IN_RECOVERY(CCV(ccv, t_flags)) && !presignalrecov)
        ledbat_data->slow_start_toggle =
            (CCV(ccv, snd_cwnd) < CCV(ccv, snd_ssthresh)) ? 1 : 0;
}

static void
ledbat_conn_init(struct cc_var *ccv)
{
    struct ledbat *ledbat_data;

    ledbat_data = ccv->cc_data;
    ledbat_data->slow_start_toggle = 1;
}

static int
ledbat_mod_init(void)
{
    ertt_id = khelp_get_id("ertt");
    if (ertt_id <= 0) {
        printf("%s: h_ertt module not found\n", __func__);
        return (ENOENT);
    }
    return (0);
}

static int
ledbat_target_handler(SYSCTL_HANDLER_ARGS)
{
    int error;
    uint32_t new;

    new = V_ledbat_target;
    error = sysctl_handle_int(oidp, &new, 0, req);
    if (error == 0 && req->newptr != NULL) {
        if (new == 0)
            error = EINVAL;
        else
            V_ledbat_target = new;
    }

    return (error);
}

SYSCTL_DECL(_net_inet_tcp_cc_ledbat);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, ledbat,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL,
    "LEDBAT related settings");

SYSCTL_PROC(_net_inet_tcp_cc_ledbat, OID_AUTO, target,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
    &VNET_NAME(ledbat_target), 50, &ledbat_target_handler, "IU",
    "LEDBAT target queueing delay in microseconds");

DECLARE_CC_MODULE(ledbat, &ledbat_cc_algo);
MODULE_VERSION(ledbat, 1);
MODULE_DEPEND(ledbat, ertt, 1, 1, 1);