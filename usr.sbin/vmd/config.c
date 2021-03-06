/*	$OpenBSD: config.c,v 1.17 2016/10/29 14:56:05 edd Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <net/if.h>

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <util.h>
#include <errno.h>
#include <imsg.h>

#include "proc.h"
#include "vmd.h"

/* Supported bridge types */
const char *vmd_descsw[] = { "switch", "bridge", NULL };

int
config_init(struct vmd *env)
{
	struct privsep	*ps = &env->vmd_ps;
	unsigned int	 what;

	/* Global configuration */
	ps->ps_what[PROC_PARENT] = CONFIG_ALL;
	ps->ps_what[PROC_VMM] = CONFIG_VMS;
	ps->ps_what[PROC_PRIV] = 0;

	/* Other configuration */
	what = ps->ps_what[privsep_process];
	if (what & CONFIG_VMS) {
		if ((env->vmd_vms = calloc(1, sizeof(*env->vmd_vms))) == NULL)
			return (-1);
		TAILQ_INIT(env->vmd_vms);
	}
	if (what & CONFIG_SWITCHES) {
		if ((env->vmd_switches = calloc(1,
		    sizeof(*env->vmd_switches))) == NULL)
			return (-1);
		TAILQ_INIT(env->vmd_switches);
	}

	return (0);
}

void
config_purge(struct vmd *env, unsigned int reset)
{
	struct privsep		*ps = &env->vmd_ps;
	struct vmd_vm		*vm;
	struct vmd_switch	*vsw;
	unsigned int		 what;

	what = ps->ps_what[privsep_process] & reset;
	if (what & CONFIG_VMS && env->vmd_vms != NULL) {
		while ((vm = TAILQ_FIRST(env->vmd_vms)) != NULL)
			vm_remove(vm);
		env->vmd_nvm = 0;
	}
	if (what & CONFIG_SWITCHES && env->vmd_switches != NULL) {
		while ((vsw = TAILQ_FIRST(env->vmd_switches)) != NULL)
			switch_remove(vsw);
		env->vmd_nswitches = 0;
	}
}

int
config_setreset(struct vmd *env, unsigned int reset)
{
	struct privsep	*ps = &env->vmd_ps;
	unsigned int	 id;

	for (id = 0; id < PROC_MAX; id++) {
		if ((reset & ps->ps_what[id]) == 0 ||
		    id == privsep_process)
			continue;
		proc_compose(ps, id, IMSG_CTL_RESET, &reset, sizeof(reset));
	}

	return (0);
}

int
config_getreset(struct vmd *env, struct imsg *imsg)
{
	unsigned int	 mode;

	IMSG_SIZE_CHECK(imsg, &mode);
	memcpy(&mode, imsg->data, sizeof(mode));

	config_purge(env, mode);

	return (0);
}

int
config_registervm(struct privsep *ps, struct vmop_create_params *vmc,
    struct vmd_vm **ret_vm)
{
	struct vmd		*env = ps->ps_env;
	struct vmd_vm		*vm = NULL;
	struct vm_create_params	*vcp = &vmc->vmc_params;
	unsigned int		 i;

	errno = 0;
	*ret_vm = NULL;

	if ((vm = vm_getbyname(vcp->vcp_name)) != NULL) {
		*ret_vm = vm;
		errno = EALREADY;
		goto fail;
	}

	if (vcp->vcp_ncpus == 0)
		vcp->vcp_ncpus = 1;
	if (vcp->vcp_ncpus > VMM_MAX_VCPUS_PER_VM) {
		log_debug("invalid number of CPUs");
		goto fail;
	} else if (vcp->vcp_ndisks > VMM_MAX_DISKS_PER_VM) {
		log_debug("invalid number of disks");
		goto fail;
	} else if (vcp->vcp_nnics > VMM_MAX_NICS_PER_VM) {
		log_debug("invalid number of interfaces");
		goto fail;
	}

	if ((vm = calloc(1, sizeof(*vm))) == NULL)
		goto fail;

	memcpy(&vm->vm_params, vmc, sizeof(vm->vm_params));
	vm->vm_pid = -1;

