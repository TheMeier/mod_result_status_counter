/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *  mod_result_status_counter -- Apache module for result status statistics
 *
 * This module counts the result status of all requests in shared memory and
 * sets up a handler where the counter can be fetched in json.
 * Most of the code ist copied from
 * http://svn.apache.org/repos/asf/httpd/httpd/trunk/modules/examples/mod_example_ipc.c
 */

#include "apr.h"
#include "apr_strings.h"

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "apr_global_mutex.h"
#include "ap_config.h"

#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

static const char *const status_lines[RESPONSE_CODES] =
    {
        "100 Continue",
        "101 Switching Protocols",
        "102 Processing",
#define LEVEL_200 3
        "200 OK",
        "201 Created",
        "202 Accepted",
        "203 Non-Authoritative Information",
        "204 No Content",
        "205 Reset Content",
        "206 Partial Content",
        "207 Multi-Status",
        "208 Already Reported",
        NULL, /* 209 */
        NULL, /* 210 */
        NULL, /* 211 */
        NULL, /* 212 */
        NULL, /* 213 */
        NULL, /* 214 */
        NULL, /* 215 */
        NULL, /* 216 */
        NULL, /* 217 */
        NULL, /* 218 */
        NULL, /* 219 */
        NULL, /* 220 */
        NULL, /* 221 */
        NULL, /* 222 */
        NULL, /* 223 */
        NULL, /* 224 */
        NULL, /* 225 */
        "226 IM Used",
#define LEVEL_300 30
        "300 Multiple Choices",
        "301 Moved Permanently",
        "302 Found",
        "303 See Other",
        "304 Not Modified",
        "305 Use Proxy",
        NULL, /* 306 */
        "307 Temporary Redirect",
        "308 Permanent Redirect",
#define LEVEL_400 39
        "400 Bad Request",
        "401 Unauthorized",
        "402 Payment Required",
        "403 Forbidden",
        "404 Not Found",
        "405 Method Not Allowed",
        "406 Not Acceptable",
        "407 Proxy Authentication Required",
        "408 Request Timeout",
        "409 Conflict",
        "410 Gone",
        "411 Length Required",
        "412 Precondition Failed",
        "413 Request Entity Too Large",
        "414 Request-URI Too Long",
        "415 Unsupported Media Type",
        "416 Requested Range Not Satisfiable",
        "417 Expectation Failed",
        NULL, /* 418 */
        NULL, /* 419 */
        NULL, /* 420 */
        NULL, /* 421 */
        "422 Unprocessable Entity",
        "423 Locked",
        "424 Failed Dependency",
        NULL, /* 425 */
        "426 Upgrade Required",
        NULL, /* 427 */
        "428 Precondition Required",
        "429 Too Many Requests",
        NULL, /* 430 */
        "431 Request Header Fields Too Large",
#define LEVEL_500 71
        "500 Internal Server Error",
        "501 Not Implemented",
        "502 Bad Gateway",
        "503 Service Unavailable",
        "504 Gateway Timeout",
        "505 HTTP Version Not Supported",
        "506 Variant Also Negotiates",
        "507 Insufficient Storage",
        "508 Loop Detected",
        NULL, /* 509 */
        "510 Not Extended",
        "511 Network Authentication Required"};

apr_shm_t *mrsc_shm;                          /* Pointer to shared memory block */
char *shmfilename;                            /* Shared memory file name, used on some systems */
static apr_global_mutex_t *mrsc_mutex = NULL; /* Lock around shared memory segment access */
static char mrsc_mutex_name[L_tmpnam];

static const char *mrsc_mutex_type = "mrsc-shm";

/* Data structure for the counters*/
typedef struct mrsc_data
{
    apr_uint64_t request_status[RESPONSE_CODES];
} mrsc_data;

/*
 * Clean up the shared memory block. This function is registered as
 * cleanup function for the configuration pool, which gets called
 * on restarts. It assures that the new children will not talk to a stale
 * shared memory segment.
 */
static apr_status_t shm_cleanup_wrapper(void *unused)
{
    if (mrsc_shm)
        return apr_shm_destroy(mrsc_shm);
    return OK;
}

/*
 * This routine is called in the parent, so we'll set up the shared
 * memory segment and mutex here.
 */

