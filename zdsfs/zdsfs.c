/*
 * FUSE file system for z/OS data set access
 * Copyright IBM Corporation 2013
 */

/* The fuse version define tells fuse that we want to use the new API */
#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <fuse.h>
#include <fuse_opt.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>

#ifdef HAVE_SETXATTR
#include <linux/xattr.h>
#endif


#include "zt_common.h"

#include "libzds.h"

#define COMP "zdsfs: "
#define METADATAFILE "metadata.txt"

/* defaults for file and directory permissions (octal) */
#define DEF_FILE_PERM 0440
#define DEF_DIR_PERM 0550

struct zdsfs_info {
	int devcount;
	int allow_inclomplete_multi_volume;
	int keepRDW;
	unsigned int tracks_per_frame;
	unsigned long long seek_buffer_size;
	struct zdsroot *zdsroot;

	char *metadata;  /* buffer that contains the content of metadata.txt */
	size_t metasize; /* total size of meta data buffer */
	size_t metaused; /* how many bytes of buffer are already filled */
	time_t metatime; /* when did we create this meta data */
};

static struct zdsfs_info zdsfsinfo;

struct zdsfs_file_info {
	struct dshandle *dsh;
	pthread_mutex_t mutex;

	int is_metadata_file;
	size_t metaread; /* how many bytes have already been read */
};



/* normalize the given path name to a dataset name
 * so that we can compare it to the names in the vtoc. This means:
 * - remove the leading /
 * - remove member names (everything after a subsequent / )
 * Note: we do no upper case, EBCDIC conversion or padding
 */
static void path_to_ds_name(const char *path, char *normds, size_t size)
{
	char *end;

	if (*path == '/')
		++path;
	strncpy(normds, path, size);
	normds[size - 1] = 0;
	end = strchr(normds, '/');
	if (end)
		*end = 0;
}

static void path_to_member_name(const char *path, char *normds, size_t size)
{
	if (*path == '/')
		++path;
	path = strchr(path, '/');
	if (!path)
		normds[0] = 0;
	else {
		++path;
		strncpy(normds, path, size);
		normds[size - 1] = 0;
	}
}



static int zdsfs_getattr(const char *path, struct stat *stbuf)
{
	char normds[MAXDSNAMELENGTH];
	size_t dssize;
	struct dataset *ds;
	struct pdsmember *member;
	int rc, ispds, issupported;
	unsigned long long tracks;
	format1_label_t *f1;
	time_t time;
	struct tm tm;

	memset(stbuf, 0, sizeof(struct stat));
	/* root directory case */
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | DEF_DIR_PERM;
		stbuf->st_nlink = 2;
		stbuf->st_atime = zdsfsinfo.metatime;
		stbuf->st_mtime = zdsfsinfo.metatime;
		stbuf->st_ctime = zdsfsinfo.metatime;
		return 0;
	}

	if (strcmp(path, "/"METADATAFILE) == 0) {
		stbuf->st_mode = S_IFREG | DEF_FILE_PERM;
		stbuf->st_nlink = 1;
		stbuf->st_size = zdsfsinfo.metaused;
		stbuf->st_atime = zdsfsinfo.metatime;
		stbuf->st_mtime = zdsfsinfo.metatime;
		stbuf->st_ctime = zdsfsinfo.metatime;
		return 0;
	}

	path_to_ds_name(path, normds, sizeof(normds));
	rc = lzds_zdsroot_find_dataset(zdsfsinfo.zdsroot, normds, &ds);
	if (rc)
		return -rc;

	lzds_dataset_get_is_supported(ds, &issupported);
	if (!issupported)
		return -ENOENT;

	/* upper limit for the size of the whole data set */
	lzds_dataset_get_size_in_tracks(ds, &tracks);
	dssize = MAXRECSIZE * tracks;

	/* get the last access time */
	lzds_dataset_get_format1_dscb(ds, &f1);
	memset(&tm, 0, sizeof(tm));
	if (f1->DS1REFD.year || f1->DS1REFD.day) {
		tm.tm_year = f1->DS1REFD.year;
		tm.tm_mday = f1->DS1REFD.day;
	} else {
		tm.tm_year = f1->DS1CREDT.year;
		tm.tm_mday = f1->DS1CREDT.day;
	}
	tm.tm_isdst = -1;
	time = mktime(&tm);
	if (time < 0)
		time = 0;

	lzds_dataset_get_is_PDS(ds, &ispds);
	if (ispds) {
		path_to_member_name(path, normds, sizeof(normds));
		/* the dataset itself is represented as directory */
		if (strcmp(normds, "") == 0) {
			stbuf->st_mode = S_IFDIR | DEF_DIR_PERM;
			stbuf->st_nlink = 2;
			stbuf->st_size = 0;
			stbuf->st_atime = time;
			stbuf->st_mtime = time;
			stbuf->st_ctime = time;
			stbuf->st_blocks = tracks * 16 * 8;
			stbuf->st_size = dssize;
			return 0;
		}
		rc = lzds_dataset_get_member_by_name(ds, normds, &member);
		if (rc)
			return -ENOENT;
		stbuf->st_mode = S_IFREG | DEF_FILE_PERM;
		stbuf->st_nlink = 1;
		/* the member cannot be bigger than the data set */
		stbuf->st_size = dssize;
		return 0;
	} else { /* normal data set */
		stbuf->st_mode = S_IFREG | DEF_FILE_PERM;
		stbuf->st_nlink = 1;
		stbuf->st_size = dssize;
		stbuf->st_blocks = tracks * 16 * 8;
		stbuf->st_atime = time;
		stbuf->st_mtime = time;
		stbuf->st_ctime = time;
	}
	return 0;
}

