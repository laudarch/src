#	$OpenBSD: Makefile,v 1.29 2016/10/14 18:44:19 deraadt Exp $

CBIN=		instbin
ARCHDIR=	${.CURDIR}/..
SRCDIR=		${.CURDIR}/../../..

.include <bsd.own.mk>

.if exists(${ARCHDIR}/Makefile.inc)
.include "${ARCHDIR}/Makefile.inc"
.endif

MOUNT_POINT?=	/mnt
VND?=		vnd0
VND_DEV=	/dev/${VND}a
VND_RDEV=	/dev/r${VND}a
DISKTYPE=	rdroot
PID!=		echo $$$$
REALIMAGE!=	echo /var/tmp/image.${PID}
IMAGE?=		ramdisk${REV}.fs
IMAGESIZE?=	4608 # 2.25MB in 512 byte blocks
NEWFS_WILL_FAIL?=false
CRUNCHGENOPTS?=-E
KERNEL?=	RAMDISK
UTILS?=		${.CURDIR}/../../miniroot

LISTS=		${ARCHDIR}/list ${.CURDIR}/list.local
MTREE=		${UTILS}/mtree.conf

LIF?=		lif${REV}.fs
CDROM?=		cd${REV}.iso
MKBOOT?=	/usr/mdec/mkboot
BOOT?=		${DESTDIR}/usr/mdec/boot
CDBOOT?=	${DESTDIR}/usr/mdec/cdboot

STRIP?=		strip
GZIPCMD?=	gzip
GZIPFLAGS?=	-9n

# old format, minfree, opt, b/i  trks, sects, cpg
NEWFSOPTS=	-t ffs -m 0 -o space -f 1024 -b 8192
NEWFSOPTS_RD?=	${NEWFSOPTS} -i 4096 -s ${IMAGESIZE}

all:	bsd.rd ${LIF} ${CDROM}

${LIF}:	bsd ${BOOT}
	${MKBOOT} -v ${BOOT} bsd ${LIF}

${CDROM}: bsd ${CDBOOT}
	-rm -rf ${.OBJDIR}/cd-dir
	mkdir ${.OBJDIR}/cd-dir
	cp bsd.rd ${.OBJDIR}/cd-dir/bsd.rd
	mkhybrid -A "OpenBSD ${OSREV} hppa bootonly CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/hppa ${OSREV} boot-only CD" \
	    -o ${.OBJDIR}/${CDROM} ${.OBJDIR}/cd-dir
	dd if=${CDBOOT} of=${.OBJDIR}/${CDROM} bs=32k count=1 conv=notrunc
	dd if=/dev/zero bs=64k count=1 >> ${.OBJDIR}/${CDROM}

obsd:
	cd ${SRCDIR}/sys/arch/${MACHINE}/compile/${KERNEL} && \
	    su ${BUILDUSER} -c '${MAKE} config && ${MAKE} clean && exec ${MAKE}'
	cp -p ${SRCDIR}/sys/arch/${MACHINE}/compile/${KERNEL}/obj/bsd obsd

bsd.rd:	obsd ${IMAGE} rdsetroot
	cp obsd bsd.rd
	${.OBJDIR}/rdsetroot bsd.rd ${IMAGE}
	@${STRIP} bsd.rd

bsd:	bsd.rd
	(cat bsd.rd ; dd if=/dev/zero count=1) | ${GZIPCMD} ${GZIPFLAGS} > bsd

rdsetroot: ${.CURDIR}/../../common/elfrdsetroot.c \
	   ${.CURDIR}/../../common/elf32.c ${.CURDIR}/../../common/elf64.c
	${HOSTCC} ${HOSTCCFLAGS} -o rdsetroot \
	    ${.CURDIR}/../../common/elfrdsetroot.c \
	    ${.CURDIR}/../../common/elf32.c ${.CURDIR}/../../common/elf64.c

${IMAGE}: ${CBIN}
	dd if=/dev/zero of=${REALIMAGE} count=${IMAGESIZE}
	vnconfig -v -c ${VND} ${REALIMAGE}
	disklabel -w ${VND} ${DISKTYPE}
	newfs ${NEWFSOPTS_RD} ${VND_RDEV} || ${NEWFS_WILL_FAIL}
	mount ${VND_DEV} ${MOUNT_POINT}
	mtree -def ${MTREE} -p ${MOUNT_POINT}/ -u
	REV=${REV} CURDIR=${.CURDIR} OBJDIR=${.OBJDIR} \
	    ARCHDIR=${ARCHDIR} TARGDIR=${MOUNT_POINT} UTILS=${UTILS} \
	    RELEASEDIR=${RELEASEDIR} sh ${UTILS}/runlist.sh ${LISTS}
	rm ${MOUNT_POINT}/${CBIN}
	@echo ""
	@df -i ${MOUNT_POINT}
	@echo ""
	umount ${MOUNT_POINT}
	vnconfig -u ${VND_DEV}
	cp ${REALIMAGE} ${IMAGE}
	rm ${REALIMAGE}

${CBIN}.conf: ${LISTS}
	awk -f ${UTILS}/makeconf.awk CBIN=${CBIN} ${LISTS} > ${CBIN}.conf

${CBIN}.mk ${CBIN}.cache ${CBIN}.c: ${CBIN}.conf
	crunchgen ${CRUNCHGENOPTS} -D ${SRCDIR} -L ${DESTDIR}/usr/lib \
	    ${CBIN}.conf

${CBIN}: ${CBIN}.mk ${CBIN}.cache ${CBIN}.c
	${MAKE} -f ${CBIN}.mk SRCLIBDIR=${.CURDIR}/../../../lib all

clean cleandir:
	/bin/rm -f *.core ${IMAGE} ${CBIN} ${CBIN}.mk ${CBIN}.cache \
	    lib*.a lib*.olist ${CBIN}.map \
	    ${CBIN}.conf *.o *.lo *.c obsd bsd bsd.* ${LIF} ${CDROM}

.ifdef RELEASEDIR
beforeinstall:
	cp bsd.rd ${LIF} ${CDROM} ${RELEASEDIR}
.endif

unconfig:
	-umount -f ${MOUNT_POINT}
	-vnconfig -u ${VND}
	-/bin/rm -f ${REALIMAGE}

.include <bsd.obj.mk>
.include <bsd.subdir.mk>
