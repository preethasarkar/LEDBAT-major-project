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

struct ledbatpp {
    int slow_start_toggle;
    uint32_t ack_count;
    int ticks_last_drain; 
};

VNET_DEFINE_STATIC(uint32_t, ledbatpp_target) = 60;
#define V_ledbatpp_target VNET(ledbatpp_target)

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

static size_t
ledbatpp_data_sz(void)
{
    return sizeof(struct ledbatpp);
}

static int
ledbatpp_cb_init(struct cc_var *ccv, void *ptr)
{
    struct ledbatpp *ledbatpp_data;

    if (ptr == NULL) {
        ledbatpp_data = malloc(sizeof(struct ledbatpp), M_CC_MEM, M_NOWAIT);
        if (ledbatpp_data == NULL)
            return ENOMEM;
    } else
        ledbatpp_data = ptr;

    ledbatpp_data->ack_count = 0;
    ledbatpp_data->slow_start_toggle = 1;
    ledbatpp_data->ticks_last_drain = ticks; /* Initialize the timer */

    ccv->cc_data = ledbatpp_data;
    return 0;
}

static void
ledbatpp_cb_destroy(struct cc_var *ccv)
{
    free(ccv->cc_data, M_CC_MEM);
}

static void
ledbatpp_ack_received(struct cc_var *ccv, ccsignal_t ack_type)
{
    struct ertt *e_t;
    struct ledbatpp *ledbatpp_data;
    uint32_t mss;
    int64_t queue_delay;
    double gain;
    uint32_t target;
    uint32_t half_cwnd;

    e_t = khelp_get_osd(&CCV(ccv, t_osd), ertt_id);
    ledbatpp_data = ccv->cc_data;
    mss = tcp_fixed_maxseg(ccv->tp);
    target = V_ledbatpp_target;

    if ((ticks - ledbatpp_data->ticks_last_drain) > (hz * 2)) {
        
        half_cwnd = CCV(ccv, snd_cwnd) / 2;
        
        /* Halve CWND, floor at 2 * MSS */
        if (half_cwnd < 2 * mss) {
            CCV(ccv, snd_cwnd) = 2 * mss;
        } else {
            CCV(ccv, snd_cwnd) = half_cwnd;
        }
        
        ledbatpp_data->ticks_last_drain = ticks;
    }

    if (!(e_t->flags & ERTT_NEW_MEASUREMENT))
        return;

    if (e_t->minrtt && e_t->markedpkt_rtt) {

        queue_delay = e_t->markedpkt_rtt - e_t->minrtt;
        gain = compute_gain(target, e_t->minrtt);

        if (ledbatpp_data->slow_start_toggle) {

            CCV(ccv, snd_cwnd) += gain * mss;

            if (queue_delay > (3 * target) / 4) {
                ledbatpp_data->slow_start_toggle = 0;
                CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
            }

        } else {

            if (queue_delay < target) {
                CCV(ccv, snd_cwnd) += gain * mss;
            } else {

                double ratio = ((double)queue_delay / target) - 1.0;

                int32_t change =
                    gain * mss -
                    (CCV(ccv, snd_cwnd) * ratio);

                int32_t min_change = -(CCV(ccv, snd_cwnd) / 2);

                if (change < min_change)
                    change = min_change;

                CCV(ccv, snd_cwnd) += change;

                if (CCV(ccv, snd_cwnd) < 2 * mss)
                    CCV(ccv, snd_cwnd) = 2 * mss;
            }
        }

        ledbatpp_data->ack_count++;

        struct timeval tv;
        microuptime(&tv);

            printf("LEDBATPP_TRACE,%jd.%06ld,%jd,%u\n",
                   (intmax_t)tv.tv_sec,
                   (long)tv.tv_usec,
                   (intmax_t)queue_delay,
                   CCV(ccv, snd_cwnd));
        
    }

    e_t->flags &= ~ERTT_NEW_MEASUREMENT;
}

static void
ledbatpp_cong_signal(struct cc_var *ccv, ccsignal_t signal_type)
{
    newreno_cc_cong_signal(ccv, signal_type);
}

static void
ledbatpp_conn_init(struct cc_var *ccv)
{
    struct ledbatpp *ledbatpp_data = ccv->cc_data;
    ledbatpp_data->slow_start_toggle = 1;

    CCV(ccv, snd_cwnd) = 2 * tcp_fixed_maxseg(ccv->tp);
}

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

static int
ledbatpp_target_handler(SYSCTL_HANDLER_ARGS)
{
    int error;
    uint32_t new;

    new = V_ledbatpp_target;
    error = sysctl_handle_int(oidp, &new, 0, req);
    if (error == 0 && req->newptr != NULL) {
        if (new == 0)
            error = EINVAL;
        else
            V_ledbatpp_target = new;
    }

    return (error);
}

SYSCTL_DECL(_net_inet_tcp_cc_ledbatpp);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, ledbatpp,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL,
    "LEDBAT++ related settings");

SYSCTL_PROC(_net_inet_tcp_cc_ledbatpp, OID_AUTO, target,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
    &VNET_NAME(ledbatpp_target), 60, &ledbatpp_target_handler, "IU",
    "LEDBAT++ target queueing delay");

DECLARE_CC_MODULE(ledbatpp, &ledbatpp_cc_algo);
MODULE_VERSION(ledbatpp, 1);
MODULE_DEPEND(ledbatpp, ertt, 1, 1, 1);