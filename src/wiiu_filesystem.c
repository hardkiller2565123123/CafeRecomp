#include "wiiu_filesystem.h"
#include "wiiu_memory.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define HOST_PATH_SEPARATOR '\\'
#define host_stat _stat64
#define host_stat_t struct _stat64
#define host_fseek _fseeki64
#define host_ftell _ftelli64
#else
#include <dirent.h>
#include <unistd.h>
#define HOST_PATH_SEPARATOR '/'
#define host_stat stat
#define host_stat_t struct stat
#define host_fseek fseeko
#define host_ftell ftello
#endif

enum {
    FS_MAX_FILES = 64,
    FS_MAX_DIRS = 32,
    FS_MAX_PATH = 1024,
    FS_LOG_LIMIT = 160,
    FS_STATUS_OK = 0,
    FS_STATUS_END = -2,
    FS_STATUS_NOT_FOUND = -6,
    FS_STATUS_NOT_FILE = -7,
    FS_STATUS_NOT_DIR = -8,
    FS_STATUS_ACCESS_ERROR = -9,
    FS_STATUS_STORAGE_FULL = -12,
    FS_STATUS_UNSUPPORTED = -14,
    FS_STAT_DIRECTORY = 0x80000000u,
    FS_STAT_FILE = 0x01000000u,
    FS_FILE_HANDLE_BASE = 0x100u,
    FS_DIR_HANDLE_BASE = 0x10000u,
    FS_IO_LIMIT = 64 * 1024 * 1024,
};

typedef struct {
    FILE* stream;
    char path[FS_MAX_PATH];
} HostFile;

typedef struct {
    bool used;
    char path[FS_MAX_PATH];
#ifdef _WIN32
    intptr_t search;
    struct __finddata64_t entry;
    bool entry_ready;
#else
    DIR* stream;
#endif
} HostDir;

static char g_title_root[FS_MAX_PATH] = ".";
static char g_base_root[FS_MAX_PATH] = ".";
static char g_dlc_root[FS_MAX_PATH] = ".";
static char g_mlc_root[FS_MAX_PATH] = ".";
static char g_current_dir[FS_MAX_PATH] = "/vol/content";
static HostFile g_files[FS_MAX_FILES];
static HostDir g_dirs[FS_MAX_DIRS];
static u32 g_fs_log_count;
static u32 g_asset_open_count;
static u32 g_asset_missing_count;
static u32 g_system_asset_missing_count;
static u64 g_asset_bytes_read;
static u64 g_dlc_probe_count;
static char g_last_read_path[FS_MAX_PATH];

static bool name_is(const char* name, const char* expected) {
    return name && expected && strcmp(name, expected) == 0;
}

static bool name_starts_with(const char* name, const char* prefix) {
    return name && prefix && strncmp(name, prefix, strlen(prefix)) == 0;
}

static void return_to_lr(CPUState* cpu) {
    cpu->pc = cpu->lr & ~3u;
}

static void write_guest_u64(CPUState* cpu, u32 address, u64 value) {
    mem_write32(cpu, address, (u32)(value >> 32));
    mem_write32(cpu, address + 4u, (u32)value);
}

static bool read_guest_string(CPUState* cpu, u32 address, char* out,
                              size_t out_size) {
    if (!out || out_size == 0)
        return false;

    out[0] = 0;
    if (address == 0)
        return false;

    for (size_t i = 0; i + 1 < out_size; i++) {
        u8 ch = mem_read8(cpu, address + (u32)i);
        out[i] = (char)ch;
        if (ch == 0)
            return true;
        if (ch < 0x20u)
            return false;
    }

    out[out_size - 1] = 0;
    return false;
}

static bool write_guest_string(CPUState* cpu, u32 address, u32 capacity,
                               const char* value) {
    if (!address || !capacity || !value)
        return false;

    size_t length = strlen(value);
    if (length + 1u > capacity)
        return false;

    for (size_t i = 0; i <= length; i++)
        mem_write8(cpu, address + (u32)i, (u8)value[i]);
    return true;
}

static void trim_last_component(char* path) {
    size_t length = strlen(path);
    while (length > 0 && (path[length - 1] == '/' || path[length - 1] == '\\'))
        path[--length] = 0;

    while (length > 0) {
        if (path[length - 1] == '/' || path[length - 1] == '\\') {
            path[length - 1] = 0;
            return;
        }
        length--;
    }

    snprintf(path, FS_MAX_PATH, ".");
}

static bool host_path_exists(const char* path) {
    host_stat_t info;
    return path && path[0] && host_stat(path, &info) == 0;
}

