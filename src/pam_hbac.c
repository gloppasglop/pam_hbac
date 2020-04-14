/*
    Copyright (C) 2015 Jakub Hrozek <jakub.hrozek@posteo.se>

    Module structure based on pam_sss by Sumit Bose <sbose@redhat.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/time.h>

#include <ldap.h>

/* TODO: Only includes for AIX */
#include <usersec.h>

#include "pam_hbac.h"
#include "pam_hbac_obj.h"
#include "pam_hbac_ldap.h"

#define CHECK_AND_RETURN_PI_STRING(s) ((s != NULL && *s != '\0')? s : "(not available)")

#define PAM_IGNORE_UNKNOWN_USER_ARG     0x0001
#define PAM_IGNORE_AUTHINFO_UNAVAIL    (0x0001 << 1)
#define PAM_DEBUG_MODE                 (0x0001 << 2)

#define PH_OPT_IGNORE_UNKNOWN_USER      "ignore_unknown_user"
#define PH_OPT_IGNORE_AUTHINFO_UNAVAIL  "ignore_authinfo_unavail"
#define PH_OPT_CONFIG                   "config="
#define PH_OPT_DEBUG_MODE               "debug"

enum pam_hbac_actions {
    PAM_HBAC_ACCOUNT,
    PAM_HBAC_SENTINEL   /* SENTINEL */
};

struct pam_items {
    const char *pam_service;
    const char *pam_user;
    const char *pam_tty;
    const char *pam_ruser;
    const char *pam_rhost;

    size_t pam_service_size;
    size_t pam_user_size;
    size_t pam_tty_size;
    size_t pam_ruser_size;
    size_t pam_rhost_size;
};

static int
parse_args(pam_handle_t *pamh,
           int argc, const char **argv,
           int *_flags,
           const char **_config)
{
    int flags = 0;
    /* Make sure that config is set if function succeeds */
    const char *config = NULL;

    /* step through arguments */
    for (; argc-- > 0; ++argv) {
        /* generic options */
        if (strcmp(*argv, PH_OPT_IGNORE_UNKNOWN_USER) == 0) {
            flags |= PAM_IGNORE_UNKNOWN_USER_ARG;
        } else if (strcmp(*argv, PH_OPT_IGNORE_AUTHINFO_UNAVAIL) == 0) {
            flags |= PAM_IGNORE_AUTHINFO_UNAVAIL;
        } else if (strcmp(*argv, PH_OPT_DEBUG_MODE) == 0) {
            flags |= PAM_DEBUG_MODE;
        } else if (strncmp(*argv, PH_OPT_CONFIG, strlen(PH_OPT_CONFIG)) == 0) {
            if (*(*argv+strlen(PH_OPT_CONFIG)) == '\0') {
                return EINVAL;
            } else {
                config = *argv+strlen(PH_OPT_CONFIG);
            }
        } else {
            logger(pamh, LOG_ERR, "unknown option: %s", *argv);
        }
    }

    *_config = config;
    *_flags = flags;
    return 0;
}

static void
print_found_options(pam_handle_t *pamh, int flags)
{
    if (flags & PAM_IGNORE_UNKNOWN_USER_ARG) {
        logger(pamh, LOG_DEBUG, "ignore_unknown_user found");
    }

    if (flags & PAM_IGNORE_AUTHINFO_UNAVAIL) {
        logger(pamh, LOG_DEBUG, "ignore_authinfo_unavail found");
    }

    if (flags & PAM_DEBUG_MODE) {
        logger(pamh, LOG_DEBUG, "debug option found");
    }
}

static int get_pam_stritem(const pam_handle_t *pamh,
                           int item_type,
                           const char **str_item)
{
    return pam_get_item(pamh, item_type,
#ifdef HAVE_PAM_GETITEM_CONST
                        (const void **) str_item);
#else
                        (void **) discard_const(str_item));
#endif
}

