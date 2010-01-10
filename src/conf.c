/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
 \********************************************************************/

/* $Id: conf.c 935 2006-02-01 03:22:04Z benoitg $ */
/** @file conf.c
  @brief Config file parsing
  @author Copyright (C) 2004 Philippe April <papril777@yahoo.com>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include <pthread.h>

#include <string.h>
#include <ctype.h>

#include "common.h"
#include "safe.h"
#include "debug.h"
#include "conf.h"
#include "http.h"
#include "auth.h"
#include "firewall.h"

#include "util.h"

/** @internal
 * Holds the current configuration of the gateway */
static s_config config;

/**
 * Mutex for the configuration file, used by the auth_servers related
 * functions. */
pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;

/** @internal
 * A flag.  If set to 1, there are missing or empty mandatory parameters in the config
 */
static int missing_parms;

/** @internal
 The different configuration options */
typedef enum {
	oBadOption,
	oDaemon,
	oDebugLevel,
	oMaxClients,
	oExternalInterface,
	oGatewayName,
	oGatewayInterface,
	oGatewayAddress,
	oGatewayPort,
	oRemoteAuthenticatorAddress,
	oRemoteAuthenticatorPort,
	oRemoteAuthenticatorPath,
	oHTTPDMaxConn,
	oWebRoot,
	oSplashPage,
	oImagesDir,
	oPagesDir,
	oRedirectURL,
	oClientIdleTimeout,
	oClientForceTimeout,
	oCheckInterval,
	oAuthenticateImmediately,
	oTrafficControl,
	oDownloadLimit,
	oUploadLimit,
	oDownloadIMQ,
	oUploadIMQ,
	oNdsctlSocket,
	oSyslogFacility,
	oFirewallRule,
	oFirewallRuleSet,
	oTrustedMACList,
	oBlockedMACList
} OpCodes;

/** @internal
 The config file keywords for the different configuration options */
static const struct {
	const char *name;
	OpCodes opcode;
	int required;
} keywords[] = {
	{ "daemon",             oDaemon },
	{ "debuglevel",         oDebugLevel },
	{ "maxclients",         oMaxClients },
	{ "externalinterface",  oExternalInterface },
	{ "gatewayname",        oGatewayName },
	{ "gatewayinterface",   oGatewayInterface },
	{ "gatewayaddress",     oGatewayAddress },
	{ "gatewayport",        oGatewayPort },
	{ "remoteauthenticatoraddress",     oRemoteAuthenticatorAddress },
	{ "remoteauthenticatorport",        oRemoteAuthenticatorPort },
	{ "remoteauthenticatorpath",        oRemoteAuthenticatorPath },
	{ "webroot",      	oWebRoot },
	{ "splashpage",      	oSplashPage },
	{ "imagesdir",   	oImagesDir },
	{ "pagesdir",   	oPagesDir },
	{ "redirectURL",      	oRedirectURL },
	{ "clientidletimeout",  oClientIdleTimeout },
	{ "clientforcetimeout", oClientForceTimeout },
	{ "checkinterval",      oCheckInterval },
	{ "authenticateimmediately",	oAuthenticateImmediately },
	{ "trafficcontrol",	oTrafficControl },
	{ "downloadlimit",	oDownloadLimit },
	{ "uploadlimit",	oUploadLimit },
	{ "downloadimq",	oDownloadIMQ },
	{ "uploadimq",		oUploadIMQ },
	{ "syslogfacility", 	oSyslogFacility },
	{ "syslogfacility", 	oSyslogFacility },
	{ "ndsctlsocket", 	oNdsctlSocket },
	{ "firewallruleset",	oFirewallRuleSet },
	{ "firewallrule",	oFirewallRule },
	{ "trustedmaclist",	oTrustedMACList },
	{ "blockedmaclist",	oBlockedMACList },
	{ NULL,                 oBadOption },
};

static OpCodes config_parse_opcode(const char *cp, const char *filename, int linenum);

/** Accessor for the current gateway configuration
@return:  A pointer to the current config.  The pointer isn't opaque, but should be treated as READ-ONLY
 */
s_config *
config_get_config(void) {
  return &config;
}