static bool host_path_is_dir(const char* path) {
    host_stat_t info;
    if (!path || !path[0] || host_stat(path, &info) != 0)
        return false;
#ifdef _WIN32
    return (info.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(info.st_mode);
#endif
}

static void set_absolute_path(char* destination, size_t destination_size,
                              const char* source) {
#ifdef _WIN32
    if (!_fullpath(destination, source, destination_size))
        snprintf(destination, destination_size, "%s", source);
#else
    if (!realpath(source, destination))
        snprintf(destination, destination_size, "%s", source);
#endif
}

static void select_mlc_root(void) {
    const char* configured = getenv("BOTW_WIIU_MLC_ROOT");
    if (configured && configured[0]) {
        set_absolute_path(g_mlc_root, sizeof(g_mlc_root), configured);
        fprintf(stderr, "filesystem: Wii U system root %s (configured)\n",
                g_mlc_root);
        return;
    }

    char candidate[FS_MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s%cmlc01", g_title_root,
             HOST_PATH_SEPARATOR);
    if (host_path_exists(candidate)) {
        set_absolute_path(g_mlc_root, sizeof(g_mlc_root), candidate);
        fprintf(stderr, "filesystem: Wii U system root %s\n", g_mlc_root);
        return;
    }

#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    const char* local_appdata = getenv("LOCALAPPDATA");
    const char* bases[] = {appdata, local_appdata};
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        if (!bases[i] || !bases[i][0])
            continue;
        snprintf(candidate, sizeof(candidate), "%s%cCemu%cmlc01", bases[i],
                 HOST_PATH_SEPARATOR, HOST_PATH_SEPARATOR);
        if (host_path_exists(candidate)) {
            set_absolute_path(g_mlc_root, sizeof(g_mlc_root), candidate);
            fprintf(stderr, "filesystem: Wii U system root %s\n",
                    g_mlc_root);
            return;
        }
    }
#endif

    snprintf(g_mlc_root, sizeof(g_mlc_root), "%s%cmlc01", g_title_root,
             HOST_PATH_SEPARATOR);
    fprintf(stderr,
            "filesystem: Wii U system root %s (create from your console dump)\n",
            g_mlc_root);
}

static void set_title_root(char* destination, size_t destination_size,
                           const char* source) {
    if (!source || !source[0]) {
        snprintf(destination, destination_size, ".");
        return;
    }
    set_absolute_path(destination, destination_size, source);
}

void wiiu_filesystem_set_title_paths(const char* base_title_dir,
                                     const char* update_title_dir,
                                     const char* dlc_title_dir) {
    set_title_root(g_base_root, sizeof(g_base_root), base_title_dir);
    set_title_root(g_title_root, sizeof(g_title_root), update_title_dir);
    set_title_root(g_dlc_root, sizeof(g_dlc_root), dlc_title_dir);

    if (!host_path_exists(g_title_root) && host_path_exists(g_base_root))
        snprintf(g_title_root, sizeof(g_title_root), "%s", g_base_root);

    fprintf(stderr, "filesystem: base root %s\n", g_base_root);
    fprintf(stderr, "filesystem: update root %s\n", g_title_root);
    fprintf(stderr, "filesystem: DLC root %s\n", g_dlc_root);
    select_mlc_root();
}

bool wiiu_filesystem_has_dlc(void) {
    ++g_dlc_probe_count;
    char content[FS_MAX_PATH];
    snprintf(content, sizeof(content), "%s%ccontent", g_dlc_root,
             HOST_PATH_SEPARATOR);
    return host_path_is_dir(content);
}

u64 wiiu_filesystem_dlc_probe_count(void) {
    return g_dlc_probe_count;
}

static void close_file_slot(HostFile* file) {
    if (file->stream)
        fclose(file->stream);
    memset(file, 0, sizeof(*file));
}

static void close_dir_slot(HostDir* dir) {
    if (!dir->used)
        return;
#ifdef _WIN32
    if (dir->search != -1)
        _findclose(dir->search);
#else
    if (dir->stream)
        closedir(dir->stream);
#endif
    memset(dir, 0, sizeof(*dir));
#ifdef _WIN32
    dir->search = -1;
#endif
}

void wiiu_filesystem_reset(void) {
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        close_file_slot(&g_files[i]);
    for (u32 i = 0; i < FS_MAX_DIRS; i++)
        close_dir_slot(&g_dirs[i]);
    g_fs_log_count = 0;
    g_asset_open_count = 0;
    g_asset_missing_count = 0;
    g_system_asset_missing_count = 0;
    g_asset_bytes_read = 0;
    g_dlc_probe_count = 0;
    g_last_read_path[0] = 0;
    snprintf(g_current_dir, sizeof(g_current_dir), "%s", "/vol/content");
}