static int zdsfs_statfs(const char *UNUSED(path), struct statvfs *statvfs)
{
	struct dasditerator *dasdit;
	unsigned int cyls, heads;
	struct dasd *dasd;
	struct dataset *ds;
	struct dsiterator *dsit;
	unsigned long long totaltracks, usedtracks, dstracks;
	int rc;

	totaltracks = 0;
	rc = lzds_zdsroot_alloc_dasditerator(zdsfsinfo.zdsroot, &dasdit);
	if (rc)
		return -ENOMEM;
	while (!lzds_dasditerator_get_next_dasd(dasdit, &dasd)) {
		lzds_dasd_get_cylinders(dasd, &cyls);
		lzds_dasd_get_heads(dasd, &heads);
		totaltracks += cyls * heads;
	}
	lzds_dasditerator_free(dasdit);

	usedtracks = 0;
	rc = lzds_zdsroot_alloc_dsiterator(zdsfsinfo.zdsroot, &dsit);
	if (rc)
		return -ENOMEM;
	while (!lzds_dsiterator_get_next_dataset(dsit, &ds)) {
		/* To compute the occupied space we consider all data sets,
		 * not just the supported ones */
		lzds_dataset_get_size_in_tracks(ds, &dstracks);
		usedtracks += dstracks;
	}
	lzds_dsiterator_free(dsit);

	if (totaltracks < usedtracks)
		return -EPROTO;

	memset(statvfs, 0, sizeof(*statvfs));
	statvfs->f_bsize = RAWTRACKSIZE;
	statvfs->f_frsize = RAWTRACKSIZE;
	statvfs->f_blocks = totaltracks;
	statvfs->f_bfree = totaltracks - usedtracks;
	statvfs->f_bavail = totaltracks - usedtracks;
	statvfs->f_namemax = MAXDSNAMELENGTH - 1;
	return 0;
}