static int
pam_hbac_get_items(pam_handle_t *pamh, struct pam_items *pi, int flags)
{
    int ret;
    
    /* TODO: Only needed for AIX */ 
    char *attribute;
    char *registry;
    char *username;
    /* END TODO */

    ret = get_pam_stritem(pamh, PAM_SERVICE, &(pi->pam_service));
    if (ret != PAM_SUCCESS) return ret;
    if (pi->pam_service == NULL) pi->pam_service="";
    pi->pam_service_size = strlen(pi->pam_service) + 1;

    ret = get_pam_stritem(pamh, PAM_USER, &(pi->pam_user));
    if (ret != PAM_SUCCESS) return ret;
    if (pi->pam_user == NULL) {
        logger(pamh, LOG_ERR, "No user found, aborting.");
        return PAM_BAD_ITEM;
    }

    if (strcmp(pi->pam_user, "root") == 0) {
        logger(pamh, LOG_NOTICE, "pam_hbac will not handle root.");
        if (flags & PAM_IGNORE_UNKNOWN_USER_ARG) {
            return PAM_IGNORE;
        }
        return PAM_USER_UNKNOWN;
    }
    
    /* TODO: Only for AIX */
    attribute = strdup(S_REGISTRY);
    username = strdup(pi->pam_user);
    ret = getuserattr(username, attribute, &registry, SEC_CHAR);
    free(attribute);
    free(username);
    if (ret != 0) {
        logger(pamh, LOG_NOTICE, "getuserattr S_REGISTRY failed for %s\n", pi->pam_user);
        if (flags & PAM_IGNORE_UNKNOWN_USER_ARG) {
            return PAM_IGNORE;
        }
        return PAM_USER_UNKNOWN;
    }

    logger(pamh, LOG_DEBUG, "REGISTRY for user %s is %s\n", pi->pam_user,registry);
    if (strcmp(registry,"files") == 0) {
        logger(pamh, LOG_NOTICE, "pam_hbac will not handle users with REGISTRY=files: %s\n", pi->pam_user);
        if (flags & PAM_IGNORE_UNKNOWN_USER_ARG) {
            return PAM_IGNORE;
        }
        return PAM_USER_UNKNOWN;
    }
    /* END TODO: Only for AIX */

    pi->pam_user_size = strlen(pi->pam_user) + 1;

    ret = get_pam_stritem(pamh, PAM_TTY, &(pi->pam_tty));
    if (ret != PAM_SUCCESS) return ret;
    if (pi->pam_tty == NULL) pi->pam_tty="";
    pi->pam_tty_size = strlen(pi->pam_tty) + 1;

    ret = get_pam_stritem(pamh, PAM_RUSER, &(pi->pam_ruser));
    if (ret != PAM_SUCCESS) return ret;
    if (pi->pam_ruser == NULL) pi->pam_ruser="";
    pi->pam_ruser_size = strlen(pi->pam_ruser) + 1;

    ret = get_pam_stritem(pamh, PAM_RHOST, &(pi->pam_rhost));
    if (ret != PAM_SUCCESS) return ret;
    if (pi->pam_rhost == NULL) pi->pam_rhost="";
    pi->pam_rhost_size = strlen(pi->pam_rhost) + 1;

    return PAM_SUCCESS;
}

static void
print_pam_items(pam_handle_t *pamh, struct pam_items *pi, int flags)
{
    if (pi == NULL) return;

    logger(pamh, LOG_DEBUG,
           "Service: %s", CHECK_AND_RETURN_PI_STRING(pi->pam_service));
    logger(pamh, LOG_DEBUG,
           "User: %s", CHECK_AND_RETURN_PI_STRING(pi->pam_user));
    logger(pamh, LOG_DEBUG,
           "Tty: %s", CHECK_AND_RETURN_PI_STRING(pi->pam_tty));
    logger(pamh, LOG_DEBUG,
           "Ruser: %s", CHECK_AND_RETURN_PI_STRING(pi->pam_ruser));
    logger(pamh, LOG_DEBUG,
           "Rhost: %s", CHECK_AND_RETURN_PI_STRING(pi->pam_rhost));
}

static pam_handle_t *global_pam_handle;

void hbac_debug_messages(const char *file, int line,
                         const char *function,
                         enum hbac_debug_level level,
                         const char *fmt, ...)
{
    int severity;
    va_list ap;

    switch(level) {
    case HBAC_DBG_FATAL:
        severity = LOG_CRIT;
        break;
    case HBAC_DBG_ERROR:
        severity = LOG_ERR;
        break;
    case HBAC_DBG_WARNING:
        severity = LOG_WARNING;
        break;
    case HBAC_DBG_INFO:
        severity = LOG_NOTICE;
        break;
    case HBAC_DBG_TRACE:
        severity = LOG_DEBUG;
        break;
    default:
        severity = LOG_NOTICE;
        break;
    }

    va_start(ap, fmt);
    va_logger(global_pam_handle, severity, fmt, ap);
    va_end(ap);
}

