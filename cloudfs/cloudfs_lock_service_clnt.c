/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include <memory.h> /* for memset */
#include "cloudfs_lock_service.h"

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

enum clnt_stat 
acquire_1(lock_params *argp, status *clnt_res, CLIENT *clnt)
{
	return (clnt_call(clnt, ACQUIRE,
		(xdrproc_t) xdr_lock_params, (caddr_t) argp,
		(xdrproc_t) xdr_status, (caddr_t) clnt_res,
		TIMEOUT));
}

enum clnt_stat 
release_1(lock_params *argp, status *clnt_res, CLIENT *clnt)
{
	return (clnt_call(clnt, RELEASE,
		(xdrproc_t) xdr_lock_params, (caddr_t) argp,
		(xdrproc_t) xdr_status, (caddr_t) clnt_res,
		TIMEOUT));
}
