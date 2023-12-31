/*
   Copyright (C) 2010,2011 bg <bg_one@mail.ru>
*/
#include "ast_config.h"

#include <unistd.h>
#include <fcntl.h>

#include <asterisk/utils.h>

#include "cpvt.h"
#include "chan_quectel.h"			/* struct pvt */
#include "at_queue.h"				/* struct at_queue_task */
#include "mutils.h"				/* ITEMS_OF() */

#/* return 0 on success */
// TODO: move to activation time, save resouces
static int init_pipe(int filedes[2])
{
	int x;
	int rv;
	int flags;

	rv = pipe(filedes);
	if(rv == 0) {
		for(x = 0; x < 2; ++x) {
			rv = fcntl(filedes[x], F_GETFL);
			flags = fcntl(filedes[x], F_GETFD);
			if(rv == -1 || flags == -1 || (rv = fcntl(filedes[x], F_SETFL, O_NONBLOCK | rv)) == -1 || (rv = fcntl(filedes[x], F_SETFD, flags | FD_CLOEXEC)) == -1)
				goto bad;
			}
		return 0;
bad:
		close(filedes[0]);
		close(filedes[1]);
	}
	return rv;
}

#/* */
EXPORT_DEF struct cpvt * cpvt_alloc(struct pvt * pvt, int call_idx, unsigned dir, call_state_t state)
{
	int filedes[2];
	struct cpvt * cpvt = NULL;

	if(init_pipe(filedes) == 0)
	{
		cpvt = ast_calloc (1, sizeof (*cpvt));
		if(cpvt)
		{
			cpvt->pvt = pvt;
			cpvt->call_idx = call_idx;
			cpvt->state = state;
			cpvt->dir = dir;
			cpvt->rd_pipe[0] = filedes[0];
			cpvt->rd_pipe[1] = filedes[1];

//			rb_init (&cpvt->a_write_rb, cpvt->a_write_buf, sizeof (cpvt->a_write_buf));

			AST_LIST_INSERT_TAIL(&pvt->chans, cpvt, entry);
			if(PVT_NO_CHANS(pvt))
				pvt_on_create_1st_channel(pvt);
			PVT_STATE(pvt, chansno)++;
			PVT_STATE(pvt, chan_count[cpvt->state])++;



			ast_debug (3, "[%s] create cpvt for call_idx %d dir %d state '%s'\n",  PVT_ID(pvt), call_idx, dir, call_state2str(state));
			return cpvt;
		}
		close(filedes[0]);
		close(filedes[1]);
	}

	return cpvt;
}

#/* */
EXPORT_DEF void cpvt_free(struct cpvt* cpvt)
{
	pvt_t * pvt = cpvt->pvt;
	struct cpvt * found;
	struct at_queue_task * task;

	close(cpvt->rd_pipe[1]);
	close(cpvt->rd_pipe[0]);


	ast_debug (3, "[%s] destroy cpvt for call_idx %d dir %d state '%s' flags %d has%s channel\n",  PVT_ID(pvt), cpvt->call_idx, cpvt->dir, call_state2str(cpvt->state), cpvt->flags, cpvt->channel ? "" : "'t");
	AST_LIST_TRAVERSE_SAFE_BEGIN(&pvt->chans, found, entry) {
		if(found == cpvt)
		{
			AST_LIST_REMOVE_CURRENT(entry);
			PVT_STATE(pvt, chan_count[cpvt->state])--;
			PVT_STATE(pvt, chansno) --;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* relink task to sys_chan */
	AST_LIST_TRAVERSE(&pvt->at_queue, task, entry) {
		if(task->cpvt == cpvt)
		{
			task->cpvt = &pvt->sys_chan;
		}
	}
	/* drop last_dialed_cpvt if need */
	if(pvt->last_dialed_cpvt == cpvt)
		pvt->last_dialed_cpvt = NULL;

	if(PVT_NO_CHANS(pvt)) {
		pvt_on_remove_last_channel(pvt);
		pvt_try_restate(pvt);
		}

	ast_free(cpvt);
}

#/* */
EXPORT_DEF struct cpvt * pvt_find_cpvt(struct pvt * pvt, int call_idx)
{
	struct cpvt * cpvt;
	AST_LIST_TRAVERSE(&pvt->chans, cpvt, entry) {
		if(call_idx == cpvt->call_idx)
			return cpvt;
	}

	return 0;
}

EXPORT_DEF struct cpvt * active_cpvt(struct pvt * pvt)
{
	struct cpvt * cpvt;
	AST_LIST_TRAVERSE(&pvt->chans, cpvt, entry) {
		if(CPVT_IS_SOUND_SOURCE(cpvt) || (cpvt)->state == CALL_STATE_INCOMING)
			return cpvt;
	}

	return 0;
}

EXPORT_DEF void voice_enable(struct pvt * pvt)
{
                                static const char cmd_atvoice[] = "AT+CPCMREG=1\r";
                                static const at_queue_cmd_t cmds1[] = {
		                ATQ_CMD_DECLARE_STIT(CMD_AT_DDSETEX, cmd_atvoice, ATQ_CMD_TIMEOUT_MEDIUM, 0),
		                                                       }; 
	                        at_queue_insert_const(&pvt->sys_chan, cmds1, ITEMS_OF(cmds1), 1);  

}

EXPORT_DEF void voice_disable(struct pvt * pvt)
{

                                static const char cmd_atvoice[] = "AT+CPCMREG=0\r";
                                static const at_queue_cmd_t cmds1[] = {
		                ATQ_CMD_DECLARE_STIT(CMD_AT_DDSETEX0, cmd_atvoice, ATQ_CMD_TIMEOUT_MEDIUM, 0),
		                                                       };
	                        at_queue_insert_const(&pvt->sys_chan, cmds1, ITEMS_OF(cmds1), 1);  

}

#/* */
EXPORT_DEF const char * pvt_call_dir(const struct pvt * pvt)
{
	static const char * dirs[] = {
		"Active",
		"Outgoing",
		"Incoming",
		"Both"
	};

	int index = 0;
	struct cpvt * cpvt;
	AST_LIST_TRAVERSE(&pvt->chans, cpvt, entry) {
		if(cpvt->dir == CALL_DIR_OUTGOING)
			index |= 0x1;
		else
			index |= 0x2;
	}

	return dirs[index];
}
