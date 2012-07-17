/*
 * Copyright 2011 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Daniel Kopecek <dkopecek@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <seap.h>
#include <probe-api.h>

#include "common/debug_priv.h"
#include "../SEAP/generic/rbt/rbt.h"
#include "probe.h"
#include "worker.h"
#include "rcache.h"
#include "input_handler.h"

/*
 * The input handler waits for incomming eval requests and either returns
 * a result immediately if it is found in the result cache or spawns a new
 * worker thread which takes care of evaluating the request, caching the
 * result and sending it to the requestee.
 */
void *probe_input_handler(void *arg)
{
        pthread_attr_t pth_attr;
        probe_t       *probe = (probe_t *)arg;

        int probe_ret, ret, cstate; /* XXX */
        SEAP_msg_t *seap_request, *seap_reply;
        SEXP_t *probe_in, *probe_out, *oid;

#define TH_CANCEL_ON  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cstate)
#define TH_CANCEL_OFF pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cstate)

        TH_CANCEL_OFF;

        if (pthread_attr_init(&pth_attr))
                return (NULL);

        if (pthread_attr_setdetachstate(&pth_attr, PTHREAD_CREATE_DETACHED)) {
                pthread_attr_destroy(&pth_attr);
                return (NULL);
        }

        pthread_cleanup_push((void(*)(void *))pthread_attr_destroy, (void *)&pth_attr);
        
        switch (errno = pthread_barrier_wait(&OSCAP_GSYM(th_barrier)))
        {
        case 0:
        case PTHREAD_BARRIER_SERIAL_THREAD:
	        break;
        default:
	        dE("pthread_barrier_wait: %d, %s.\n",
	           errno, strerror(errno));
	        return (NULL);
        }

	while(1) {
                TH_CANCEL_ON;

		if (SEAP_recvmsg(probe->SEAP_ctx, probe->sd, &seap_request) == -1) {
			ret = errno;

			dE("An error ocured while receiving SEAP message. errno=%u, %s.\n", errno, strerror(errno));

                        /*
                         * TODO: check for abort request
                         */
			break;
		}

                TH_CANCEL_OFF;

		probe_in = SEAP_msg_get(seap_request);

		if (probe_in == NULL)
			abort();

		SEXP_VALIDATE(probe_in);

                /*
                 * Get a reference to the `id' attribute of the input object. The value
                 * of this attribute is the OVAL object ID and serves as a key in the
                 * result cache.
                 */
		oid = probe_obj_getattrval(probe_in, "id");
                SEXP_free(probe_in);

		if (oid != NULL) {
			SEXP_VALIDATE(oid);

			probe_out = probe_rcache_sexp_get(probe->rcache, oid);
                        SEXP_free(oid);

			if (probe_out == NULL) {
				/* cache miss */
                                probe_pwpair_t *pair;

                                pair = oscap_talloc(probe_pwpair_t);
                                pair->probe = probe;
                                pair->pth   = probe_worker_new();
                                pair->pth->sid = SEAP_msg_id(seap_request);
                                pair->pth->msg = seap_request;
                                pair->pth->msg_handler = &probe_worker;

                                if (rbt_i32_add(probe->workers, pair->pth->sid, pair->pth, NULL) != 0) {
                                        /*
                                         * Getting here means that there is already a
                                         * thread handling the message with the given
                                         * ID.
                                         */
                                        dW("Attempt to evaluate an object "
                                           "(ID=%u) " // TODO: 64b IDs
                                           "which is already being evaluated by an other thread.\n", pair->pth->sid);

                                        oscap_free(pair->pth);
                                        oscap_free(pair);
                                        SEAP_msg_free(seap_request);
                                } else {
                                        /* OK */

                                        if (pthread_create(&pair->pth->tid, &pth_attr, &probe_worker_runfn, pair))
                                        {
                                                dE("Cannot start a new worker thread: %d, %s.\n", errno, strerror(errno));

                                                if (rbt_i32_del(probe->workers, pair->pth->sid, NULL) != 0)
	                                                dE("rbt_i32_del: failed to remove worker thread (ID=%u)\n", pair->pth->sid);

                                                SEAP_msg_free(pair->pth->msg);
                                                oscap_free(pair->pth);
                                                oscap_free(pair);

                                                probe_ret = PROBE_EUNKNOWN;
                                                probe_out = NULL;

                                                goto __error_reply;
                                        }
                                }

				seap_request = NULL;
				continue;
			} else {
				/* cache hit */
				probe_ret = 0;
			}
		} else {
                        /* the `id' was not found in the input object */
                        dE("No `id' attribute\n");
                        probe_ret = PROBE_ENOATTR;
			probe_out = NULL;
                }

		if (probe_out == NULL || probe_ret != 0) {
		__error_reply:
			if (SEAP_replyerr(probe->SEAP_ctx, probe->sd, seap_request, probe_ret) == -1)
                        {
				dE("An error ocured while sending error status. errno=%u, %s.\n",
				   errno, strerror(errno));

				SEAP_msg_free(seap_request);

				break;
			}
		} else {
			SEXP_VALIDATE(probe_out);

			seap_reply = SEAP_msg_new();
			SEAP_msg_set(seap_reply, probe_out);
                        SEXP_free(probe_out);

			if (SEAP_reply(probe->SEAP_ctx, probe->sd, seap_reply, seap_request) == -1) {
				ret = errno;

				dE("An error ocured while sending SEAP message. errno=%u, %s.\n",
				   errno, strerror(errno));

                                /* TODO: check for abort request */
                                SEAP_msg_free(seap_reply);
                                SEAP_msg_free(seap_request);

                                break;
			}

			SEAP_msg_free(seap_reply);
		}

		SEAP_msg_free(seap_request);
	} /* main loop */

        pthread_cleanup_pop(1);
        return (NULL);
}