static struct pam_hbac_ctx *
ph_init(pam_handle_t *pamh,
        const char *config_file)
{
    int ret;
    struct pam_hbac_ctx *ctx;

    ctx = (struct pam_hbac_ctx *) calloc(1, sizeof(struct pam_hbac_ctx));
    if (ctx == NULL) {
        return NULL;
    }

    if (config_file != NULL) {
        logger(pamh, LOG_DEBUG, "Using config file %s\n", config_file);
        ret = ph_read_config(pamh, config_file, &ctx->pc);
    } else {
        ret = ph_read_dfl_config(pamh, &ctx->pc);
    }
    if (ret != 0) {
        logger(pamh, LOG_DEBUG,
               "ph_read_dfl_config returned error: %s", strerror(ret));
        free(ctx);
        return NULL;
    }

    ctx->pamh = pamh;
    return ctx;
}

static void
ph_cleanup(struct pam_hbac_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ph_cleanup_config(ctx->pc);
    free(ctx);
}

void
ph_destroy_secret(struct pam_hbac_ctx *ctx)
{
    if (ctx == NULL || ctx->pc == NULL || ctx->pc->bind_pw == NULL) {
        return;
    }

    _pam_overwrite(discard_const(ctx->pc->bind_pw));
    free_const(ctx->pc->bind_pw);
    /* To avoid double free */
    ctx->pc->bind_pw = NULL;
}

