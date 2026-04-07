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

#define LEDBAT_GAIN 5
#define LEDBAT_TARGET V_ledbat_target
#define LEDBAT_MIN_CWND 2
#define LEDBAT_ALLOWED_INCREASE 1

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
    int64_t queue_delay, off_target;
    int64_t cwnd_inc;
    uint32_t mss = tcp_fixed_maxseg(ccv->tp);
    uint32_t bytes_newly_acked = ccv->bytes_this_ack;

    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);
    ledbat_data = ccv->cc_data;
 
    if (ledbat_data->slow_start_toggle) {
        CCV(ccv, snd_cwnd) += mss;
        if (CCV(ccv, snd_cwnd) > CCV(ccv, snd_ssthresh)) {
            ledbat_data->slow_start_toggle = 0;
        }
	queue_delay = e_t->markedpkt_rtt - e_t->minrtt;

        if (queue_delay < 0) {
        	queue_delay = 0;
        }

	struct timeval tv;
            microuptime(&tv);
            printf("LEDBAT_TRACE,%jd.%06ld,%jd,%u\n",
                   (intmax_t)tv.tv_sec, (long)tv.tv_usec,
                   (intmax_t)queue_delay, CCV(ccv, snd_cwnd));
        return; 
    }
    if (e_t->flags & ERTT_NEW_MEASUREMENT) { 
        if (e_t->minrtt && e_t->markedpkt_rtt) {
            
            queue_delay = e_t->markedpkt_rtt - e_t->minrtt;

            if (queue_delay < 0) {
                queue_delay = 0;
            }

            off_target = (int64_t)V_ledbat_target - queue_delay;
            cwnd_inc = (LEDBAT_GAIN * off_target * (int64_t)bytes_newly_acked * (int64_t)mss) /  ((int64_t)V_ledbat_target * (int64_t)CCV(ccv, snd_cwnd));

	    int64_t new_cwnd = (int64_t)CCV(ccv, snd_cwnd) + cwnd_inc;
            uint32_t flightsize = CCV(ccv, snd_max) - CCV(ccv, snd_una);
            uint32_t max_allowed = flightsize + (LEDBAT_ALLOWED_INCREASE * mss);
            
            if (new_cwnd > max_allowed) {
                new_cwnd = max_allowed;
            }

            if (new_cwnd < (LEDBAT_MIN_CWND * mss)) {
                new_cwnd = (LEDBAT_MIN_CWND * mss);
            }

            CCV(ccv, snd_cwnd) = (uint32_t)new_cwnd;
            
            struct timeval tv;
            microuptime(&tv);
            printf("LEDBAT_TRACE,%jd.%06ld,%jd,%u\n",
                   (intmax_t)tv.tv_sec, (long)tv.tv_usec,
                   (intmax_t)queue_delay, CCV(ccv, snd_cwnd));

        }
        e_t->flags &= ~ERTT_NEW_MEASUREMENT;
    }

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
    &VNET_NAME(ledbat_target), 100, &ledbat_target_handler, "IU",
    "LEDBAT target queueing delay");

DECLARE_CC_MODULE(ledbat, &ledbat_cc_algo);
MODULE_VERSION(ledbat, 1);
MODULE_DEPEND(ledbat, ertt, 1, 1, 1);