/** Sets the default config parameters and initialises the configuration system */
void
config_init(void) {
  debug(LOG_DEBUG, "Setting default config parameters");
  strncpy(config.configfile, DEFAULT_CONFIGFILE, sizeof(config.configfile));
  config.debuglevel = DEFAULT_DEBUGLEVEL;
  config.ext_interface = NULL;
  config.maxclients = DEFAULT_MAXCLIENTS;
  config.gw_name = DEFAULT_GATEWAYNAME;
  config.gw_interface = NULL;
  config.gw_address = NULL;
  config.gw_port = DEFAULT_GATEWAYPORT;
  config.remote_auth_address = NULL;
  config.remote_auth_port = DEFAULT_REMOTE_AUTH_PORT;
  config.webroot = DEFAULT_WEBROOT;
  config.splashpage = DEFAULT_SPLASHPAGE;
  config.imagesdir = DEFAULT_IMAGESDIR;
  config.pagesdir = DEFAULT_PAGESDIR;
  config.authdir = DEFAULT_AUTHDIR;
  config.denydir = DEFAULT_DENYDIR;
  config.redirectURL = NULL;
  config.clienttimeout = DEFAULT_CLIENTTIMEOUT;
  config.clientforceout = DEFAULT_CLIENTFORCEOUT;
  config.checkinterval = DEFAULT_CHECKINTERVAL;
  config.daemon = -1;
  config.authenticate_immediately = DEFAULT_AUTHENTICATE_IMMEDIATELY;
  config.traffic_control = DEFAULT_TRAFFIC_CONTROL;
  config.upload_limit =  DEFAULT_UPLOAD_LIMIT;
  config.download_limit = DEFAULT_DOWNLOAD_LIMIT;
  config.upload_imq =  DEFAULT_UPLOAD_IMQ;
  config.download_imq = DEFAULT_DOWNLOAD_IMQ;
  config.syslog_facility = DEFAULT_SYSLOG_FACILITY;
  config.log_syslog = DEFAULT_LOG_SYSLOG;
  config.ndsctl_sock = safe_strdup(DEFAULT_NDSCTL_SOCK);
  config.internal_sock = safe_strdup(DEFAULT_INTERNAL_SOCK);
  config.rulesets = NULL;
  config.trustedmaclist = NULL;
  config.blockedmaclist = NULL;
}

/**
 * If the command-line didn't provide a config, use the default.
 */
void
config_init_override(void) {
  if (config.daemon == -1) config.daemon = DEFAULT_DAEMON;
}

/** @internal
Attempts to parse an opcode from the config file
*/
static OpCodes
config_parse_opcode(const char *cp, const char *filename, int linenum) {
  int i;

  for (i = 0; keywords[i].name; i++)
    if (strcasecmp(cp, keywords[i].name) == 0)
      return keywords[i].opcode;

  debug(LOG_ERR, "%s: line %d: Bad configuration option: %s", 
	filename, linenum, cp);
  return oBadOption;
}

/**
Advance to the next word
@param s string to parse, this is the next_word pointer, the value of s
	 when the macro is called is the current word, after the macro
	 completes, s contains the beginning of the NEXT word, so you
	 need to save s to something else before doing TO_NEXT_WORD
@param e should be 0 when calling TO_NEXT_WORD(), it'll be changed to 1
	 if the end of the string is reached.
*/
#define TO_NEXT_WORD(s, e) do { \
	while (*s != '\0' && !isblank(*s)) { \
		s++; \
	} \
	if (*s != '\0') { \
		*s = '\0'; \
		s++; \
		while (isblank(*s)) \
			s++; \
	} else { \
		e = 1; \
	} \
} while (0)