static int zdsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t UNUSED(offset), struct fuse_file_info *UNUSED(fi))
{
	char normds[MAXDSNAMELENGTH];
	char *mbrname;
	char *dsname;
	struct dataset *ds;
	struct dsiterator *dsit;
	struct memberiterator *it;
	struct pdsmember *member;
	int rc;
	int ispds, issupported;

	/* we have two type of directories
	 * type one: the root directory contains all data sets
	 */
	if (strcmp(path, "/") == 0) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, METADATAFILE, NULL, 0);
		/* note that we do not need to distinguish between
		 * normal files and directories here, that is done
		 * in the rdf_getattr function
		 */
		rc = lzds_zdsroot_alloc_dsiterator(zdsfsinfo.zdsroot, &dsit);
		if (rc)
			return -ENOMEM;
		while (!lzds_dsiterator_get_next_dataset(dsit, &ds)) {
			lzds_dataset_get_is_supported(ds, &issupported);
			if (issupported) {
				lzds_dataset_get_name(ds, &dsname);
				filler(buf, dsname, NULL, 0);
			}
		}
		lzds_dsiterator_free(dsit);
		return 0;
	}

	/* type two: a partitioned data set, contains all PDS members */
	path_to_ds_name(path, normds, sizeof(normds));
	rc = lzds_zdsroot_find_dataset(zdsfsinfo.zdsroot, normds, &ds);
	if (rc)
		return -ENOENT;
	lzds_dataset_get_is_PDS(ds, &ispds);
	if (ispds) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		rc = lzds_dataset_alloc_memberiterator(ds, &it);
		if (rc)
			return -ENOMEM;
		while (!lzds_memberiterator_get_next_member(it, &member)) {
			lzds_pdsmember_get_name(member, &mbrname);
			filler(buf, mbrname, NULL, 0);
		}
		lzds_memberiterator_free(it);
	} else
		return -ENOENT;

	return 0;
}


static int zdsfs_open(const char *path, struct fuse_file_info *fi)
{
	char normds[45];
	struct dshandle *dsh;
	struct dataset *ds;
	struct zdsfs_file_info *zfi;
	int rc;
	int ispds, issupported;
	struct errorlog *log;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;
	/*
	 * The contents of the dataset is smaller than the data set
	 * size we return in zdsfs_getattr. We need to set direct_io
	 * to make sure that fuse will report the end of the data with EOF.
	 */
	fi->direct_io = 1;

	zfi = malloc(sizeof(*zfi));
	if (!zfi)
		return -ENOMEM;
	rc = pthread_mutex_init(&zfi->mutex, NULL);
	if (rc)
		goto error1;

	if (strcmp(path, "/"METADATAFILE) == 0) {
		zfi->dsh = NULL;
		zfi->is_metadata_file = 1;
		zfi->metaread = 0;
		fi->fh = (unsigned long)zfi;
		return 0;
	}

	path_to_ds_name(path, normds, sizeof(normds));
	rc = lzds_zdsroot_find_dataset(zdsfsinfo.zdsroot, normds, &ds);
	if (rc)
		goto error1;

	lzds_dataset_get_is_supported(ds, &issupported);
	if (!issupported) {
		/* we should never get this error, as unsupported data sets are
		 * not listed. But just in case, print a message */
		fprintf(stderr,	"Error: Data set %s is not supported\n", normds);
		rc = -ENOENT;
		goto error1;
	}

	rc = lzds_dataset_alloc_dshandle(ds, zdsfsinfo.tracks_per_frame, &dsh);
	if (rc) {
		rc = -rc;
		goto error1;
	}

	rc = lzds_dshandle_set_seekbuffer(dsh, zdsfsinfo.seek_buffer_size);
	if (rc) {
		fprintf(stderr,	"Error when preparing seek buffer:\n");
		lzds_dshandle_get_errorlog(dsh, &log);
		lzds_errorlog_fprint(log, stderr);
		rc = -rc;
		goto error2;
	}
	/* if the data set is a PDS, then the path must contain a valid
	 * member name, and the context must be set to this member
	 */
	lzds_dataset_get_is_PDS(ds, &ispds);
	if (ispds) {
		path_to_member_name(path, normds, sizeof(normds));
		rc = lzds_dshandle_set_member(dsh, normds);
		if (rc) {
			fprintf(stderr,	"Error when preparing member:\n");
			lzds_dshandle_get_errorlog(dsh, &log);
			lzds_errorlog_fprint(log, stderr);
			rc = -rc;
			goto error2;
		}
	}
	rc = lzds_dshandle_set_keepRDW(dsh, zdsfsinfo.keepRDW);
	if (rc) {
		fprintf(stderr,	"Error when preparing RDW setting:\n");
		lzds_dshandle_get_errorlog(dsh, &log);
		lzds_errorlog_fprint(log, stderr);
		rc = -rc;
		goto error2;
	}
	rc = lzds_dshandle_open(dsh);
	if (rc) {
		fprintf(stderr,	"Error when opening data set:\n");
		lzds_dshandle_get_errorlog(dsh, &log);
		lzds_errorlog_fprint(log, stderr);
		rc = -rc;
		goto error2;
	}
	zfi->is_metadata_file = 0;
	zfi->metaread = 0;
	zfi->dsh = dsh;
	fi->fh = (uint64_t)(unsigned long)zfi;
	return 0;

error2:
	lzds_dshandle_free(dsh);
error1:
	free(zfi);
	return rc;

}

