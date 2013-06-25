/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2013
 */

/**
 * @file debugcli.c - A "routing module" that in fact merely gives
 * access to debug commands within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 18/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <session.h>
#include <router.h>
#include <modules.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <debugcli.h>

static char *version_str = "V1.0.0";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	int	execute(ROUTER *instance, void *router_session, GWBUF *queue);

/** The module object definition */
static ROUTER_OBJECT MyObject = { createInstance, newSession, closeSession, execute };

extern int execute_cmd(CLI_SESSION *cli);

static SPINLOCK		instlock;
static CLI_INSTANCE	*instances;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	fprintf(stderr, "Initial debug router module.\n");
	spinlock_init(&instlock);
	instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
	fprintf(stderr, "Returing debug router module object.\n");
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service)
{
CLI_INSTANCE	*inst;

	if ((inst = malloc(sizeof(CLI_INSTANCE))) == NULL)
		return NULL;

	inst->service = service;
	spinlock_init(&inst->lock);
	inst->sessions = NULL;


	/*
	 * We have completed the creation of the instance data, so now
	 * insert this router instance into the linked list of routers
	 * that have been created with this module.
	 */
	spinlock_acquire(&instlock);
	inst->next = instances;
	instances = inst;
	spinlock_release(&instlock);

	return (ROUTER *)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
CLI_INSTANCE	*inst = (CLI_INSTANCE *)instance;
CLI_SESSION	*client;

	if ((client = (CLI_SESSION *)malloc(sizeof(CLI_SESSION))) == NULL)
	{
		return NULL;
	}
	client->session = session;

	memset(client->cmdbuf, 0, 80);

	spinlock_acquire(&inst->lock);
	client->next = inst->sessions;
	inst->sessions = client;
	spinlock_release(&inst->lock);

	session->state = SESSION_STATE_READY;
	return (void *)client;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *router_session)
{
CLI_INSTANCE	*inst = (CLI_INSTANCE *)instance;
CLI_SESSION	*session = (CLI_SESSION *)router_session;


	spinlock_acquire(&inst->lock);
	if (inst->sessions == session)
		inst->sessions = session->next;
	else
	{
		CLI_SESSION *ptr = inst->sessions;
		while (ptr && ptr->next != session)
			ptr = ptr->next;
		if (ptr)
			ptr->next = session->next;
	}
	spinlock_release(&inst->lock);

	/*
	 * We are no longer in the linked list, free
	 * all the memory and other resources associated
	 * to the client session.
	 */
	free(session);
}

/**
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance		The router instance
 * @param router_session	The router session returned from the newSession call
 * @param queue			The queue of data buffers to route
 * @return The number of bytes sent
 */
static	int	
execute(ROUTER *instance, void *router_session, GWBUF *queue)
{
CLI_SESSION	*session = (CLI_SESSION *)router_session;

	/* Extract the characters */
	while (queue)
	{
		strncat(session->cmdbuf, GWBUF_DATA(queue), GWBUF_LENGTH(queue));
		queue = gwbuf_consume(queue, GWBUF_LENGTH(queue));
	}

	if (strrchr(session->cmdbuf, '\n'))
	{
		if (execute_cmd(session))
			dcb_printf(session->session->client, "Gateway> ");
		else
			session->session->client->func.close(session->session->client);
	}
	return 1;
}