/** @internal
Parses firewall rule set information
*/
static void
parse_firewall_ruleset(char *ruleset, FILE *fd, char *filename, int *linenum) {
  char line[MAX_BUF], *p1, *p2;
  int  opcode;

  /* find whitespace delimited word in ruleset string */
  while(isblank(ruleset[0])) ruleset++;
  p1 = strchr(ruleset,' ');
  if(p1) *p1 = '\0';
  p1 = strchr(ruleset,'\t');
  if(p1) *p1 = '\0';

  debug(LOG_DEBUG, "Adding Firewall Rule Set %s", ruleset);
	
  /* Parsing loop */
  while (1) {

    /* Read a line */
    memset(line, 0, MAX_BUF);
    if (fgets(line, MAX_BUF, fd) == NULL) {
      debug(LOG_ERR, "Unclosed Firewall Rule Set at line %d in %s", *linenum, filename);
      debug(LOG_ERR, "Exiting...");
      exit(-1);
    }
    (*linenum)++; /* increment line counter. */


    p1 = line;
    /* get rid of returns, newlines */
    p2 = strchr(p1,'\n');
    if(p2) *p2 = '\0';
    p2 = strchr(p1,'\r');
    if(p2) *p2 = '\0';

    /* strip any comment */
    p2 = strchr(p1,'#');
    if(p2) *p2 = '\0';
    /* strip leading whitespace from the line */
    while(isblank(p1[0])) p1++;
    /* strip trailing whitespace from the line */
    while(p1[0] != '\0' && isblank(p1[strlen(p1)-1])) p1[strlen(p1)-1] = '\0';

    /* if nothing left, get next line */
    if(p1[0] == '\0') continue;

    /* if closing brace, we are done */
    if(p1[0] == '}') break;

    /* next, we coopt the parsing of the regular config */
    
    /* keep going until word boundary is found. */
    p2 = p1;
    while ((*p2 != '\0') && (!isblank(*p2))) p2++;
    /* if this is end of line, it's a problem */
    if(p2[0] == '\0') {
      debug(LOG_ERR, "Firewall Rule incomplete on line %d in %s", *linenum, filename);
      debug(LOG_ERR, "Exiting...");
      exit(-1);
    }
    /* terminate first word, point past it */
    *p2 = '\0';
    p2++;

    /* skip whitespace to point at arg */
    while (isblank(*p2)) p2++;

    /* Get opcode */
    opcode = config_parse_opcode(p1, filename, *linenum);

    debug(LOG_DEBUG, "p1 = [%s]; p2 = [%s]", p1, p2);
			
    switch (opcode) {
    case oFirewallRule:
      _parse_firewall_rule(ruleset, p2);
      break;

    case oBadOption:
    default:
      debug(LOG_ERR, "Bad option %s parsing Firewall Rule Set on line %d in %s", p1, *linenum, filename);
      debug(LOG_ERR, "Exiting...");
      exit(-1);
      break;
    }

  }

  debug(LOG_DEBUG, "Firewall Rule Set %s added.", ruleset);
}