static int zdsfs_release(const char *UNUSED(path), struct fuse_file_info *fi)
{
	struct zdsfs_file_info *zfi;
	int rc;

	if (!fi->fh)
		return -EINVAL;
	zfi = (struct zdsfs_file_info *)(unsigned long)fi->fh;
	if (zfi->dsh) {
		lzds_dshandle_close(zfi->dsh);
		lzds_dshandle_free(zfi->dsh);
	}
	rc = pthread_mutex_destroy(&zfi->mutex);
	if (rc)
		fprintf(stderr,	"Error: could not destroy mutex, rc=%d\n", rc);
	free(zfi);
	return 0;
}

static int zdsfs_read(const char *UNUSED(path), char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
	struct zdsfs_file_info *zfi;
	ssize_t count;
	int rc, rc2;
	long long rcoffset;
	struct errorlog *log;

	if (!fi->fh)
		return -ENOENT;
	zfi = (struct zdsfs_file_info *)(unsigned long)fi->fh;

	rc2 = pthread_mutex_lock(&zfi->mutex);
	if (rc2) {
		fprintf(stderr,	"Error: could not lock mutex, rc=%d\n", rc2);
		return -EIO;
	}
	rc = 0;
	if (zfi->is_metadata_file) {
		if (zfi->metaread >= zdsfsinfo.metaused) {
			pthread_mutex_unlock(&zfi->mutex);
			return 0;
		}
		count = zdsfsinfo.metaused - zfi->metaread;
		if (size < (size_t)count)
			count = size;
		memcpy(buf, &zdsfsinfo.metadata[zfi->metaread], count);
		zfi->metaread += count;
	} else {
		lzds_dshandle_get_offset(zfi->dsh, &rcoffset);
		if (rcoffset != offset)
			rc = lzds_dshandle_lseek(zfi->dsh, offset, &rcoffset);
		if (!rc)
			rc = lzds_dshandle_read(zfi->dsh, buf, size, &count);
	}
	rc2 = pthread_mutex_unlock(&zfi->mutex);
	if (rc2)
		fprintf(stderr,	"Error: could not unlock mutex, rc=%d\n", rc2);
	if (rc) {
		fprintf(stderr,	"Error when reading from data set:\n");
		lzds_dshandle_get_errorlog(zfi->dsh, &log);
		lzds_errorlog_fprint(log, stderr);
		return -rc;
	}
	return count;
}


#ifdef HAVE_SETXATTR

#define RECFMXATTR "user.recfm"
#define LRECLXATTR "user.lrecl"
#define DSORGXATTR "user.dsorg"

static int zdsfs_listxattr(const char *path, char *list, size_t size)
{
	int pos = 0;
	size_t list_len;

	/* root directory and the metadata have no extended attributes */
	if (!strcmp(path, "/") || !strcmp(path, "/"METADATAFILE))
		return 0;

	list_len = strlen(RECFMXATTR) + 1 +
		strlen(LRECLXATTR) + 1 +
		strlen(DSORGXATTR) + 1;
	/* size 0 is a special case to query the necessary buffer length */
	if (!size)
		return list_len;
	if (size < list_len)
		return -ERANGE;
	strcpy(list, RECFMXATTR);
	pos += strlen(RECFMXATTR) + 1;
	strcpy(&list[pos], LRECLXATTR);
	pos += strlen(LRECLXATTR) + 1;
	strcpy(&list[pos], DSORGXATTR);
	pos += strlen(DSORGXATTR) + 1;
	return pos;
}