/* FIXME - return more sensible return codes */
static int
pam_hbac(enum pam_hbac_actions action, pam_handle_t *pamh,
         int pam_flags, int argc, const char **argv)
{
    int ret;
    int pam_ret = PAM_SYSTEM_ERR;
    int flags;
    struct pam_items pi;
    struct pam_hbac_ctx *ctx = NULL;
    const char *config_file = NULL;

    struct ph_user *user = NULL;
    struct ph_entry *service = NULL;
    struct ph_entry *targethost = NULL;

    struct hbac_eval_req *eval_req = NULL;
    struct hbac_rule **rules = NULL;
    enum hbac_eval_result hbac_eval_result;
    struct hbac_info *info = NULL;

    (void) pam_flags; /* unused */

    global_pam_handle = pamh;
    hbac_enable_debug(hbac_debug_messages);

    /* Check supported actions */
    switch (action) {
        case PAM_HBAC_ACCOUNT:
            break;
        default:
            logger(pamh, LOG_ERR, "Unsupported action %d\n", action);
            return PAM_SYSTEM_ERR;
    }

    ret = parse_args(pamh, argc, argv, &flags, &config_file);
    if (ret != PAM_SUCCESS) {
        logger(pamh, LOG_ERR,
               "parse_args returned error: %s", strerror(ret));
        pam_ret = PAM_SYSTEM_ERR;
        goto done;
    }

    set_debug_mode(flags & PAM_DEBUG_MODE);

    print_found_options(pamh, flags);

    pam_ret = pam_hbac_get_items(pamh, &pi, flags);
    if (pam_ret != PAM_SUCCESS) {
        logger(pamh, LOG_ERR,
               "pam_hbac_get_items returned error: %s",
               pam_strerror(pamh, pam_ret));
        goto done;
    }

    ctx = ph_init(pamh, config_file);
    if (!ctx) {
        logger(pamh, LOG_ERR, "ph_init failed\n");
        pam_ret = PAM_SYSTEM_ERR;
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_init: OK");
    ph_dump_config(pamh, ctx->pc);

    ret = ph_connect(ctx);
    /* Destroy secret as soon as possible */
    ph_destroy_secret(ctx);
    if (ret != 0) {
        logger(pamh, LOG_NOTICE,
               "ph_connect returned error: %s", strerror(ret));
        if (flags & PAM_IGNORE_AUTHINFO_UNAVAIL) {
            pam_ret = PAM_IGNORE;
        } else {
            pam_ret = PAM_AUTHINFO_UNAVAIL;
        }
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_connect: OK");

    print_pam_items(pamh, &pi, flags);

    /* Run info on the user from NSS, otherwise we can't support AD users since
     * they are not in IPA LDAP.
     */
    user = ph_get_user(pamh, pi.pam_user);
    if (user == NULL) {
        logger(pamh, LOG_NOTICE,
               "Did not find user %s\n", pi.pam_user);
        if (flags & PAM_IGNORE_UNKNOWN_USER_ARG) {
            pam_ret = PAM_IGNORE;
        } else {
            pam_ret = PAM_USER_UNKNOWN;
        }
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_get_user: OK");

    /* Search hosts for fqdn = hostname (automatic or set from config file) */
    ret = ph_get_host(ctx, ctx->pc->hostname, &targethost);
    if (ret == ENOENT) {
        logger(pamh, LOG_NOTICE,
               "Did not find host %s denying access\n", ctx->pc->hostname);
        pam_ret = PAM_PERM_DENIED;
        goto done;
    } else if (ret != 0) {
        logger(pamh, LOG_ERR,
               "ph_get_host error: %s", strerror(ret));
        pam_ret = PAM_ABORT;
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_get_host: OK");

    /* Search for the service */
    ret = ph_get_svc(ctx, pi.pam_service, &service);
    if (ret == ENOENT) {
        logger(pamh, LOG_NOTICE,
               "Did not find service %s denying access\n", pi.pam_service);
        pam_ret = PAM_PERM_DENIED;
        goto done;
    } else if (ret != 0) {
        logger(pamh, LOG_ERR,
               "ph_get_svc error: %s", strerror(ret));
        pam_ret = PAM_ABORT;
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_get_svc: OK");

    /* Download all enabled rules that apply to this host or any of its hostgroups.
     * Iterate over the rules. For each rule:
     *  - Allocate hbac_rule
     *  - check its memberUser attributes. Parse either a username or a groupname
     *    from the DN. Put it into hbac_rule_element
     *  - check its memberService attribtue. Parse either a svcname or a svcgroupname
     *    from the DN. Put into hbac_rule_element
     *
     * Get data for eval request by matching the PAM service name with a downloaded
     * service. Not matching it is not an error, it can still match /all/.
     */

    ret = ph_create_hbac_eval_req(user, targethost, service,
                                  ctx->pc->search_base, &eval_req);
    if (ret != 0) {
        logger(pamh, LOG_ERR,
               "ph_create_eval_req returned error [%d]: %s",
               ret, strerror(ret));
        pam_ret = PAM_SYSTEM_ERR;
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_create_hbac_eval_req: OK");

    ret = ph_get_hbac_rules(ctx, targethost, &rules);
    if (ret != 0) {
        logger(pamh, LOG_ERR,
               "ph_get_hbac_rules returned error [%d]: %s",
               ret, strerror(ret));
        pam_ret = PAM_SYSTEM_ERR;
        goto done;
    }
    logger(pamh, LOG_DEBUG, "ph_get_hbac_rules: OK");

    hbac_eval_result = hbac_evaluate(rules, eval_req, &info);
    switch (hbac_eval_result) {
    case HBAC_EVAL_ALLOW:
        logger(pamh, LOG_DEBUG, "Allowing access\n");
        pam_ret = PAM_SUCCESS;
        break;
    case HBAC_EVAL_DENY:
        logger(pamh, LOG_DEBUG, "Denying access\n");
        pam_ret = PAM_PERM_DENIED;
        break;
    case HBAC_EVAL_OOM:
        logger(pamh, LOG_ERR, "Out of memory!\n");
        pam_ret = PAM_BUF_ERR;
        break;
    case HBAC_EVAL_ERROR:
    default:
        logger(pamh, LOG_ERR,
               "hbac_evaluate returned %d\n", hbac_eval_result);
        pam_ret = PAM_SYSTEM_ERR;
        break;
    }

done:
    logger(pamh, LOG_DEBUG,
           "returning [%d]: %s", pam_ret, pam_strerror(pamh, pam_ret));

    hbac_free_info(info);
    ph_free_hbac_rules(rules);
    ph_free_hbac_eval_req(eval_req);
    ph_free_user(user);
    ph_entry_free(service);
    ph_entry_free(targethost);
    ph_disconnect(ctx);
    ph_cleanup(ctx);
    return pam_ret;
}

/* --- public account management functions --- */

PH_SM_PROTO
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
                 int argc, const char **argv)
{
    return pam_hbac(PAM_HBAC_ACCOUNT, pamh, flags, argc, argv);
}

/* static module data */
#ifdef PAM_STATIC

struct pam_module _pam_hbac_modstruct = {
    "pam_hbac",
    NULL,
    NULL,
    pam_sm_acct_mgmt,
    NULL,
    NULL,
    NULL
};

#endif
