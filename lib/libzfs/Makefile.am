include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libzfs/include \
	-I${top_srcdir}/lib/libzpool/include \
	-I${top_srcdir}/lib/libspl/include \
        -I${top_srcdir}/lib/libnvpair/include \
	-I${top_srcdir}/lib/libuutil/include \
	-I${top_srcdir}/lib/libefi/include \
	-I${top_srcdir}/module/zfs \
	-I${top_srcdir}/module/zfs/include \
        -I${top_srcdir}/module/zcommon/include \
        -I${top_srcdir}/module/nvpair/include \
        -I${top_srcdir}/module/avl/include \
	-I${top_srcdir}/module/unicode/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_LDFLAGS = -lm

libzfs_la_LIBADD = \
	${top_srcdir}/lib/libefi/libefi.la \
	${top_srcdir}/lib/libuutil/libuutil.la

libzfs_la_SOURCES = \
	${top_srcdir}/lib/libzfs/libzfs_changelist.c \
	${top_srcdir}/lib/libzfs/libzfs_config.c \
	${top_srcdir}/lib/libzfs/libzfs_dataset.c \
	${top_srcdir}/lib/libzfs/libzfs_fru.c \
	${top_srcdir}/lib/libzfs/libzfs_graph.c \
	${top_srcdir}/lib/libzfs/libzfs_import.c \
	${top_srcdir}/lib/libzfs/libzfs_mount.c \
	${top_srcdir}/lib/libzfs/libzfs_pool.c \
	${top_srcdir}/lib/libzfs/libzfs_sendrecv.c \
	${top_srcdir}/lib/libzfs/libzfs_status.c \
	${top_srcdir}/lib/libzfs/libzfs_util.c \
	${top_srcdir}/lib/libzfs/include/libzfs.h \
	${top_srcdir}/lib/libzfs/include/libzfs_impl.h