static int zdsfs_getxattr(const char *path, const char *name, char *value,
			  size_t size)
{
	char normds[45];
	struct dataset *ds;
	format1_label_t *f1;
	int rc;
	char buffer[20];
	size_t length;
	int ispds;

	/* nothing for root directory but clear error code needed */
	if (!strcmp(path, "/") || !strcmp(path, "/"METADATAFILE))
		return -ENODATA;

	path_to_ds_name(path, normds, sizeof(normds));
	rc = lzds_zdsroot_find_dataset(zdsfsinfo.zdsroot, normds, &ds);
	if (rc)
		return -rc;
	lzds_dataset_get_format1_dscb(ds, &f1);

	/* null terminate strings */
	memset(value, 0, size);
	memset(buffer, 0, sizeof(buffer));

	/* returned size should be the string length, getfattr fails if I
	 * include an extra byte for the zero termination */
	if (strcmp(name, RECFMXATTR) == 0) {
		lzds_DS1RECFM_to_recfm(f1->DS1RECFM, buffer);
	} else if (strcmp(name, LRECLXATTR) == 0) {
		snprintf(buffer, sizeof(buffer), "%d", f1->DS1LRECL);
	} else if (strcmp(name, DSORGXATTR) == 0) {
		lzds_dataset_get_is_PDS(ds, &ispds);
		if (ispds) {
			/* It is valid to ask for attributes of the directory */
			path_to_member_name(path, normds, sizeof(normds));
			if (strlen(normds))
				snprintf(buffer, sizeof(buffer), "PS");
			else
				snprintf(buffer, sizeof(buffer), "PO");
		} else
			snprintf(buffer, sizeof(buffer), "PS");
	} else
		return -ENODATA;

	length = strlen(buffer);
	if (size == 0) /* special case to query the necessary buffer size */
		return length;
	if (size < length)
		return -ERANGE;
	strcpy(value, buffer);
	return length;

}

#endif /* HAVE_SETXATTR */


static struct fuse_operations rdf_oper = {
	.getattr   = zdsfs_getattr,
	.statfs    = zdsfs_statfs,
	.readdir   = zdsfs_readdir,
	.open      = zdsfs_open,
	.release   = zdsfs_release,
	.read      = zdsfs_read,
#ifdef HAVE_SETXATTR
	.listxattr = zdsfs_listxattr,
	.getxattr  = zdsfs_getxattr,
	/* no setxattr, removexattr since our xattrs are virtual */
#endif
};


static int zdsfs_verify_datasets(void)
{
	int allcomplete, rc;
	struct dataset *ds;
	char *dsname;
	struct dsiterator *dsit;
	int iscomplete, issupported;

	allcomplete = 1;

	rc = lzds_zdsroot_alloc_dsiterator(zdsfsinfo.zdsroot, &dsit);
	if (rc)
		return ENOMEM;
	while (!lzds_dsiterator_get_next_dataset(dsit, &ds)) {
		lzds_dataset_get_is_complete(ds, &iscomplete);
		if (!iscomplete) {
			lzds_dataset_get_name(ds, &dsname);
			fprintf(stderr,	"Warning: Data set %s is not "
				"complete\n", dsname);
			allcomplete = 0;
			continue;
		}
		lzds_dataset_get_is_supported(ds, &issupported);
		if (!issupported) {
			lzds_dataset_get_name(ds, &dsname);
			fprintf(stderr,	"Warning: Data set %s is not "
				"supported\n", dsname);
		}
	}
	lzds_dsiterator_free(dsit);

	if (!allcomplete && zdsfsinfo.allow_inclomplete_multi_volume) {
		fprintf(stderr, "Continue operation with incomplete data"
			" sets\n");
		return 0;
	}

	if (allcomplete) {
		return 0;
	} else {
		fprintf(stderr,
			"Error: Some multi volume data sets are not complete.\n"
			"Specify option 'ignore_incomplete' to allow operation "
			"with incomplete data sets, or add missing volumes.\n");
		return EPROTO;
	}
};