/** @internal
Helper for parse_firewall_ruleset.  Parses a single rule in a ruleset
*/
static int
_parse_firewall_rule(char *ruleset, char *leftover) {
  int i;
  int block_allow = 0; /**< 0 == block, 1 == allow */
  int all_nums = 1; /**< If 0, word contained illegal chars */
  int finished = 0; /**< reached end of line */
  char *token = NULL; /**< First word */
  char *port = NULL; /**< port(s) to allow/block */
  char *protocol = NULL; /**< protocol to allow/block: tcp/udp/icmp */
  char *mask = NULL; /**< Netmask */
  char *other_kw = NULL; /**< other key word */
  t_firewall_ruleset *tmpr;
  t_firewall_ruleset *tmpr2;
  t_firewall_rule *tmp;
  t_firewall_rule *tmp2;

  debug(LOG_DEBUG, "leftover: %s", leftover);

  /* lowercase everything */
  for (i = 0; *(leftover + i) != '\0'
	 && (*(leftover + i) = tolower(*(leftover + i))); i++);
	
  token = leftover;
  TO_NEXT_WORD(leftover, finished);
	
  /* Parse token */
  if (!strcasecmp(token, "block")) {
    block_allow = 0;
  } else if (!strcasecmp(token, "allow")) {
    block_allow = 1;
  } else {
    debug(LOG_ERR, "Invalid rule type %s, expecting "
	  "\"block\" or \"allow\"", token);
    return -1;
  }

  /* Parse the remainder */

  /* Get the optional protocol */
  if (strncmp(leftover, "tcp", 3) == 0
      || strncmp(leftover, "udp", 3) == 0
      || strncmp(leftover, "icmp", 4) == 0) {
    protocol = leftover;
    TO_NEXT_WORD(leftover, finished);
  }

  /* Get the optional port or port range*/
  if (strncmp(leftover, "port", 4) == 0) {
    TO_NEXT_WORD(leftover, finished);
    /* Get port now */
    port = leftover;
    TO_NEXT_WORD(leftover, finished);
    for (i = 0; *(port + i) != '\0'; i++)
      if (!isdigit(*(port + i)) && (*(port + i) != ':'))
	all_nums = 0; /*< No longer only digits or : */
    if (!all_nums) {
      debug(LOG_ERR, "Invalid port %s", port);
      return -3; /*< Fail */
    }
  }

  /* Now, look for optional IP address/mask */
  if (!finished) {
    /* should be exactly "to" */
    other_kw = leftover;
    TO_NEXT_WORD(leftover, finished);
    if (strcmp(other_kw, "to") || finished) {
      debug(LOG_ERR, "Invalid or unexpected keyword %s, "
	    "expecting \"to\"", other_kw);
      return -4; /*< Fail */
    }

    /* Get IP address/range now */
    mask = leftover;
    TO_NEXT_WORD(leftover, finished);
    all_nums = 1;
    for (i = 0; *(mask + i) != '\0'; i++)
      if (!isdigit(*(mask + i)) && (*(mask + i) != '.')
	  && (*(mask + i) != '/'))
	all_nums = 0; /*< No longer only digits or . or / */
    if (!all_nums) {
      debug(LOG_ERR, "Invalid mask %s", mask);
      return -5; /*< Fail */
    }
  }

  /* Generate rule record */
  tmp = safe_malloc(sizeof(t_firewall_rule));
  memset((void *)tmp, 0, sizeof(t_firewall_rule));
  tmp->block_allow = block_allow;
  if (protocol != NULL)
    tmp->protocol = safe_strdup(protocol);
  if (port != NULL)
    tmp->port = safe_strdup(port);
  if (mask == NULL)
    tmp->mask = safe_strdup("0.0.0.0/0");
  else
    tmp->mask = safe_strdup(mask);

  debug(LOG_DEBUG, "Adding Firewall Rule %s %s port %s to %s", token, tmp->protocol, tmp->port, tmp->mask);
	
  /* Append the rule record */
  if (config.rulesets == NULL) {
    config.rulesets = safe_malloc(sizeof(t_firewall_ruleset));
    memset(config.rulesets, 0, sizeof(t_firewall_ruleset));
    config.rulesets->name = safe_strdup(ruleset);
    tmpr = config.rulesets;
  } else {
    tmpr2 = tmpr = config.rulesets;
    while (tmpr != NULL && (strcmp(tmpr->name, ruleset) != 0)) {
      tmpr2 = tmpr;
      tmpr = tmpr->next;
    }
    if (tmpr == NULL) {
      /* Ruleset did not exist */
      tmpr = safe_malloc(sizeof(t_firewall_ruleset));
      memset(tmpr, 0, sizeof(t_firewall_ruleset));
      tmpr->name = safe_strdup(ruleset);
      tmpr2->next = tmpr;
    }
  }

  /* At this point, tmpr == the ruleset */
  if (tmpr->rules == NULL) {
    /* No rules... */
    tmpr->rules = tmp;
  } else {
    tmp2 = tmpr->rules;
    while (tmp2->next != NULL)
      tmp2 = tmp2->next;
    tmp2->next = tmp;
  }
	
  return 1;
}

t_firewall_rule *
get_ruleset(char *ruleset) {
  t_firewall_ruleset	*tmp;

  for (tmp = config.rulesets; tmp != NULL
	 && strcmp(tmp->name, ruleset) != 0; tmp = tmp->next);

  if (tmp == NULL)
    return NULL;

  return(tmp->rules);
}