	for (i = 0; i < vcp->vcp_ndisks; i++)
		vm->vm_disks[i] = -1;
	for (i = 0; i < vcp->vcp_nnics; i++)
		vm->vm_ifs[i].vif_fd = -1;
	vm->vm_kernel = -1;
	vm->vm_vmid = env->vmd_nvm + 1;

	if (vm_getbyvmid(vm->vm_vmid) != NULL)
		fatalx("too many vms");

	TAILQ_INSERT_TAIL(env->vmd_vms, vm, vm_entry);
	env->vmd_nvm++;
	*ret_vm = vm;
	return (0);
fail:
	if (errno == 0)
		errno = EINVAL;
	return (-1);
}

int
config_getvm(struct privsep *ps, struct vmd_vm *vm,
    int kernel_fd, uint32_t peerid)
{
	struct vmd_if		*vif;
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params	*vcp = &vmc->vmc_params;
	unsigned int		 i;
	int			 fd, ttys_fd;
	int			 kernfd = -1, *diskfds = NULL, *tapfds = NULL;
	int			 saved_errno = 0;
	char			 ptyname[VM_TTYNAME_MAX];
	char			 ifname[IF_NAMESIZE], *s;
	char			 path[PATH_MAX];
	unsigned int		 unit;

	errno = 0;

	if (vm->vm_running)
		goto fail;

	switch (privsep_process) {
	case PROC_VMM:
		if (kernel_fd == -1) {
			log_debug("invalid kernel fd");
			goto fail;
		}
		vm->vm_kernel = kernel_fd;
		break;
	case PROC_PARENT:
		diskfds = reallocarray(NULL, vcp->vcp_ndisks, sizeof(*diskfds));
		if (diskfds == NULL) {
			saved_errno = errno;
			log_warn("%s: cannot allocate disk fds", __func__);
			goto fail;
		}
		for (i = 0; i < vcp->vcp_ndisks; i++)
			diskfds[i] = -1;

		tapfds = reallocarray(NULL, vcp->vcp_nnics, sizeof(*tapfds));
		if (tapfds == NULL) {
			saved_errno = errno;
			log_warn("%s: cannot allocate tap fds", __func__);
			goto fail;
		}
		for (i = 0; i < vcp->vcp_nnics; i++)
			tapfds[i] = -1;

		vm->vm_peerid = peerid;

		/* Open kernel for child */
		if ((kernfd = open(vcp->vcp_kernel, O_RDONLY)) == -1) {
			saved_errno = errno;
			log_warn("%s: can't open kernel %s", __func__,
			    vcp->vcp_kernel);
			goto fail;
		}

		/* Open disk images for child */
		for (i = 0 ; i < vcp->vcp_ndisks; i++) {
			if ((diskfds[i] =
			    open(vcp->vcp_disks[i], O_RDWR)) == -1) {
				saved_errno = errno;
				log_warn("%s: can't open %s", __func__,
				    vcp->vcp_disks[i]);
				goto fail;
			}
		}

		/* Open network interfaces */
		for (i = 0 ; i < vcp->vcp_nnics; i++) {
			vif = &vm->vm_ifs[i];

			/* Check if the user has requested a specific tap(4) */
			s = vmc->vmc_ifnames[i];
			if (*s != '\0' && strcmp("tap", s) != 0) {
				if (priv_getiftype(s, ifname, &unit) == -1 ||
				    strcmp(ifname, "tap") != 0) {
					saved_errno = errno;
					log_warn("%s: invalid tap name",
					    __func__);
					goto fail;
				}
			} else
				s = NULL;

			/*
			 * Either open the requested tap(4) device or get
			 * the next available one.
			 */
			if (s != NULL) {
				snprintf(path, PATH_MAX, "/dev/%s", s);
				tapfds[i] = open(path, O_RDWR | O_NONBLOCK);
			} else {
				tapfds[i] = opentap(ifname);
				s = ifname;
			}
			if (tapfds[i] == -1) {
				saved_errno = errno;
				log_warn("%s: can't open %s", __func__, s);
				goto fail;
			}
			if ((vif->vif_name = strdup(s)) == NULL) {
				saved_errno = errno;
				log_warn("%s: can't save ifname", __func__);
				goto fail;
			}

			/* Check if the the interface is attached to a switch */
			s = vmc->vmc_ifswitch[i];
			if (*s != '\0') {
				if ((vif->vif_switch = strdup(s)) == NULL) {
					saved_errno = errno;
					log_warn("%s: can't save switch",
					    __func__);
					goto fail;
				}
			}

			/* Check if the the interface is assigned to a group */
			s = vmc->vmc_ifgroup[i];
			if (*s != '\0') {
				if ((vif->vif_group = strdup(s)) == NULL) {
					saved_errno = errno;
					log_warn("%s: can't save group",
					    __func__);
					goto fail;
				}
			}

			/* Set the interface status */
			vif->vif_flags = vmc->vmc_ifflags[i] & IFF_UP;
		}

		/* Open TTY */
		if (openpty(&fd, &ttys_fd, ptyname, NULL, NULL) == -1 ||
		    (vm->vm_ttyname = strdup(ptyname)) == NULL) {
			saved_errno = errno;
			log_warn("%s: can't open tty", __func__);
			goto fail;
		}
		close(ttys_fd);

		/* Send VM information */
		proc_compose_imsg(ps, PROC_VMM, -1,
		    IMSG_VMDOP_START_VM_REQUEST, vm->vm_vmid, kernfd,
		    vmc, sizeof(*vmc));
		for (i = 0; i < vcp->vcp_ndisks; i++) {
			proc_compose_imsg(ps, PROC_VMM, -1,
			    IMSG_VMDOP_START_VM_DISK, vm->vm_vmid, diskfds[i],
			    &i, sizeof(i));
		}
		for (i = 0; i < vcp->vcp_nnics; i++) {
			proc_compose_imsg(ps, PROC_VMM, -1,
			    IMSG_VMDOP_START_VM_IF, vm->vm_vmid, tapfds[i],
			    &i, sizeof(i));
		}

		proc_compose_imsg(ps, PROC_VMM, -1,
		    IMSG_VMDOP_START_VM_END, vm->vm_vmid, fd,  NULL, 0);
		break;
	default:
		fatalx("vm received by invalid process");
	}

	free(diskfds);
	free(tapfds);

	vm->vm_running = 1;
	return (0);

 fail:
	if (kernfd != -1)
		close(kernfd);
	if (diskfds != NULL) {
		for (i = 0; i < vcp->vcp_ndisks; i++)
			close(diskfds[i]);
		free(diskfds);
	}
	if (tapfds != NULL) {
		for (i = 0; i < vcp->vcp_nnics; i++)
			close(tapfds[i]);
		free(tapfds);
	}

	vm_remove(vm);
	errno = saved_errno;
	if (errno == 0)
		errno = EINVAL;
	return (-1);
}