static int zdsfs_create_meta_data_buffer(struct zdsfs_info *info)
{
	char *mbrname;
	char *dsname;
	struct dataset *ds;
	struct dsiterator *dsit;
	struct memberiterator *it;
	struct pdsmember *member;
	int rc;
	int ispds, issupported;

	char buffer[200]; /* large enough for one line of meta data */
	char recfm[20];
	char *temp;
	char *metadata;
	size_t metasize; /* total size of meta data buffer */
	size_t metaused; /* how many bytes of buffer are already filled */
	size_t count;
	format1_label_t *f1;

	metadata = malloc(4096);
	if (!metadata)
		return -ENOMEM;
	metasize = 4096;
	metaused = 0;
	metadata[metaused] = 0;

	rc = lzds_zdsroot_alloc_dsiterator(zdsfsinfo.zdsroot, &dsit);
	if (rc) {
		rc = -ENOMEM;
		goto error;
	}
	while (!lzds_dsiterator_get_next_dataset(dsit, &ds)) {
		lzds_dataset_get_is_supported(ds, &issupported);
		if (!issupported)
			continue;
		lzds_dataset_get_name(ds, &dsname);
		lzds_dataset_get_format1_dscb(ds, &f1);
		lzds_DS1RECFM_to_recfm(f1->DS1RECFM, recfm);
		lzds_dataset_get_is_PDS(ds, &ispds);
		count = snprintf(buffer, sizeof(buffer),
				 "dsn=%s,recfm=%s,lrecl=%u,"
				 "dsorg=%s\n",
				 dsname, recfm, f1->DS1LRECL,
				 ispds ? "PO" : "PS");
		if (count >= sizeof(buffer)) {	/* just a sanity check */
			count = sizeof(buffer) - 1;
			buffer[count] = 0;
		}
		if (metaused + count + 1 > metasize) {
			temp = realloc(metadata,  metasize + 4096);
			if (!temp) {
				rc = -ENOMEM;
				goto error;
			}
			metadata = temp;
			metasize += 4096;
		}
		memcpy(&metadata[metaused], buffer, count + 1);
		metaused += count;

		/* if the dataset is a PDS then we need to process all members
		 * of the PDS, otherwise continue with the next dataset */
		if (!ispds)
			continue;

		rc = lzds_dataset_alloc_memberiterator(ds, &it);
		if (rc) {
			rc = -ENOMEM;
			goto error;
		}
		while (!lzds_memberiterator_get_next_member(it, &member)) {
			lzds_pdsmember_get_name(member, &mbrname);
			count = snprintf(buffer, sizeof(buffer),
					 "dsn=%s(%s),recfm=%s,lrecl=%u,"
					 "dsorg=PS\n",
					 dsname, mbrname, recfm, f1->DS1LRECL);
			if (count >= sizeof(buffer)) {
				count = sizeof(buffer) - 1;
				buffer[count] = 0;
			}
			if (metaused + count + 1 > metasize) {
				temp = realloc(metadata,  metasize + 4096);
				if (!temp) {
					rc = -ENOMEM;
					goto error;
				}
				metadata = temp;
				metasize += 4096;
			}
			memcpy(&metadata[metaused], buffer, count + 1);
			metaused += count;
		}
		lzds_memberiterator_free(it);
		it = NULL;
	}
	lzds_dsiterator_free(dsit);
	dsit = NULL;

	if (info->metadata)
		free(info->metadata);
	info->metadata = metadata;
	info->metasize = metasize;
	info->metaused = metaused;
	info->metatime = time(NULL);
	return 0;

error:
	free(metadata);
	lzds_dsiterator_free(dsit);
	lzds_memberiterator_free(it);
	return rc;
};



enum {
	KEY_HELP,
	KEY_VERSION,
	KEY_DEVFILE,
	KEY_TRACKS,
	KEY_SEEKBUFFER,
};

#define ZDSFS_OPT(t, p, v) { t, offsetof(struct zdsfs_info, p), v }

static const struct fuse_opt zdsfs_opts[] = {
	FUSE_OPT_KEY("-h",		KEY_HELP),
	FUSE_OPT_KEY("--help",		KEY_HELP),
	FUSE_OPT_KEY("-v",		KEY_VERSION),
	FUSE_OPT_KEY("--version",	KEY_VERSION),
	FUSE_OPT_KEY("-l %s",		KEY_DEVFILE),
	FUSE_OPT_KEY("tracks=",         KEY_TRACKS),
	FUSE_OPT_KEY("seekbuffer=",     KEY_SEEKBUFFER),
	ZDSFS_OPT("rdw",                keepRDW, 1),
	ZDSFS_OPT("ignore_incomplete",  allow_inclomplete_multi_volume, 1),
	FUSE_OPT_END
};


