/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x500
#endif

#include "common.h"
#include "utils.h"
#include "obj-backend.h"

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#ifdef WIN32
#include <windows.h>
#include <io.h>
#endif

#define DEBUG_FLAG SEAFILE_DEBUG_OTHER
#include "log.h"

typedef struct FsPriv {
    char *v0_obj_dir;
    int v0_dir_len;
    char *obj_dir;
    int   dir_len;
} FsPriv;

static void
id_to_path (FsPriv *priv, const char *obj_id, char path[],
            const char *repo_id, int version)
{
    char *pos = path;
    int n;

#if defined MIGRATION || defined SEAFILE_CLIENT
    if (version > 0) {
        n = snprintf (path, SEAF_PATH_MAX, "%s/%s/", priv->obj_dir, repo_id);
        pos += n;
    } else {
        memcpy (pos, priv->v0_obj_dir, priv->v0_dir_len);
        pos[priv->v0_dir_len] = '/';
        pos += priv->v0_dir_len + 1;
    }
#else
    n = snprintf (path, SEAF_PATH_MAX, "%s/%s/", priv->obj_dir, repo_id);
    pos += n;
#endif

    memcpy (pos, obj_id, 2);
    pos[2] = '/';
    pos += 3;

    memcpy (pos, obj_id + 2, 41 - 2);
}

#ifdef WIN32
static int
win32_get_file_contents (const char *path, void **data, int *len)
{
    wchar_t *wpath;
    HANDLE h;
    LARGE_INTEGER lint;
    gint64 size;
    char *buf;
    DWORD nread;
    int ret = 0;

    wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);

    h = CreateFileW (wpath,
                     GENERIC_READ,
                     FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                     NULL,
                     OPEN_EXISTING,
                     0,
                     NULL);
    if (h == INVALID_HANDLE_VALUE) {
        seaf_warning ("Failed to open %s for read: %lu\n ",
                      path, GetLastError());
        ret = -1;
        goto out;
    }

    if (!GetFileSizeEx (h, &lint)) {
        seaf_warning ("Failed to GetFileSizeEx for %s: %lu\n",
                      path, GetLastError());
        ret = -1;
        goto out;
    }

    size = (gint64)(lint.QuadPart);
    buf = g_new0 (char, size);
    if (!buf) {
        seaf_warning ("Out of memory\n");
        ret = -1;
        goto out;
    }

    if (!ReadFile (h, buf, (DWORD)size, &nread, NULL)) {
        seaf_warning ("ReadFile failed %s: %lu\n", path, GetLastError());
        ret = -1;
        goto out;
    }
    if (nread != (DWORD)size) {
        seaf_warning ("Failed to read whole file %s\n", path);
        ret = -1;
        goto out;
    }

    *len = (int)size;
    *data = (void *)buf;

out:
    CloseHandle (h);
    g_free (wpath);
    if (ret < 0)
        g_free (buf);
    return ret;
}
#endif

static int
obj_backend_fs_read (ObjBackend *bend,
                     const char *repo_id,
                     int version,
                     const char *obj_id,
                     void **data,
                     int *len)
{
    char path[SEAF_PATH_MAX];

    id_to_path (bend->priv, obj_id, path, repo_id, version);

    /* seaf_debug ("object path: %s\n", path); */

#ifndef WIN32

    gsize tmp_len;
    GError *error = NULL;

    g_file_get_contents (path, (gchar**)data, &tmp_len, &error);
    if (error) {
#ifdef MIGRATION
        g_clear_error (&error);
        id_to_path (bend->priv, obj_id, path, repo_id, 1);
        g_file_get_contents (path, (gchar**)data, &tmp_len, &error);
        if (error) {
            seaf_debug ("[obj backend] Failed to read object %s: %s.\n",
                        obj_id, error->message);
            g_clear_error (&error);
            return -1;
        }
#else
        seaf_debug ("[obj backend] Failed to read object %s: %s.\n",
                    obj_id, error->message);
        g_clear_error (&error);
        return -1;
#endif
    }

    *len = (int)tmp_len;
    return 0;

#else

    return win32_get_file_contents (path, data, len);
#endif
}

#ifndef WIN32
/*
 * Flush operating system and disk caches for @fd.
 */
static int
fsync_obj_contents (int fd)
{
#ifdef __linux__
    /* Some file systems may not support fsync().
     * In this case, just skip the error.
     */
    if (fsync (fd) < 0) {
        if (errno == EINVAL)
            return 0;
        else {
            seaf_warning ("Failed to fsync: %s.\n", strerror(errno));
            return -1;
        }
    }
    return 0;
#endif

#ifdef __APPLE__
    /* OS X: fcntl() is required to flush disk cache, fsync() only
     * flushes operating system cache.
     */
    if (fcntl (fd, F_FULLFSYNC, NULL) < 0) {
        seaf_warning ("Failed to fsync: %s.\n", strerror(errno));
        return -1;
    }
    return 0;
#endif
}
#endif  /* WIN32 */