int
config_getdisk(struct privsep *ps, struct imsg *imsg)
{
	struct vmd_vm	*vm;
	unsigned int	 n;
	
	errno = 0;
	if ((vm = vm_getbyvmid(imsg->hdr.peerid)) == NULL) {
		errno = ENOENT;
		return (-1);
	}

	IMSG_SIZE_CHECK(imsg, &n);
	memcpy(&n, imsg->data, sizeof(n));

	if (n >= vm->vm_params.vmc_params.vcp_ndisks ||
	    vm->vm_disks[n] != -1 || imsg->fd == -1) {
		log_debug("invalid disk id");
		errno = EINVAL;
		return (-1);
	}
	vm->vm_disks[n] = imsg->fd;

	return (0);
}

int
config_getif(struct privsep *ps, struct imsg *imsg)
{
	struct vmd_vm	*vm;
	unsigned int	 n;

	errno = 0;
	if ((vm = vm_getbyvmid(imsg->hdr.peerid)) == NULL) {
		errno = ENOENT;
		return (-1);
	}

	IMSG_SIZE_CHECK(imsg, &n);
	memcpy(&n, imsg->data, sizeof(n));
	if (n >= vm->vm_params.vmc_params.vcp_nnics ||
	    vm->vm_ifs[n].vif_fd != -1 || imsg->fd == -1) {
		log_debug("invalid interface id");
		goto fail;
	}
	vm->vm_ifs[n].vif_fd = imsg->fd;

	return (0);
 fail:
	if (imsg->fd != -1)
		close(imsg->fd);
	errno = EINVAL;
	return (-1);
}