static void usage(const char *progname)
{
	fprintf(stderr,
"Usage: %s <devices> <mountpoint> [<options>]\n"
"\n"
"Use the zdsfs command to provide read access to data sets stored on one or\n"
"more z/OS DASD devices.\n\n"
"General options:\n"
"    -o opt,[opt...]        Mount options\n"
"    -h   --help            Print help, then exit\n"
"    -v   --version         Print version, then exit\n"
"\n"
"ZDSFS options:\n"
"    -l list_file           Text file that contains a list of DASD device"
" nodes\n"
"    -o rdw                 Keep record descriptor words in byte stream\n"
"    -o ignore_incomplete   Continue processing even if parts of a multi"
" volume\n"
"                           data set are missing\n"
"    -o tracks=N            Size of the track buffer in tracks (default 128)\n"
"    -o seekbuffer=S        Upper limit in bytes for the seek history buffer\n"
"                           size (default 1048576)\n", progname);
}

static void zdsfs_process_device(const char *device)
{
	struct dasd *newdasd;
	struct errorlog *log;
	int rc;

	rc = lzds_zdsroot_add_device(zdsfsinfo.zdsroot, device, &newdasd);
	if (rc) {
		fprintf(stderr, "error when adding device %s: %s\n", device,
			strerror(rc));
		lzds_zdsroot_get_errorlog(zdsfsinfo.zdsroot, &log);
		lzds_errorlog_fprint(log, stderr);
		exit(1);
	}
	zdsfsinfo.devcount++;
	rc = lzds_dasd_read_vlabel(newdasd);
	if (rc) {
		fprintf(stderr, "error when reading volume label from "
			"device %s: %s\n", device, strerror(rc));
		lzds_dasd_get_errorlog(newdasd, &log);
		lzds_errorlog_fprint(log, stderr);
		exit(1);
	}
	rc = lzds_dasd_read_rawvtoc(newdasd);
	if (rc) {
		fprintf(stderr, "error when reading VTOC from device %s:"
			" %s\n", device, strerror(rc));
		lzds_dasd_get_errorlog(newdasd, &log);
		lzds_errorlog_fprint(log, stderr);
		exit(1);
	}
	rc = lzds_zdsroot_extract_datasets_from_dasd(zdsfsinfo.zdsroot,
						     newdasd);
	if (rc) {
		fprintf(stderr, "error when extracting data sets from dasd %s:"
			" %s\n", device, strerror(rc));
		lzds_zdsroot_get_errorlog(zdsfsinfo.zdsroot, &log);
		lzds_errorlog_fprint(log, stderr);
		exit(1);
	}
}

static void zdsfs_process_device_file(const char *devfile)
{
	struct stat sb;
	int fd;
	ssize_t count;
	size_t size, residual;
	char *buffer;
	char *runbuf;
	char *token;

	fd = open(devfile, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "could not open file %s: %s\n",
			devfile, strerror(errno));
		exit(1);
	}
	if (fstat(fd, &sb) < 0) {
		fprintf(stderr, "could not stat file %s: %s\n",
			devfile, strerror(errno));
		exit(1);
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file %s\n", devfile);
		exit(1);
	}
	if (sb.st_size) {
		size = (size_t)sb.st_size + 1;
		buffer = malloc(size);
		bzero(buffer, size);
		if (!buffer) {
			fprintf(stderr, "could not allocate memory to buffer"
				" file %s\n", devfile);
			exit(1);
		}
	} else
		return;

	count = 0;
	residual = (size_t)sb.st_size;
	while (residual) {
		count = read(fd, buffer, residual);
		if (count < 0) {
			fprintf(stderr, "error when reading file %s: %s\n",
				devfile, strerror(errno));
			exit(1);
		}
		if (!count) /* EOF */
			residual = 0;
		residual -= count;
	}

	runbuf = buffer;
	while ((token = strsep(&runbuf, " \t\n"))) {
		/* if several delimiters follow after another, token points
		 * to an empty string
		 */
		if (!strlen(token))
			continue;

		if (stat(token, &sb) < 0) {
			fprintf(stderr, "could not stat device %s from"
				" device list, %s\n", token, strerror(errno));
			exit(1);
		}
		if (S_ISBLK(sb.st_mode))
			zdsfs_process_device(token);
	}
	free(buffer);
}