static int mrsc_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                            apr_pool_t *ptemp, server_rec *s)
{
    apr_status_t rs;
    mrsc_data *base;
    const char *tempdir;
    int i;

    /*
     * Do nothing if we are not creating the final configuration.
     * The parent process gets initialized a couple of times as the
     * server starts up, and we don't want to create any more mutexes
     * and shared memory segments than we're actually going to use.
     */
    void *data;
    const char *userdata_key = "mrsc_init";

    apr_pool_userdata_get(&data, userdata_key, s->process->pool);
    if (!data)
    {
        apr_pool_userdata_set((const void *)1, userdata_key,
                              apr_pool_cleanup_null, s->process->pool);
        return OK;
    }

    /*
     * The shared memory allocation routines take a file name.
     * Depending on system-specific implementation of these
     * routines, that file may or may not actually be created. We'd
     * like to store those files in the operating system's designated
     * temporary directory, which APR can point us to.
     */
    rs = apr_temp_dir_get(&tempdir, pconf);
    if (APR_SUCCESS != rs)
    {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to find temporary directory");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Create the shared memory segment */

    /*
     * Create a unique filename using our pid. This information is
     * stashed in the global variable so the children inherit it.
     */
    shmfilename = apr_psprintf(pconf, "%s/httpd_shm.%ld", tempdir,
                               (long int)getpid());

    /* Now create that segment */
    rs = apr_shm_create(&mrsc_shm, sizeof(mrsc_data),
                        (const char *)shmfilename, pconf);
    if (APR_SUCCESS != rs)
    {
        ap_log_error(APLOG_MARK, APLOG_ERR, rs, s,
                     "Failed to create shared memory segment on file %s",
                     shmfilename);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Created it, now let's zero it out */
    base = (mrsc_data *)apr_shm_baseaddr_get(mrsc_shm);
    for (i = 0; i < RESPONSE_CODES; ++i)
    {
        base->request_status[i] = 0;
    }

    /* Create global mutex */
    tmpnam(mrsc_mutex_name);

    rs = apr_global_mutex_create(&mrsc_mutex, mrsc_mutex_name,
                                 APR_LOCK_DEFAULT, s->process->pool);
    if (APR_SUCCESS != rs)
    {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * Destroy the shm segment when the configuration pool gets destroyed. This
     * happens on server restarts. The parent will then (above) allocate a new
     * shm segment that the new children will bind to.
     */
    apr_pool_cleanup_register(pconf, NULL, shm_cleanup_wrapper,
                              apr_pool_cleanup_null);
    return OK;
}

/*
 * This routine gets called when a child inits. We use it to attach
 * to the shared memory segment, and reinitialize the mutex.
 */

static void mrsc_child_init(apr_pool_t *p, server_rec *s)
{
    apr_status_t rs;

    /*
     * Re-open the mutex for the child. Note we're reusing
     * the mutex pointer global here.
     */
    rs = apr_global_mutex_child_init(&mrsc_mutex,
                                     apr_global_mutex_lockfile(mrsc_mutex),
                                     p);
    if (APR_SUCCESS != rs)
    {
        ap_log_error(APLOG_MARK, APLOG_CRIT, rs, s,
                     "Failed to reopen mutex %s in child",
                     mrsc_mutex_type);
        /* There's really nothing else we can do here, since This
         * routine doesn't return a status. If this ever goes wrong,
         * it will turn Apache into a fork bomb. Let's hope it never
         * will.
         */
        exit(1); /* Ugly, but what else? */
    }
}

/* The sample content handler */
static int mrsc_handler(request_rec *r)
{
    int gotlock = 0;
    int camped;
    apr_time_t startcamp;
    apr_int64_t timecamped;
    apr_status_t rs;
    mrsc_data *base;
    int i;

    if (strcmp(r->handler, "request_status_counter"))
    {
        return DECLINED;
    }

    base = (mrsc_data *)apr_shm_baseaddr_get(mrsc_shm);

    if (!r->header_only)
    {
        r->content_type = "text/plain; version=0.0.4";
        ap_rputs("# HELP http_requests_count_total The total number of HTTP requests.\n", r);
        ap_rputs("# TYPE http_requests_count_total counter\n", r);
        for (i = 0; i < RESPONSE_CODES; ++i)
        {
            if (status_lines[i] == '\0')
            {
                ap_rprintf(r, "http_requests_count_total{status=\"%s apache code %d\"}  %d\n", "unknown", i, base->request_status[i]);
            }
            else
            {
                ap_rprintf(r, "http_requests_count_total{status=\"%s\"}  %d\n", status_lines[i], base->request_status[i]);
            }
        }
    }
    return OK;
}

static int mrsc_request_hook(request_rec *r)
{
    apr_status_t rs;
    mrsc_data *base;

    apr_global_mutex_lock(mrsc_mutex);
    base = (mrsc_data *)apr_shm_baseaddr_get(mrsc_shm);
    base->request_status[ap_index_of_response(r->status)]++;
    apr_global_mutex_unlock(mrsc_mutex);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                 "counter %i is at %d", r->status, base->request_status[ap_index_of_response(r->status)]);

    return OK;
}

static void mrsc_register_hooks(apr_pool_t *p)
{
    ap_hook_post_config(mrsc_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_child_init(mrsc_child_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(mrsc_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(mrsc_request_hook, NULL, NULL, APR_HOOK_LAST);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA result_status_counter_module =
    {
        STANDARD20_MODULE_STUFF,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        mrsc_register_hooks /* register hooks                      */
};
