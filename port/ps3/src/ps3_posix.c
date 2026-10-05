/* The part of port/linux/src/posix.h that the reused msvc_crt.c calls, on newlib: its MSVC
_stat, _fstat, _chsize and _mkdir. */

#include "../../linux/src/posix.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void information_from_stat(const struct stat *status, struct posix_file_information *information)
{
	memset(information, 0, sizeof(*information));
	if (S_ISDIR(status->st_mode))
		information->flags |= _posix_file_is_directory;
	if (!(status->st_mode & S_IWUSR))
		information->flags |= _posix_file_is_read_only;
	information->size_low = (posix_ulong)status->st_size;
	information->size_high = (posix_ulong)((unsigned long long)status->st_size >> 32);
	information->modification_seconds = (posix_ulong)status->st_mtime;
	information->access_seconds = (posix_ulong)status->st_atime;
	information->creation_seconds = (posix_ulong)status->st_ctime;
}

int posix_stat(const char *path, struct posix_file_information *information)
{
	struct stat status;

	if (stat(path, &status) != 0)
		return -1;
	information_from_stat(&status, information);
	return 0;
}

int posix_fstat(int descriptor, struct posix_file_information *information)
{
	struct stat status;

	if (fstat(descriptor, &status) != 0)
		return -1;
	information_from_stat(&status, information);
	return 0;
}

int posix_truncate(int descriptor, posix_ulong size_low, posix_ulong size_high)
{
	return ftruncate(descriptor, (off_t)(((unsigned long long)size_high << 32) | size_low));
}

int posix_make_directory(const char *path)
{
	return mkdir(path, 0777);
}