bool wiiu_filesystem_read_last_file(u32 expected_size, u8** data_out,
                                    size_t* size_out) {
    if (!data_out || !size_out || !g_last_read_path[0])
        return false;

    *data_out = NULL;
    *size_out = 0;
    FILE* file = fopen(g_last_read_path, "rb");
    if (!file)
        return false;
    if (host_fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    s64 length = (s64)host_ftell(file);
    if (length <= 0 || (expected_size != 0u &&
                        (u64)length != (u64)expected_size) ||
        host_fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    size_t size = (size_t)length;
    if ((s64)size != length) {
        fclose(file);
        return false;
    }

    u8* data = (u8*)malloc(size);
    if (!data || fread(data, 1u, size, file) != size) {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);
    *data_out = data;
    *size_out = size;
    return true;
}

void wiiu_filesystem_shutdown(void) {
    fprintf(stderr,
            "filesystem: asset summary opened=%u title_missing=%u "
            "system_missing=%u bytes=%llu\n",
            g_asset_open_count, g_asset_missing_count,
            g_system_asset_missing_count,
            (unsigned long long)g_asset_bytes_read);
    wiiu_filesystem_reset();
}

static bool path_has_parent_component(const char* path) {
    const char* cursor = path;
    while (*cursor) {
        while (*cursor == '/' || *cursor == '\\')
            cursor++;
        const char* start = cursor;
        while (*cursor && *cursor != '/' && *cursor != '\\')
            cursor++;
        if ((cursor - start) == 2 && start[0] == '.' && start[1] == '.')
            return true;
    }
    return false;
}

static bool map_guest_path_from_root(const char* guest, const char* title_root,
                                     char* host, size_t host_size) {
    if (!guest || !guest[0] || path_has_parent_component(guest))
        return false;

    const char* area = "content";
    const char* root = title_root;
    const char* relative = guest;

    if (strncmp(guest, "/vol/content", 12) == 0) {
        relative = guest + 12;
    } else if (strncmp(guest, "/vol/code", 9) == 0) {
        area = "code";
        relative = guest + 9;
    } else if (strncmp(guest, "/vol/meta", 9) == 0) {
        area = "meta";
        relative = guest + 9;
    } else if (strncmp(guest, "/vol/aoc", 8) == 0 ||
               strncmp(guest, "/vol/storage_aoc", 16) == 0) {
        const char* volume_end = strchr(guest + 5, '/');
        root = g_dlc_root;
        area = "content";
        relative = volume_end ? volume_end + 1 : guest + strlen(guest);
    } else if (strncmp(guest, "/vol/save", 9) == 0) {
        area = "save";
        relative = guest + 9;
    } else if (strncmp(guest, "/vol/storage_mlc01/usr/save", 27) == 0) {
        area = "save";
        relative = guest + 27;
    } else if (strncmp(guest, "/vol/storage_mlc01/sys/title", 28) == 0) {
        root = g_mlc_root;
        area = NULL;
        relative = guest + strlen("/vol/storage_mlc01/");
    } else {
        while (*relative == '/' || *relative == '\\')
            relative++;
    }

    while (*relative == '/' || *relative == '\\')
        relative++;

    int written = area ? snprintf(host, host_size, "%s%c%s", root,
                                  HOST_PATH_SEPARATOR, area)
                       : snprintf(host, host_size, "%s", root);
    if (written < 0 || (size_t)written >= host_size)
        return false;

    size_t cursor = (size_t)written;
    if (*relative) {
        if (cursor + 1 >= host_size)
            return false;
        host[cursor++] = HOST_PATH_SEPARATOR;
        for (; *relative; relative++) {
            if (cursor + 1 >= host_size)
                return false;
            char ch = *relative;
            host[cursor++] = (ch == '/' || ch == '\\')
                                 ? HOST_PATH_SEPARATOR
                                 : ch;
        }
    }
    host[cursor] = 0;
    return true;
}

static bool map_guest_path(const char* guest, char* host, size_t host_size) {
    return map_guest_path_from_root(guest, g_title_root, host, host_size);
}

static bool guest_path_uses_title_overlay(const char* guest) {
    if (!guest || !guest[0])
        return false;
    return strncmp(guest, "/vol/content", 12) == 0 ||
           strncmp(guest, "/vol/code", 9) == 0 ||
           strncmp(guest, "/vol/meta", 9) == 0 || guest[0] != '/';
}

static bool try_base_fallback(const char* guest, char* host,
                              size_t host_size) {
    if (!guest_path_uses_title_overlay(guest) ||
        strcmp(g_base_root, g_title_root) == 0 ||
        !map_guest_path_from_root(guest, g_base_root, host, host_size)) {
        return false;
    }
    return host_path_exists(host);
}

static bool try_localized_fallback(const char* guest, char* host,
                                   size_t host_size) {
    const char* localized = strstr(guest, "LocalizedData/");
    if (!localized)
        localized = strstr(guest, "LocalizedData\\");
    if (!localized)
        return false;

    const char* locale = localized + strlen("LocalizedData/");
    const char* locale_end = locale;
    while (*locale_end && *locale_end != '/' && *locale_end != '\\')
        locale_end++;
    size_t locale_length = (size_t)(locale_end - locale);
    if (locale_length == 0 ||
        (locale_length == 4u && strncmp(locale, "UsEn", 4u) == 0))
        return false;

    char fallback_guest[FS_MAX_PATH];
    size_t prefix = (size_t)(locale - guest);
    int written = snprintf(fallback_guest, sizeof(fallback_guest), "%.*sUsEn%s",
                           (int)prefix, guest, locale_end);
    if (written < 0 || (size_t)written >= sizeof(fallback_guest) ||
        !map_guest_path(fallback_guest, host, host_size)) {
        return false;
    }

    host_stat_t info;
    if (host_stat(host, &info) != 0) {
        if (!try_base_fallback(fallback_guest, host, host_size) ||
            host_stat(host, &info) != 0) {
            return false;
        }
    }
    if (g_fs_log_count++ < FS_LOG_LIMIT) {
        fprintf(stderr, "filesystem: localized fallback %s -> %s\n", guest,
                fallback_guest);
    }
    return true;
}

static bool map_guest_address(CPUState* cpu, u32 guest_address,
                              char* guest, size_t guest_size,
                              char* host, size_t host_size) {
    return read_guest_string(cpu, guest_address, guest, guest_size) &&
           map_guest_path(guest, host, host_size);
}

static int host_make_dir(const char* path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static void ensure_parent_dirs(const char* path) {
    char scratch[FS_MAX_PATH];
    snprintf(scratch, sizeof(scratch), "%s", path);

    size_t start = 1;
#ifdef _WIN32
    if (scratch[0] && scratch[1] == ':')
        start = 3;
#endif
    for (size_t i = start; scratch[i]; i++) {
        if (scratch[i] != '/' && scratch[i] != '\\')
            continue;
        char saved = scratch[i];
        scratch[i] = 0;
        if (scratch[0])
            host_make_dir(scratch);
        scratch[i] = saved;
    }
}

static bool mode_can_write(const char* mode) {
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') ||
                    strchr(mode, '+'));
}

static void make_binary_mode(const char* guest_mode, char* host_mode,
                             size_t host_mode_size) {
    size_t cursor = 0;
    for (size_t i = 0; guest_mode && guest_mode[i] &&
                       cursor + 2 < host_mode_size; i++) {
        char ch = guest_mode[i];
        if (ch == 'r' || ch == 'w' || ch == 'a' || ch == '+')
            host_mode[cursor++] = ch;
    }
    if (cursor == 0)
        host_mode[cursor++] = 'r';
    host_mode[cursor++] = 'b';
    host_mode[cursor] = 0;
}

static s32 stat_host_path(CPUState* cpu, const char* path, u32 guest_stat) {
    host_stat_t info;
    if (host_stat(path, &info) != 0)
        return errno == ENOENT ? FS_STATUS_NOT_FOUND : FS_STATUS_ACCESS_ERROR;

    if (guest_stat) {
        for (u32 i = 0; i < 0x64u; i++)
            mem_write8(cpu, guest_stat + i, 0);

#ifdef _WIN32
        bool is_dir = (info.st_mode & _S_IFDIR) != 0;
#else
        bool is_dir = S_ISDIR(info.st_mode);
#endif
        u64 size = is_dir ? 0u : (u64)info.st_size;
        mem_write32(cpu, guest_stat + 0x00u,
                    is_dir ? FS_STAT_DIRECTORY : FS_STAT_FILE);
        mem_write32(cpu, guest_stat + 0x04u, is_dir ? 0777u : 0666u);
        mem_write32(cpu, guest_stat + 0x10u,
                    size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)size);
        mem_write32(cpu, guest_stat + 0x14u,
                    size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)size);
        write_guest_u64(cpu, guest_stat + 0x24u, (u64)info.st_ctime);
        write_guest_u64(cpu, guest_stat + 0x2Cu, (u64)info.st_mtime);
    }
    return FS_STATUS_OK;
}