/**
@param filename Full path of the configuration file to be read 
*/
void
config_read(char *filename) {
  FILE *fd;
  char line[MAX_BUF], *s, *p1, *p2;
  int linenum = 0, opcode, value;

  debug(LOG_INFO, "Reading configuration file '%s'", filename);

  if (!(fd = fopen(filename, "r"))) {
    debug(LOG_ERR, "Could not open configuration file '%s', "
	  "exiting...", filename);
    exit(1);
  }

  while (1) {
    /* Read a line */
    memset(line, 0, MAX_BUF);
    if (fgets(line, MAX_BUF, fd) == NULL) {
      break;
    }
    linenum++;
    s = line;

    /* terminate the line at returns, newlines */
    p1 = strchr(s,'\n');
    if(p1) *p1 = '\0';
    p1 = strchr(s,'\r');
    if(p1) *p1 = '\0';

    /* strip any comment */
    p1 = strchr(s,'#');
    if(p1) *p1 = '\0';
    /* strip leading whitespace from the line */
    while(isblank(s[0])) s++;
    /* strip trailing whitespace from the line */
    while(s[0] != '\0' && isblank(s[strlen(s)-1])) s[strlen(s)-1] = '\0';
    /* if nothing left, get next line */
    if(s[0] == '\0') continue;

    /* now we require the line must have form: <opcode><whitespace><arg>
       even if <arg> is just a left brace, for example */
    
    /* see if there is a whitespace-delimited arg following the opcode */
    p1 = s;
    /* find first word end boundary */
    while ((*p1 != '\0') && (!isblank(*p1))) p1++;
    /* if this is end of line, it's a problem */
    if(p1[0] == '\0') {
      debug(LOG_ERR, "Option %s requires argument on line %d in %s", s, linenum, filename);
      debug(LOG_ERR, "Exiting...");
      exit(-1);
    }

    /* terminate opcode, point past it */
    *p1 = '\0';
    p1++;

    /* skip delimiting whitespace, make p1 point at arg */
    while (isblank(*p1)) p1++;

    debug(LOG_DEBUG, "Parsing opcode: %s, arg: %s", s, p1);
    opcode = config_parse_opcode(s, filename, linenum);

    switch(opcode) {
    case oDaemon:
      if (config.daemon == -1 && ((value = parse_boolean_value(p1)) != -1)) {
	config.daemon = value;
      }
      break;
    case oMaxClients:
      if(sscanf(p1, "%d", &config.maxclients) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
    case oExternalInterface:
      config.ext_interface = safe_strdup(p1);
      break;
    case oGatewayName:
      config.gw_name = safe_strdup(p1);
      break;
    case oGatewayInterface:
      config.gw_interface = safe_strdup(p1);
      break;
    case oGatewayAddress:
      config.gw_address = safe_strdup(p1);
      break;
    case oGatewayPort:
      sscanf(p1, "%d", &config.gw_port);
      break;
    case oRemoteAuthenticatorAddress:
      config.remote_auth_address = safe_strdup(p1);
      break;
    case oRemoteAuthenticatorPort:
      sscanf(p1, "%d", &config.remote_auth_port);
      break;
    case oRemoteAuthenticatorPath:
      config.remote_auth_path = safe_strdup(p1);
      break;
    case oFirewallRuleSet:
      parse_firewall_ruleset(p1, fd, filename, &linenum);
      break;
    case oTrustedMACList:
      parse_trusted_mac_list(p1);
      break;
    case oBlockedMACList:
      parse_blocked_mac_list(p1);
      break;
    case oWebRoot:
      config.webroot = safe_strdup(p1);
      break;
    case oSplashPage:
      config.splashpage = safe_strdup(p1);
      break;
    case oImagesDir:
      config.imagesdir = safe_strdup(p1);
      break;
    case oPagesDir:
      config.pagesdir = safe_strdup(p1);
      break;
    case oRedirectURL:
      config.redirectURL = safe_strdup(p1);
      break;
    case oNdsctlSocket:
      free(config.ndsctl_sock);
      config.ndsctl_sock = safe_strdup(p1);
      break;
    case oClientIdleTimeout:
      if(sscanf(p1, "%d", &config.clienttimeout) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oClientForceTimeout:
      if(sscanf(p1, "%d", &config.clientforceout) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oAuthenticateImmediately:
      if ((value = parse_boolean_value(p1)) != -1) {
	config.authenticate_immediately = value;
      } else {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oTrafficControl:
      if ((value = parse_boolean_value(p1)) != -1) {
	config.traffic_control = value;
      } else {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oDownloadLimit:
      if(sscanf(p1, "%d", &config.download_limit) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oUploadLimit:
      if(sscanf(p1, "%d", &config.upload_limit) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oDownloadIMQ:
      if(sscanf(p1, "%d", &config.download_imq) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oUploadIMQ:
      if(sscanf(p1, "%d", &config.upload_imq) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;

      
    case oSyslogFacility:
      if(sscanf(p1, "%d", &config.syslog_facility) < 1) {
	debug(LOG_ERR, "Bad arg %s to option %s on line %d in %s", p1, s, linenum, filename);
	debug(LOG_ERR, "Exiting...");
	exit(-1);
      }
      break;
    case oBadOption:
      debug(LOG_ERR, "Bad option %s on line %d in %s", s, linenum, filename);
      debug(LOG_ERR, "Exiting...");
      exit(-1);
      break;
    }
  }

  fclose(fd);
}

/** @internal
Parses a boolean value from the config file
*/
static int
parse_boolean_value(char *line) {
  if (strcasecmp(line, "yes") == 0) {
    return 1;
  }
  if (strcasecmp(line, "no") == 0) {
    return 0;
  }
  if (strcasecmp(line, "true") == 0) {
    return 1;
  }
  if (strcasecmp(line, "false") == 0) {
    return 0;
  }
  if (strcmp(line, "1") == 0) {
    return 1;
  }
  if (strcmp(line, "0") == 0) {
    return 0;
  }

  return -1;
}

void parse_trusted_mac_list(char *ptr) {
  char *ptrcopy = NULL, *ptrcopyptr;
  char *possiblemac = NULL;
  char *mac = NULL;
  t_MAC *p = NULL;

  debug(LOG_DEBUG, "Parsing string [%s] for trusted MAC addresses", ptr);

  mac = safe_malloc(18);

  /* strsep modifies original, so let's make a copy */
  ptrcopyptr = ptrcopy = safe_strdup(ptr);

  while (possiblemac = strsep(&ptrcopy, ", \t")) {
    if (sscanf(possiblemac, "%17[A-Fa-f0-9:]", mac) == 1) {
      /* Copy mac to the list */

      debug(LOG_DEBUG, "Adding MAC address [%s] to trusted list", mac);

      if (config.trustedmaclist == NULL) {
	config.trustedmaclist = safe_malloc(sizeof(t_MAC));
	config.trustedmaclist->mac = safe_strdup(mac);
	config.trustedmaclist->next = NULL;
      }
      else {
	/* Advance to the last entry */
	for (p = config.trustedmaclist; p->next != NULL; p = p->next);
	p->next = safe_malloc(sizeof(t_MAC));
	p = p->next;
	p->mac = safe_strdup(mac);
	p->next = NULL;
      }
    }
  }
  free(ptrcopyptr);
  free(mac);
}

void parse_blocked_mac_list(char *ptr) {
  char *ptrcopy = NULL, *ptrcopyptr;
  char *possiblemac = NULL;
  char *mac = NULL;
  t_MAC *p = NULL;

  debug(LOG_DEBUG, "Parsing string [%s] for blocked MAC addresses", ptr);

  mac = safe_malloc(18);

  /* strsep modifies original, so let's make a copy */
  ptrcopyptr = ptrcopy = safe_strdup(ptr);
  
  while ((possiblemac = strsep(&ptrcopy, ", \t"))) {
    if (sscanf(possiblemac, "%17[A-Fa-f0-9:]", mac) == 1) {
      /* Copy mac to the list */

      debug(LOG_DEBUG, "Adding MAC address [%s] to blocked list", mac);

      if (config.blockedmaclist == NULL) {
	config.blockedmaclist = safe_malloc(sizeof(t_MAC));
	config.blockedmaclist->mac = safe_strdup(mac);
	config.blockedmaclist->next = NULL;
      }
      else {
	/* Advance to the last entry */
	for (p = config.blockedmaclist; p->next != NULL; p = p->next);
	p->next = safe_malloc(sizeof(t_MAC));
	p = p->next;
	p->mac = safe_strdup(mac);
	p->next = NULL;
      }
    }
  }
  free(ptrcopyptr);
  free(mac);
}

/** Verifies if the configuration is complete and valid.  Terminates the program if it isn't */
void
config_validate(void)
{
  config_notnull(config.gw_interface, "GatewayInterface");

  if (missing_parms) {
    debug(LOG_ERR, "Configuration is not complete, exiting...");
    exit(-1);
  }
}

/** @internal
    Verifies that a required parameter is not a null pointer
*/
static void
config_notnull(void *parm, char *parmname)
{
	if (parm == NULL) {
		debug(LOG_ERR, "%s is not set", parmname);
		missing_parms = 1;
	}
}
