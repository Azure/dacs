// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ContentHandler.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

// TODO: have a context struct instead of global variable
static std::string mountPath;

static void *dacsfs_init(
    struct fuse_conn_info *conn,
	struct fuse_config *cfg)
{
	//cfg->direct_io = 1; // Can't do direct IO because it doesn't support mmap
	cfg->kernel_cache = 1;
	cfg->use_ino = 0;
    cfg->nullpath_ok = 1;

	//conn->max_readahead = 0; // Disable readahead
	return NULL;
}

static int dacsfs_getattr(
    const char *path,
    struct stat *stbuf,
	struct fuse_file_info *fi)
{
	LOG_DEBUG("getattr recieved. path={}, fi->fh={}", path == nullptr ? "null" : path , fi == nullptr ? 0 : fi->fh);
	int res = 0;

	if(fi)
		res = ContentHandler::GetInstance().HandleFxstat(1, fi->fh, stbuf);
	else
		res = ContentHandler::GetInstance().HandleLxstat(
			1,
			path,
			stbuf);

	return res;
}

static int dacsfs_opendir(const char *path, struct fuse_file_info *fi)
{
	LOG_DEBUG("opendir recieved. path={}, fi->flags={}", path, fi->flags);
	void* d = ContentHandler::GetInstance().HandleOpendir(
		path);

	fi->fh = (unsigned long) d;
	return 0;
}

static int dacsfs_readdir(
	const char *path,
	void *buf,
	fuse_fill_dir_t filler,
	off_t offset,
	struct fuse_file_info *fi,
	enum fuse_readdir_flags flags)
{
	(void) flags;

	LOG_DEBUG("readdir recieved. path={}, offset={}, fi->fh={}",
	  path == nullptr ? "null" : path,
	  offset,
	  fi == nullptr ? 0 : fi->fh);

	void *dp = (void *)fi->fh;
	struct dirent64 *de;

	while ((de = (struct dirent64 *)ContentHandler::GetInstance().HandleReaddir(dp, true)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = 0;
		st.st_mode = de->d_type << 12;
		st.st_mode |= 0777;

		if (filler(buf, de->d_name, &st, 0, (fuse_fill_dir_flags)0))
			break;
	}

	return 0;
}

static int dacsfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	LOG_DEBUG("releasedir recieved. path={}, fi->fh={}",
	    path == nullptr ? "null" : path,
		fi == nullptr ? 0 : fi->fh);

	void *dp = (void *)fi->fh;
	return ContentHandler::GetInstance().HandleClosedir(dp);
}

static int dacsfs_open(const char *path, struct fuse_file_info *fi)
{
	LOG_DEBUG("open recieved. path={}, fi->flags={}", path, fi->flags);
	int res = ContentHandler::GetInstance().HandleOpen(
		path,
		fi->flags);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int dacsfs_read(
	const char *path,
	char *buf,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	int fd;
	int res;

	if (fi == NULL)
		fd = ContentHandler::GetInstance().HandleOpen(
			path,
			O_RDONLY);
	else
		fd = fi->fh;

	if (fd == -1)
		return -errno;

	res = ContentHandler::GetInstance().HandlePRead(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if (fi == NULL)
		ContentHandler::GetInstance().HandleClose(fd);
	return res;
}

static int dacsfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	return ContentHandler::GetInstance().HandleClose(fi->fh);
}

static off_t dacsfs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
	int fd;
	off_t res;

	if (fi == NULL)
		fd = ContentHandler::GetInstance().HandleOpen(
			path,
			O_RDONLY);
	else
		fd = fi->fh;

	if (fd == -1)
		return -errno;

	res = ContentHandler::GetInstance().HandleLseek(fd, off, whence);
	if (res == -1)
		res = -errno;

	if (fi == NULL)
		close(fd);
	return res;
}

static const struct fuse_operations dacsfs_oper = {
	.getattr	= dacsfs_getattr,
	.readlink	= NULL,
	.mknod      = NULL,
	.mkdir	    = NULL,
	.unlink     = NULL,
	.rmdir 	    = NULL,
	.symlink	= NULL,
	.rename	    = NULL,
	.link	    = NULL,
	.chmod	    = NULL,
	.chown	    = NULL,
	.truncate   = NULL,
	.open		= dacsfs_open,
	.read		= dacsfs_read,
	.write	    = NULL,
	.statfs  	= NULL,
	.flush	    = NULL,
	.release    = dacsfs_release,
	.fsync	    = NULL,
	.setxattr	= NULL,
	.getxattr	= NULL,
	.listxattr	= NULL,
	.removexattr = NULL,
	.opendir    = dacsfs_opendir,
	.readdir	= dacsfs_readdir,
	.releasedir = dacsfs_releasedir,
	.fsyncdir	= NULL,
	.init       = dacsfs_init,
	.destroy    = NULL,
	.access     = NULL,
	.create	    = NULL,
	.lock       = NULL,
	.utimens	= NULL,
	.bmap   	= NULL,
	.ioctl  	= NULL,
	.poll   	= NULL,
	.write_buf	= NULL,
	.read_buf	= NULL,
	.flock  	= NULL,
	.fallocate	= NULL,
	.copy_file_range = NULL,
	.lseek      = dacsfs_lseek
};

static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
}

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;
	
	LOG_INFO("Mounting with arguments:");
	for (int i = 1; i < argc; i++)
	{
		LOG_INFO("argv[{}] = {}", i, argv[i]);
	}

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

	ret = fuse_main(args.argc, args.argv, &dacsfs_oper, NULL);
	fuse_opt_free_args(&args);
	LOG_INFO("Fuse mount at exited with code {}", ret);
	return ret;
}