static HostFile* find_file(u32 handle) {
    if (handle < FS_FILE_HANDLE_BASE)
        return NULL;
    u32 index = handle - FS_FILE_HANDLE_BASE;
    if (index >= FS_MAX_FILES || !g_files[index].stream)
        return NULL;
    return &g_files[index];
}

static HostDir* find_dir(u32 handle) {
    if (handle < FS_DIR_HANDLE_BASE)
        return NULL;
    u32 index = handle - FS_DIR_HANDLE_BASE;
    if (index >= FS_MAX_DIRS || !g_dirs[index].used)
        return NULL;
    return &g_dirs[index];
}

/* The current local account service exposes slot 1. SAVE paths are relative
   to that account (or the shared slot), never to /vol/content. */
static bool read_save_path(CPUState* cpu, u32 slot, u32 address,
                           char* path, size_t capacity) {
    char relative[FS_MAX_PATH];
    slot &= 0xFFu;
    if ((slot != 1u && slot != 0xFFu) ||
        !read_guest_string(cpu, address, relative, sizeof(relative)))
        return false;
    int length = snprintf(path, capacity, "/vol/save/%s/%s",
                          slot == 0xFFu ? "common" : "80000001", relative);
    return length >= 0 && (size_t)length < capacity;
}

static s32 open_host_file_path(CPUState* cpu, const char* guest_path,
                          u32 guest_mode_address, u32 guest_handle_address) {
    char host_path[FS_MAX_PATH];
    char guest_mode[32];
    char host_mode[32];
    if (!map_guest_path(guest_path, host_path, sizeof(host_path)) ||
        !read_guest_string(cpu, guest_mode_address, guest_mode,
                           sizeof(guest_mode))) {
        return FS_STATUS_NOT_FOUND;
    }

    make_binary_mode(guest_mode, host_mode, sizeof(host_mode));
    if (mode_can_write(guest_mode))
        ensure_parent_dirs(host_path);

    FILE* stream = fopen(host_path, host_mode);
    if (!stream && !mode_can_write(guest_mode) &&
        try_base_fallback(guest_path, host_path, sizeof(host_path))) {
        stream = fopen(host_path, host_mode);
    }
    if (!stream && !mode_can_write(guest_mode) &&
        try_localized_fallback(guest_path, host_path, sizeof(host_path))) {
        stream = fopen(host_path, host_mode);
    }
    if (!stream) {
        bool system_asset =
            strncmp(guest_path, "/vol/storage_mlc01/sys/title/",
                    strlen("/vol/storage_mlc01/sys/title/")) == 0;
        if (system_asset)
            g_system_asset_missing_count++;
        else
            g_asset_missing_count++;
        if (g_fs_log_count++ < FS_LOG_LIMIT)
            fprintf(stderr, "filesystem: open failed %s mode=%s (%s)\n",
                    guest_path, guest_mode, host_path);
        if (system_asset && strstr(guest_path, "FFLRes") != NULL) {
            fprintf(stderr,
                    "filesystem: Mii resource requires your Wii U system dump: "
                    "%s\n",
                    host_path);
        }
        return errno == ENOENT ? FS_STATUS_NOT_FOUND : FS_STATUS_ACCESS_ERROR;
    }

    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (g_files[i].stream)
            continue;
        g_files[i].stream = stream;
        snprintf(g_files[i].path, sizeof(g_files[i].path), "%s", host_path);
        mem_write32(cpu, guest_handle_address, FS_FILE_HANDLE_BASE + i);
        if (g_fs_log_count++ < FS_LOG_LIMIT)
            fprintf(stderr, "filesystem: opened %s -> handle=0x%X\n",
                    guest_path, FS_FILE_HANDLE_BASE + i);
        g_asset_open_count++;
        return FS_STATUS_OK;
    }

    fclose(stream);
    return FS_STATUS_STORAGE_FULL;
}