static int zdsfs_process_args(void *UNUSED(data), const char *arg, int key,
			      struct fuse_args *outargs)
{
	struct stat sb;
	unsigned long tracks_per_frame;
	unsigned long long seek_buffer_size;
	const char *value;
	char *endptr;

	switch (key) {
	case FUSE_OPT_KEY_OPT:
		return 1;
	case FUSE_OPT_KEY_NONOPT:
		if (stat(arg, &sb) < 0) {
			fprintf(stderr, "could not stat device %s, %s\n",
				arg, strerror(errno));
			return 1;
		}
		if (S_ISBLK(sb.st_mode)) {
			zdsfs_process_device(arg);
			return 0;
		}
		/* not a block device, so let fuse parse it */
		return 1;
	case KEY_DEVFILE:
		/* note that arg starts with "-l" */
		zdsfs_process_device_file(arg + 2);
		return 0;
	case KEY_TRACKS:
		value = arg + strlen("tracks=");
		/* strtoul does not complain about negative values  */
		if (*value == '-') {
			errno = EINVAL;
		} else {
			errno = 0;
			tracks_per_frame = strtoul(value, &endptr, 10);
		}
		if (!errno && tracks_per_frame <= UINT_MAX)
			zdsfsinfo.tracks_per_frame = tracks_per_frame;
		else
			errno = ERANGE;
		if (errno || (endptr && (*endptr != '\0'))) {
			fprintf(stderr, "Invalid value '%s' for option "
				"'tracks'\n", value);
			exit(1);
		}
		return 0;
	case KEY_SEEKBUFFER:
		value = arg + strlen("seekbuffer=");
		/* strtoull does not complain about negative values  */
		if (*value == '-') {
			errno = EINVAL;
		} else {
			errno = 0;
			seek_buffer_size = strtoull(value, &endptr, 10);
		}
		if (errno || (endptr && (*endptr != '\0'))) {
			fprintf(stderr,	"Invalid value '%s' for option "
				"'seekbuffer'\n", value);
			exit(1);
		}
		zdsfsinfo.seek_buffer_size = seek_buffer_size;
		return 0;
	case KEY_HELP:
		usage(outargs->argv[0]);
		fuse_opt_add_arg(outargs, "-ho");
		/* call fuse_main to let library print fuse options */
		fuse_main(outargs->argc, outargs->argv, &rdf_oper, NULL);
		exit(0);
	case KEY_VERSION:
		fprintf(stderr, COMP "FUSE file system for z/OS data set access"
			", program version %s\n", RELEASE_STRING);
		fprintf(stderr, "Copyright IBM Corp. 2013\n");
		exit(0);
	default:
		fprintf(stderr, "Unknown argument key %x\n", key);
		exit(1);
	}
}


int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	int rc;

	bzero(&zdsfsinfo, sizeof(zdsfsinfo));
	zdsfsinfo.keepRDW = 0;
	zdsfsinfo.allow_inclomplete_multi_volume = 0;
	zdsfsinfo.tracks_per_frame = 128;
	zdsfsinfo.seek_buffer_size = 1048576;

	rc = lzds_zdsroot_alloc(&zdsfsinfo.zdsroot);
	if (rc) {
		fprintf(stderr, "Could not allocate internal structures\n");
		exit(1);
	}
	if (fuse_opt_parse(&args, &zdsfsinfo, zdsfs_opts,
			   zdsfs_process_args) == -1) {
		fprintf(stderr, "Failed to parse option\n");
		exit(1);
	}

	if (!zdsfsinfo.devcount) {
		fprintf(stderr, "Please specify a block device\n");
		fprintf(stderr, "Try '%s --help' for more information\n",
			argv[0]);
		exit(1);
	}
	rc = zdsfs_verify_datasets();
	if (rc)
		goto cleanup;

	rc = zdsfs_create_meta_data_buffer(&zdsfsinfo);
	if (rc)
		goto cleanup;

	rc = fuse_main(args.argc, args.argv, &rdf_oper, NULL);

cleanup:
	lzds_zdsroot_free(zdsfsinfo.zdsroot);

	fuse_opt_free_args(&args);
	return rc;
}
