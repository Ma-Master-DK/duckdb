#pragma once
#include <libxnvme.h>

namespace duckdb {
extern thread_local xnvme_queue *queue_ptr;

static void cb_fn(struct xnvme_cmd_ctx *ctx, void *XNVME_UNUSED(cb_arg)) {
	if (xnvme_cmd_ctx_cpl_status(ctx)) {
		xnvme_cli_pinf("Command did not complete successfully");
		xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
	} else {
		// xnvme_cli_pinf("Command completed succesfully");
	}

	// Completed: Put the command-context back in the queue
	xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
}
} // namespace duckdb