static s32 open_host_file(CPUState* cpu, u32 guest_path_address,
                          u32 guest_mode_address, u32 guest_handle_address) {
    char path[FS_MAX_PATH];
    if (!read_guest_string(cpu, guest_path_address, path, sizeof(path)))
        return FS_STATUS_NOT_FOUND;
    return open_host_file_path(cpu, path, guest_mode_address, guest_handle_address);
}

static s32 read_host_file(CPUState* cpu, u32 guest_buffer, u32 size,
                          u32 count, u32 handle, bool positioned, u32 position) {
    HostFile* file = find_file(handle);
    if (!file)
        return FS_STATUS_ACCESS_ERROR;
    if (size == 0 || count == 0)
        return 0;
    /* TitleBG.pack exceeds the old 64 MiB temporary-buffer limit. Validate
       the full guest destination and read directly into its backing region
       instead of rejecting valid title assets or allocating a second copy. */
    u64 total = (u64)size * count;
    if (total > 0xFFFFFFFFull || count > 0x7FFFFFFFu ||
        (u64)guest_buffer + total > 0x100000000ull)
        return FS_STATUS_ACCESS_ERROR;
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* segment =
        wiiu_memory_find(memory, guest_buffer, (u32)total);
    if (!segment || !segment->writable)
        return FS_STATUS_ACCESS_ERROR;
    u8* buffer = segment->data +
                 (wiiu_memory_canonical_address(guest_buffer) - segment->base);

    s64 old_position = -1;
    if (positioned) {
        old_position = (s64)host_ftell(file->stream);
        if (host_fseek(file->stream, position, SEEK_SET) != 0) {
            return FS_STATUS_ACCESS_ERROR;
        }
    }

    size_t items = fread(buffer, size, count, file->stream);
    size_t bytes = items * size;

    if (positioned && old_position >= 0)
        host_fseek(file->stream, old_position, SEEK_SET);
    g_asset_bytes_read += bytes;
    if (bytes != 0u)
        snprintf(g_last_read_path, sizeof(g_last_read_path), "%s",
                 file->path);

    if (g_fs_log_count++ < FS_LOG_LIMIT)
        fprintf(stderr,
                "filesystem: read handle=0x%X size=%u count=%u -> %zu items\n",
                handle, size, count, items);
    return (s32)items;
}

static s32 write_host_file(CPUState* cpu, u32 guest_buffer, u32 size,
                           u32 count, u32 handle, bool positioned,
                           u32 position) {
    HostFile* file = find_file(handle);
    if (!file)
        return FS_STATUS_ACCESS_ERROR;
    if (size == 0 || count == 0)
        return 0;
    if (count > FS_IO_LIMIT / size)
        return FS_STATUS_ACCESS_ERROR;

    size_t total = (size_t)size * count;
    u8* buffer = (u8*)malloc(total);
    if (!buffer)
        return FS_STATUS_STORAGE_FULL;
    for (size_t i = 0; i < total; i++)
        buffer[i] = mem_read8(cpu, guest_buffer + (u32)i);

    s64 old_position = -1;
    if (positioned) {
        old_position = (s64)host_ftell(file->stream);
        if (host_fseek(file->stream, position, SEEK_SET) != 0) {
            free(buffer);
            return FS_STATUS_ACCESS_ERROR;
        }
    }

    size_t items = fwrite(buffer, size, count, file->stream);
    if (positioned && old_position >= 0)
        host_fseek(file->stream, old_position, SEEK_SET);
    free(buffer);
    return (s32)items;
}