/*
 * Rename file from @tmp_path to @obj_path.
 * This also makes sure the changes to @obj_path's parent folder
 * is flushed to disk.
 */
static int
rename_and_sync (const char *tmp_path, const char *obj_path)
{
#ifdef __linux__
    char *parent_dir;
    int ret = 0;

    if (rename (tmp_path, obj_path) < 0) {
        seaf_warning ("Failed to rename from %s to %s: %s.\n",
                      tmp_path, obj_path, strerror(errno));
        return -1;
    }

    parent_dir = g_path_get_dirname (obj_path);
    int dir_fd = open (parent_dir, O_RDONLY);
    if (dir_fd < 0) {
        seaf_warning ("Failed to open dir %s: %s.\n", parent_dir, strerror(errno));
        goto out;
    }

    /* Some file systems don't support fsyncing a directory. Just ignore the error.
     */
    if (fsync (dir_fd) < 0) {
        if (errno != EINVAL) {
            seaf_warning ("Failed to fsync dir %s: %s.\n",
                          parent_dir, strerror(errno));
            ret = -1;
        }
        goto out;
    }

out:
    g_free (parent_dir);
    if (dir_fd >= 0)
        close (dir_fd);
    return ret;
#endif

#ifdef __APPLE__
    /*
     * OS X garantees an existence of obj_path always exists,
     * even when the system crashes.
     */
    if (rename (tmp_path, obj_path) < 0) {
        seaf_warning ("Failed to rename from %s to %s: %s.\n",
                      tmp_path, obj_path, strerror(errno));
        return -1;
    }
    return 0;
#endif

#ifdef WIN32
    wchar_t *w_tmp_path = g_utf8_to_utf16 (tmp_path, -1, NULL, NULL, NULL);
    wchar_t *w_obj_path = g_utf8_to_utf16 (obj_path, -1, NULL, NULL, NULL);
    int ret = 0;

    if (!MoveFileExW (w_tmp_path, w_obj_path,
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        seaf_warning ("MoveFilExW failed: %lu.\n", GetLastError());
        ret = -1;
        goto out;
    }

out:
    g_free (w_tmp_path);
    g_free (w_obj_path);
    return ret;
#endif
}

static int
create_parent_dir (ObjBackend *bend,
                   const char *repo_id,
                   int version,
                   const char *obj_id)
{
    FsPriv *priv = bend->priv;
    /* repo_id(36) + / + obj_prefix(2) + '\0' */
    char new_path[40];

    if (version > 0)
        snprintf (new_path, sizeof(new_path), "%s/%.2s", repo_id, obj_id);
    else
        snprintf (new_path, sizeof(new_path), "%.2s", obj_id);

    return seaf_util_mkdir_with_parents (priv->obj_dir, new_path, 0777);
}

#ifndef WIN32

static int
save_obj_contents (ObjBackend *bend,
                   const char *repo_id,
                   int version,
                   const char *obj_id,
                   const void *data,
                   int len,
                   gboolean need_sync)
{
    char path[SEAF_PATH_MAX];
    char tmp_path[SEAF_PATH_MAX];
    int fd;

    id_to_path (bend->priv, obj_id, path, repo_id, version);

    if (!create_parent_dir (bend, repo_id, version, obj_id) < 0)
        return -1;

    snprintf (tmp_path, SEAF_PATH_MAX, "%s.XXXXXX", path);
    fd = g_mkstemp (tmp_path);
    if (fd < 0) {
        seaf_warning ("[obj backend] Failed to open tmp file %s: %s.\n",
                      tmp_path, strerror(errno));
        return -1;
    }

    if (writen (fd, data, len) < 0) {
        seaf_warning ("[obj backend] Failed to write obj %s: %s.\n",
                      tmp_path, strerror(errno));
        return -1;
    }

    if (need_sync && fsync_obj_contents (fd) < 0)
        return -1;

    /* Close may return error, especially in NFS. */
    if (close (fd) < 0) {
        seaf_warning ("[obj backend Failed close obj %s: %s.\n",
                      tmp_path, strerror(errno));
        return -1;
    }

    if (need_sync) {
        if (rename_and_sync (tmp_path, path) < 0)
            return -1;
    } else {
        if (g_rename (tmp_path, path) < 0) {
            seaf_warning ("[obj backend] Failed to rename %s: %s.\n",
                          path, strerror(errno));
            return -1;
        }
    }

    return 0;
}

#else

static int
create_temp_path (ObjBackend *bend,
                  const char *repo_id,
                  int version,
                  const char *obj_id,
                  char **temp_path)
{
    FsPriv *priv = bend->priv;
    /* repo_id(36) + / + obj_prefix(2) + '\0' */
    char new_path[40];
    int ret = 0;
    wchar_t temp_path_w[MAX_PATH];
    char *temp_path_parent = NULL;
    wchar_t *temp_path_parent_w, *prefix_str_w;

    if (version > 0)
        snprintf (new_path, sizeof(new_path), "%s/%.2s", repo_id, obj_id);
    else
        snprintf (new_path, sizeof(new_path), "%.2s", obj_id);

    ret = seaf_util_mkdir_with_parents (priv->obj_dir, new_path, 0777);
    if (ret < 0) {
        seaf_warning ("Failed to create parent path for object %s:%s.\n",
                      repo_id, obj_id);
        return ret;
    }

    temp_path_parent = g_build_path ("/", priv->obj_dir, new_path, NULL);
    temp_path_parent_w = g_utf8_to_utf16 (temp_path_parent, -1, NULL, NULL, NULL);
    prefix_str_w = g_utf8_to_utf16 (obj_id + 2, -1, NULL, NULL, NULL);

    if (!GetTempFileNameW (temp_path_parent_w, prefix_str_w, 0, temp_path_w)) {
        seaf_warning ("Failed to GetTempFileNameW: %lu\n", GetLastError());
        ret = -1;
        goto out;
    }

    *temp_path = g_utf16_to_utf8 (temp_path_w, -1, NULL, NULL, NULL);

out:
    g_free (temp_path_parent);
    g_free (temp_path_parent_w);
    g_free (prefix_str_w);
    return ret;
}

static int
save_obj_contents (ObjBackend *bend,
                   const char *repo_id,
                   int version,
                   const char *obj_id,
                   const void *data,
                   int len,
                   gboolean need_sync)
{
    char path[SEAF_PATH_MAX];
    char *temp_path = NULL;
    wchar_t *temp_path_w = NULL;
    HANDLE h;
    int ret = 0;

    id_to_path (bend->priv, obj_id, path, repo_id, version);

    if (create_temp_path (bend, repo_id, version, obj_id, &temp_path) < 0) {
        return -1;
    }

    temp_path_w = g_utf8_to_utf16 (temp_path, -1, NULL, NULL, NULL);
    h = CreateFileW (temp_path_w,
                     GENERIC_WRITE,
                     0,
                     NULL,
                     CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL,
                     NULL);
    if (h == INVALID_HANDLE_VALUE) {
        seaf_warning ("Failed to CreateFileW: %lu\n", GetLastError());
        ret = -1;
        goto out;
    }

    DWORD n;
    if (!WriteFile (h, data, len, &n, NULL)) {
        seaf_warning ("Failed to write object %s:%s: %lu\n",
                      repo_id, obj_id, GetLastError());
        ret = -1;
        CloseHandle (h);
        goto out;
    }

    if (need_sync) {
        if (!FlushFileBuffers (h)) {
            seaf_warning ("FlushFileBuffer() failed: %lu.\n", GetLastError());
            CloseHandle (h);
            ret = -1;
            goto out;
        }
    }

    if (!CloseHandle (h)) {
        seaf_warning ("CloseHandle() failed: %lu\n", GetLastError());
        ret = -1;
        goto out;
    }

    if (need_sync) {
        if (rename_and_sync (temp_path, path) < 0) {
            ret = -1;
        }
    } else {
        if (g_rename (temp_path, path) < 0) {
            seaf_warning ("[obj backend] Failed to rename %s: %s.\n",
                          path, strerror(errno));
            ret = -1;
        }
    }

out:
    g_free (temp_path);
    g_free (temp_path_w);
    return 0;
}

#endif

static int
obj_backend_fs_write (ObjBackend *bend,
                      const char *repo_id,
                      int version,
                      const char *obj_id,
                      void *data,
                      int len,
                      gboolean need_sync)
{
    if (save_obj_contents (bend, repo_id, version, obj_id, data, len, need_sync) < 0) {
        seaf_warning ("[obj backend] Failed to write obj %s:%s.\n",
                      repo_id, obj_id);
        return -1;
    }

    return 0;
}

static gboolean
obj_backend_fs_exists (ObjBackend *bend,
                       const char *repo_id,
                       int version,
                       const char *obj_id)
{
    char path[SEAF_PATH_MAX];
    SeafStat st;

    id_to_path (bend->priv, obj_id, path, repo_id, version);

    if (seaf_stat (path, &st) == 0)
        return TRUE;

    return FALSE;
}

static void
obj_backend_fs_delete (ObjBackend *bend,
                       const char *repo_id,
                       int version,
                       const char *obj_id)
{
    char path[SEAF_PATH_MAX];

    id_to_path (bend->priv, obj_id, path, repo_id, version);
    seaf_util_unlink (path);
}

static int
obj_backend_fs_foreach_obj (ObjBackend *bend,
                            const char *repo_id,
                            int version,
                            SeafObjFunc process,
                            void *user_data)
{
    FsPriv *priv = bend->priv;
    char *obj_dir = NULL;
    int dir_len;
    GDir *dir1 = NULL, *dir2;
    const char *dname1, *dname2;
    char obj_id[128];
    char path[SEAF_PATH_MAX], *pos;
    int ret = 0;

#if defined MIGRATION || defined SEAFILE_CLIENT
    if (version > 0)
        obj_dir = g_build_filename (priv->obj_dir, repo_id, NULL);
    else
        obj_dir = g_strdup(priv->v0_obj_dir);
#else
    obj_dir = g_build_filename (priv->obj_dir, repo_id, NULL);
#endif
    dir_len = strlen (obj_dir);

    dir1 = g_dir_open (obj_dir, 0, NULL);
    if (!dir1) {
        goto out;
    }

    memcpy (path, obj_dir, dir_len);
    pos = path + dir_len;

    while ((dname1 = g_dir_read_name(dir1)) != NULL) {
        snprintf (pos, sizeof(path) - dir_len, "/%s", dname1);

        dir2 = g_dir_open (path, 0, NULL);
        if (!dir2) {
            seaf_warning ("Failed to open object dir %s.\n", path);
            continue;
        }

        while ((dname2 = g_dir_read_name(dir2)) != NULL) {
            snprintf (obj_id, sizeof(obj_id), "%s%s", dname1, dname2);
            if (!process (repo_id, version, obj_id, user_data)) {
                g_dir_close (dir2);
                goto out;
            }
        }
        g_dir_close (dir2);
    }

out:
    if (dir1)
        g_dir_close (dir1);
    g_free (obj_dir);

    return ret;
}

static int
obj_backend_fs_copy (ObjBackend *bend,
                     const char *src_repo_id,
                     int src_version,
                     const char *dst_repo_id,
                     int dst_version,
                     const char *obj_id)
{
    char src_path[SEAF_PATH_MAX];
    char dst_path[SEAF_PATH_MAX];

    id_to_path (bend->priv, obj_id, src_path, src_repo_id, src_version);
    id_to_path (bend->priv, obj_id, dst_path, dst_repo_id, dst_version);

    if (g_file_test (dst_path, G_FILE_TEST_EXISTS))
        return 0;

    if (create_parent_dir (bend, dst_repo_id, dst_version, obj_id) < 0) {
        seaf_warning ("Failed to create dst path %s for obj %s.\n",
                      dst_path, obj_id);
        return -1;
    }

#ifdef WIN32
    if (!CreateHardLink (dst_path, src_path, NULL)) {
        seaf_warning ("Failed to link %s to %s: %d.\n",
                      src_path, dst_path, GetLastError());
        return -1;
    }
    return 0;
#else
    int ret = link (src_path, dst_path);
    if (ret < 0 && errno != EEXIST) {
        seaf_warning ("Failed to link %s to %s: %s.\n",
                      src_path, dst_path, strerror(errno));
        return -1;
    }
    return ret;
#endif
}

ObjBackend *
obj_backend_fs_new (const char *seaf_dir, const char *obj_type)
{
    ObjBackend *bend;
    FsPriv *priv;

    bend = g_new0(ObjBackend, 1);
    priv = g_new0(FsPriv, 1);
    bend->priv = priv;

    priv->v0_obj_dir = g_build_filename (seaf_dir, obj_type, NULL);
    priv->v0_dir_len = strlen(priv->v0_obj_dir);

    priv->obj_dir = g_build_filename (seaf_dir, "storage", obj_type, NULL);
    priv->dir_len = strlen (priv->obj_dir);

    if (g_mkdir_with_parents (priv->v0_obj_dir, 0777) < 0) {
        seaf_warning ("[Obj Backend] Objects dir %s does not exist and"
                   " is unable to create\n", priv->v0_obj_dir);
        goto onerror;
    }

    if (g_mkdir_with_parents (priv->obj_dir, 0777) < 0) {
        seaf_warning ("[Obj Backend] Objects dir %s does not exist and"
                   " is unable to create\n", priv->obj_dir);
        goto onerror;
    }

    bend->read = obj_backend_fs_read;
    bend->write = obj_backend_fs_write;
    bend->exists = obj_backend_fs_exists;
    bend->delete = obj_backend_fs_delete;
    bend->foreach_obj = obj_backend_fs_foreach_obj;
    bend->copy = obj_backend_fs_copy;

    return bend;

onerror:
    g_free (priv->v0_obj_dir);
    g_free (priv->obj_dir);
    g_free (priv);
    g_free (bend);

    return NULL;
}