#ifdef _WIN32
static bool dir_next(HostDir* dir, char* name, size_t name_size,
                     char* full_path, size_t full_path_size) {
    for (;;) {
        if (!dir->entry_ready) {
            if (dir->search == -1 || _findnext64(dir->search, &dir->entry) != 0)
                return false;
        }
        dir->entry_ready = false;
        if (strcmp(dir->entry.name, ".") == 0 ||
            strcmp(dir->entry.name, "..") == 0) {
            continue;
        }
        snprintf(name, name_size, "%s", dir->entry.name);
        snprintf(full_path, full_path_size, "%s%c%s", dir->path,
                 HOST_PATH_SEPARATOR, dir->entry.name);
        return true;
    }
}
#else
static bool dir_next(HostDir* dir, char* name, size_t name_size,
                     char* full_path, size_t full_path_size) {
    struct dirent* entry;
    while ((entry = readdir(dir->stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(name, name_size, "%s", entry->d_name);
        snprintf(full_path, full_path_size, "%s/%s", dir->path,
                 entry->d_name);
        return true;
    }
    return false;
}
#endif

static s32 open_host_dir_path(CPUState* cpu, const char* guest_path,
                         u32 guest_handle_address) {
    char host_path[FS_MAX_PATH];
    if (!map_guest_path(guest_path, host_path, sizeof(host_path))) {
        return FS_STATUS_NOT_FOUND;
    }

    host_stat_t info;
    if (host_stat(host_path, &info) != 0) {
        if ((!try_base_fallback(guest_path, host_path, sizeof(host_path)) &&
             !try_localized_fallback(guest_path, host_path,
                                     sizeof(host_path))) ||
            host_stat(host_path, &info) != 0) {
            return FS_STATUS_NOT_FOUND;
        }
    }
#ifdef _WIN32
    if ((info.st_mode & _S_IFDIR) == 0)
#else
    if (!S_ISDIR(info.st_mode))
#endif
        return FS_STATUS_NOT_DIR;

    for (u32 i = 0; i < FS_MAX_DIRS; i++) {
        if (g_dirs[i].used)
            continue;
        HostDir* dir = &g_dirs[i];
        memset(dir, 0, sizeof(*dir));
        dir->used = true;
        snprintf(dir->path, sizeof(dir->path), "%s", host_path);
#ifdef _WIN32
        char pattern[FS_MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\*", host_path);
        dir->search = _findfirst64(pattern, &dir->entry);
        dir->entry_ready = dir->search != -1;
#else
        dir->stream = opendir(host_path);
        if (!dir->stream) {
            close_dir_slot(dir);
            return FS_STATUS_ACCESS_ERROR;
        }
#endif
        mem_write32(cpu, guest_handle_address, FS_DIR_HANDLE_BASE + i);
        if (g_fs_log_count++ < FS_LOG_LIMIT)
            fprintf(stderr, "filesystem: opened dir %s -> handle=0x%X\n",
                    guest_path, FS_DIR_HANDLE_BASE + i);
        return FS_STATUS_OK;
    }
    return FS_STATUS_STORAGE_FULL;
}

static s32 read_host_dir(CPUState* cpu, u32 handle, u32 guest_entry) {
    HostDir* dir = find_dir(handle);
    if (!dir)
        return FS_STATUS_ACCESS_ERROR;

    char name[256];
    char full_path[FS_MAX_PATH];
    if (!dir_next(dir, name, sizeof(name), full_path, sizeof(full_path)))
        return FS_STATUS_END;

    for (u32 i = 0; i < 0x164u; i++)
        mem_write8(cpu, guest_entry + i, 0);
    s32 result = stat_host_path(cpu, full_path, guest_entry);
    if (result != FS_STATUS_OK)
        return result;
    write_guest_string(cpu, guest_entry + 0x64u, 256u, name);
    return FS_STATUS_OK;
}

static bool write_save_path(CPUState* cpu, u32 source, u32 destination,
                            u32 capacity, const char* prefix) {
    char relative[FS_MAX_PATH];
    if (!read_guest_string(cpu, source, relative, sizeof(relative)))
        relative[0] = 0;
    while (relative[0] == '/' || relative[0] == '\\')
        memmove(relative, relative + 1, strlen(relative));
    if (path_has_parent_component(relative))
        return false;

    char output[FS_MAX_PATH];
    if (relative[0])
        snprintf(output, sizeof(output), "%s/%s", prefix, relative);
    else
        snprintf(output, sizeof(output), "%s", prefix);
    return write_guest_string(cpu, destination, capacity, output);
}

static bool handle_save_path_import(CPUState* cpu, const char* name) {
    if (name_is(name, "SAVEGetSharedDataTitlePath")) {
        char prefix[96];
        snprintf(prefix, sizeof(prefix),
                 "/vol/storage_mlc01/sys/title/%08X/%08X/content", cpu->gpr[3],
                 cpu->gpr[4]);
        cpu->gpr[3] = write_save_path(cpu, cpu->gpr[5], cpu->gpr[6],
                                             cpu->gpr[7], prefix)
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
        return_to_lr(cpu);
        return true;
    }
    if (name_is(name, "SAVEGetSharedSaveDataPath")) {
        cpu->gpr[3] = write_save_path(cpu, cpu->gpr[5], cpu->gpr[6],
                                             cpu->gpr[7], "/vol/save/common")
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
        return_to_lr(cpu);
        return true;
    }
    if (name_is(name, "SAVEGetNoDeleteSaveDataPath")) {
        cpu->gpr[3] = write_save_path(cpu, cpu->gpr[5], cpu->gpr[6],
                                             cpu->gpr[7], "/vol/save/no_delete")
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
        return_to_lr(cpu);
        return true;
    }
    if (name_is(name, "SAVEGetNoDeleteGroupSaveDirPath")) {
        cpu->gpr[3] = write_guest_string(cpu, cpu->gpr[3], cpu->gpr[4],
                                         "/vol/save/no_delete")
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
        return_to_lr(cpu);
        return true;
    }
    return false;
}

bool wiiu_filesystem_handle_import(CPUState* cpu, const char* name) {
    if (!cpu || !name)
        return false;

    if (name_is(name, "FSInitCmdBlock")) {
        for (u32 i = 0; i < 0xA80u; i++)
            mem_write8(cpu, cpu->gpr[3] + i, 0);
        cpu->gpr[3] = FS_STATUS_OK;
    } else if (name_is(name, "FSInit") || name_is(name, "FSShutdown") ||
               name_is(name, "FSAddClient") || name_is(name, "FSDelClient") ||
               name_is(name, "SAVEInit") || name_is(name, "SAVEShutdown") ||
               name_starts_with(name, "SAVEInit")) {
        cpu->gpr[3] = FS_STATUS_OK;
    } else if (name_is(name, "FSSetStateChangeNotification") ||
               name_is(name, "FSSetCmdPriority") ||
               name_is(name, "FSFlushQuota") ||
               name_is(name, "SAVEFlushQuota") ||
               name_is(name, "FSGetErrorCodeForViewer") ||
               name_is(name, "FSGetLastError") ||
               name_is(name, "FSGetLastErrorCodeForViewer")) {
        /* Host file operations are synchronous, so these status hooks are idle. */
        cpu->gpr[3] = FS_STATUS_OK;
    } else if (name_is(name, "FSGetVolumeState")) {
        cpu->gpr[3] = 1u; /* FS_VOLSTATE_READY */
    } else if (name_is(name, "FSGetCwd")) {
        cpu->gpr[3] = write_guest_string(cpu, cpu->gpr[5], cpu->gpr[6],
                                         g_current_dir)
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSChangeDir")) {
        char guest[FS_MAX_PATH];
        char host[FS_MAX_PATH];
        if (map_guest_address(cpu, cpu->gpr[5], guest, sizeof(guest), host,
                              sizeof(host)) &&
            (host_path_is_dir(host) ||
             (try_base_fallback(guest, host, sizeof(host)) &&
              host_path_is_dir(host)))) {
            snprintf(g_current_dir, sizeof(g_current_dir), "%s", guest);
            cpu->gpr[3] = FS_STATUS_OK;
        } else {
            cpu->gpr[3] = FS_STATUS_NOT_DIR;
        }
    } else if (name_is(name, "FSOpenFile")) {
        cpu->gpr[3] = (u32)open_host_file(cpu, cpu->gpr[5], cpu->gpr[6],
                                         cpu->gpr[7]);
    } else if (name_is(name, "SAVEOpenFile")) {
        char path[FS_MAX_PATH];
        cpu->gpr[3] = read_save_path(cpu, cpu->gpr[5], cpu->gpr[6],
                                    path, sizeof(path))
            ? (u32)open_host_file_path(cpu, path, cpu->gpr[7], cpu->gpr[8])
            : (u32)FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSCloseFile")) {
        HostFile* file = find_file(cpu->gpr[5]);
        cpu->gpr[3] = file ? FS_STATUS_OK : FS_STATUS_ACCESS_ERROR;
        if (file)
            close_file_slot(file);
    } else if (name_is(name, "FSReadFile")) {
        cpu->gpr[3] = (u32)read_host_file(cpu, cpu->gpr[5], cpu->gpr[6],
                                         cpu->gpr[7], cpu->gpr[8], false, 0);
    } else if (name_is(name, "FSReadFileWithPos")) {
        cpu->gpr[3] = (u32)read_host_file(cpu, cpu->gpr[5], cpu->gpr[6],
                                         cpu->gpr[7], cpu->gpr[9], true,
                                         cpu->gpr[8]);
    } else if (name_is(name, "FSWriteFile")) {
        cpu->gpr[3] = (u32)write_host_file(cpu, cpu->gpr[5], cpu->gpr[6],
                                          cpu->gpr[7], cpu->gpr[8], false, 0);
    } else if (name_is(name, "FSWriteFileWithPos")) {
        cpu->gpr[3] = (u32)write_host_file(cpu, cpu->gpr[5], cpu->gpr[6],
                                          cpu->gpr[7], cpu->gpr[9], true,
                                          cpu->gpr[8]);
    } else if (name_is(name, "FSGetStat") || name_is(name, "SAVEGetStat")) {
        char guest[FS_MAX_PATH] = "<invalid>";
        char host[FS_MAX_PATH];
        s32 status = FS_STATUS_NOT_FOUND;
        bool save = name_is(name, "SAVEGetStat");
        u32 output = cpu->gpr[save ? 7 : 6];
        bool mapped = save
            ? (read_save_path(cpu, cpu->gpr[5], cpu->gpr[6], guest, sizeof(guest)) &&
               map_guest_path(guest, host, sizeof(host)))
            : map_guest_address(cpu, cpu->gpr[5], guest, sizeof(guest), host, sizeof(host));
        if (mapped) {
            status = stat_host_path(cpu, host, output);
            if (status == FS_STATUS_NOT_FOUND &&
                try_base_fallback(guest, host, sizeof(host))) {
                status = stat_host_path(cpu, host, output);
            }
            if (status == FS_STATUS_NOT_FOUND &&
                try_localized_fallback(guest, host, sizeof(host))) {
                status = stat_host_path(cpu, host, output);
            }
        }
        cpu->gpr[3] = (u32)status;
        if (g_fs_log_count++ < FS_LOG_LIMIT)
            fprintf(stderr, "filesystem: stat %s -> %d\n", guest,
                    (s32)cpu->gpr[3]);
    } else if (name_is(name, "FSGetStatFile")) {
        HostFile* file = find_file(cpu->gpr[5]);
        cpu->gpr[3] = file ? (u32)stat_host_path(cpu, file->path, cpu->gpr[6])
                           : (u32)FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "SAVEGetStatOtherApplication")) {
        /* A missing save from another title is a normal launch condition. */
        char path[FS_MAX_PATH];
        const char* printed_path = "<invalid>";
        if (read_guest_string(cpu, cpu->gpr[8], path, sizeof(path)))
            printed_path = path;
        if (cpu->gpr[9] != 0u) {
            for (u32 i = 0; i < 0x64u; i++)
                mem_write8(cpu, cpu->gpr[9] + i, 0);
        }
        cpu->gpr[3] = (u32)FS_STATUS_NOT_FOUND;
        if (g_fs_log_count++ < FS_LOG_LIMIT) {
            fprintf(stderr,
                    "filesystem: other save stat title=%08X%08X slot=%u "
                    "path=%s -> no save\n",
                    cpu->gpr[5], cpu->gpr[6], cpu->gpr[7] & 0xFFu,
                    printed_path);
        }
    } else if (name_is(name, "FSGetPosFile")) {
        HostFile* file = find_file(cpu->gpr[5]);
        s64 position = file ? (s64)host_ftell(file->stream) : -1;
        if (position >= 0 && cpu->gpr[6])
            mem_write32(cpu, cpu->gpr[6], (u32)position);
        cpu->gpr[3] = position >= 0 ? FS_STATUS_OK : FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSSetPosFile")) {
        HostFile* file = find_file(cpu->gpr[5]);
        cpu->gpr[3] = file && host_fseek(file->stream, cpu->gpr[6], SEEK_SET) == 0
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSFlushFile")) {
        HostFile* file = find_file(cpu->gpr[5]);
        cpu->gpr[3] = file && fflush(file->stream) == 0
                          ? FS_STATUS_OK
                          : FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSOpenDir")) {
        char path[FS_MAX_PATH];
        cpu->gpr[3] = read_guest_string(cpu, cpu->gpr[5], path, sizeof(path))
            ? (u32)open_host_dir_path(cpu, path, cpu->gpr[6])
            : (u32)FS_STATUS_NOT_FOUND;
    } else if (name_is(name, "SAVEOpenDir")) {
        char path[FS_MAX_PATH];
        cpu->gpr[3] = read_save_path(cpu, cpu->gpr[5], cpu->gpr[6], path, sizeof(path))
            ? (u32)open_host_dir_path(cpu, path, cpu->gpr[7])
            : (u32)FS_STATUS_ACCESS_ERROR;
    } else if (name_is(name, "FSReadDir")) {
        cpu->gpr[3] = (u32)read_host_dir(cpu, cpu->gpr[5], cpu->gpr[6]);
    } else if (name_is(name, "FSCloseDir")) {
        HostDir* dir = find_dir(cpu->gpr[5]);
        cpu->gpr[3] = dir ? FS_STATUS_OK : FS_STATUS_ACCESS_ERROR;
        if (dir)
            close_dir_slot(dir);
    } else if (name_is(name, "FSMakeDir")) {
        char guest[FS_MAX_PATH];
        char host[FS_MAX_PATH];
        if (map_guest_address(cpu, cpu->gpr[5], guest, sizeof(guest), host,
                              sizeof(host))) {
            ensure_parent_dirs(host);
            cpu->gpr[3] = host_make_dir(host) == 0 || errno == EEXIST
                              ? FS_STATUS_OK
                              : FS_STATUS_ACCESS_ERROR;
        } else {
            cpu->gpr[3] = FS_STATUS_ACCESS_ERROR;
        }
    } else if (handle_save_path_import(cpu, name)) {
        return true;
    } else if (name_starts_with(name, "FS") ||
               name_starts_with(name, "SAVE")) {
        u32 arg3 = cpu->gpr[3];
        cpu->gpr[3] = FS_STATUS_UNSUPPORTED;
        fprintf(stderr,
                "filesystem: unsupported import %s pc=0x%08X lr=0x%08X "
                "args=%08X,%08X,%08X,%08X\n",
                name, cpu->pc, cpu->lr, arg3, cpu->gpr[4],
                cpu->gpr[5], cpu->gpr[6]);
    } else {
        return false;
    }

    return_to_lr(cpu);
    return true;
}
