#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define PATH_SEPARATOR '\\'
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '/'
#endif

typedef struct {
    uint64_t files;
    uint64_t archives;
    uint64_t archive_entries;
    uint64_t stored_bytes;
    uint64_t unpacked_bytes;
    uint64_t failures;
} AssetStats;

static uint16_t read_u16(const uint8_t* data, int little_endian) {
    if (little_endian)
        return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
    return (uint16_t)((uint16_t)data[0] << 8) | data[1];
}

static uint32_t read_u32(const uint8_t* data, int little_endian) {
    if (little_endian) {
        return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
               ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static int has_extension(const char* path, const char* extension) {
    const char* dot = strrchr(path, '.');
    return dot && strcasecmp(dot, extension) == 0;
}

static int read_file(const char* path, uint8_t** data_out, size_t* size_out) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    size_t size = (size_t)length;
    uint8_t* data = (uint8_t*)malloc(size ? size : 1u);
    if (!data) {
        fclose(file);
        return 0;
    }
    if (size && fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *data_out = data;
    *size_out = size;
    return 1;
}

static int decode_yaz0(const uint8_t* source, size_t source_size,
                       uint8_t** output_out, size_t* output_size_out) {
    if (source_size < 16u || memcmp(source, "Yaz0", 4) != 0)
        return 0;

    uint32_t decoded_size = read_u32(source + 4, 0);
    if (decoded_size == 0 || decoded_size > 0x40000000u)
        return 0;
    uint8_t* output = (uint8_t*)malloc(decoded_size);
    if (!output)
        return 0;

    size_t source_pos = 16u;
    size_t output_pos = 0;
    uint8_t code = 0;
    unsigned bits_left = 0;
    while (output_pos < decoded_size) {
        if (bits_left == 0) {
            if (source_pos >= source_size)
                goto invalid;
            code = source[source_pos++];
            bits_left = 8;
        }

        if (code & 0x80u) {
            if (source_pos >= source_size)
                goto invalid;
            output[output_pos++] = source[source_pos++];
        } else {
            if (source_pos + 1u >= source_size)
                goto invalid;
            uint8_t first = source[source_pos++];
            uint8_t second = source[source_pos++];
            size_t distance = ((size_t)(first & 0x0Fu) << 8) | second;
            size_t length = first >> 4;
            if (length == 0) {
                if (source_pos >= source_size)
                    goto invalid;
                length = (size_t)source[source_pos++] + 0x12u;
            } else {
                length += 2u;
            }
            if (distance + 1u > output_pos || length > decoded_size - output_pos)
                goto invalid;
            size_t copy_pos = output_pos - distance - 1u;
            while (length--)
                output[output_pos++] = output[copy_pos++];
        }
        code <<= 1;
        bits_left--;
    }

    *output_out = output;
    *output_size_out = decoded_size;
    return 1;

invalid:
    free(output);
    return 0;
}

static int validate_sarc(const uint8_t* data, size_t size,
                         uint64_t* entries_out) {
    if (size < 0x20u || memcmp(data, "SARC", 4) != 0)
        return 0;
    int little_endian;
    if (data[6] == 0xFEu && data[7] == 0xFFu)
        little_endian = 0;
    else if (data[6] == 0xFFu && data[7] == 0xFEu)
        little_endian = 1;
    else
        return 0;

    uint16_t header_size = read_u16(data + 4, little_endian);
    uint32_t declared_size = read_u32(data + 8, little_endian);
    uint32_t data_offset = read_u32(data + 0x0C, little_endian);
    if (header_size < 0x14u || header_size + 0x0Cu > size ||
        declared_size > size || data_offset > declared_size ||
        memcmp(data + header_size, "SFAT", 4) != 0) {
        return 0;
    }

    const uint8_t* sfat = data + header_size;
    uint16_t sfat_size = read_u16(sfat + 4, little_endian);
    uint16_t node_count = read_u16(sfat + 6, little_endian);
    if (sfat_size < 0x0Cu)
        return 0;
    size_t nodes_offset = (size_t)header_size + sfat_size;
    size_t nodes_size = (size_t)node_count * 0x10u;
    if (nodes_offset + nodes_size + 8u > size)
        return 0;

    size_t sfnt_offset = nodes_offset + nodes_size;
    const uint8_t* sfnt = data + sfnt_offset;
    if (memcmp(sfnt, "SFNT", 4) != 0)
        return 0;
    uint16_t sfnt_size = read_u16(sfnt + 4, little_endian);
    if (sfnt_size < 8u || sfnt_offset + sfnt_size > data_offset)
        return 0;
    size_t names_offset = sfnt_offset + sfnt_size;

    for (uint32_t i = 0; i < node_count; i++) {
        const uint8_t* node = data + nodes_offset + (size_t)i * 0x10u;
        uint32_t name_field = read_u32(node + 4, little_endian);
        uint32_t file_start = read_u32(node + 8, little_endian);
        uint32_t file_end = read_u32(node + 12, little_endian);
        if (file_start > file_end || file_end > declared_size - data_offset)
            return 0;

        if ((name_field >> 24) != 0) {
            size_t name_offset = names_offset +
                                 (size_t)(name_field & 0x00FFFFFFu) * 4u;
            if (name_offset >= data_offset ||
                !memchr(data + name_offset, 0, data_offset - name_offset)) {
                return 0;
            }
        }
    }

    *entries_out = node_count;
    return 1;
}

static int list_sarc(const uint8_t* data, size_t size) {
    if (size < 0x20u || memcmp(data, "SARC", 4) != 0)
        return 0;
    int little_endian;
    if (data[6] == 0xFEu && data[7] == 0xFFu)
        little_endian = 0;
    else if (data[6] == 0xFFu && data[7] == 0xFEu)
        little_endian = 1;
    else
        return 0;

    uint16_t header_size = read_u16(data + 4, little_endian);
    uint32_t declared_size = read_u32(data + 8, little_endian);
    uint32_t data_offset = read_u32(data + 0x0C, little_endian);
    if (header_size < 0x14u || declared_size > size ||
        data_offset > declared_size ||
        (size_t)header_size + 0x0Cu > size ||
        memcmp(data + header_size, "SFAT", 4) != 0) {
        return 0;
    }

    const uint8_t* sfat = data + header_size;
    uint16_t sfat_size = read_u16(sfat + 4, little_endian);
    uint16_t node_count = read_u16(sfat + 6, little_endian);
    size_t nodes_offset = (size_t)header_size + sfat_size;
    size_t nodes_size = (size_t)node_count * 0x10u;
    size_t sfnt_offset = nodes_offset + nodes_size;
    if (sfat_size < 0x0Cu || sfnt_offset + 8u > size ||
        memcmp(data + sfnt_offset, "SFNT", 4) != 0) {
        return 0;
    }
    uint16_t sfnt_size = read_u16(data + sfnt_offset + 4, little_endian);
    size_t names_offset = sfnt_offset + sfnt_size;
    if (sfnt_size < 8u || names_offset > data_offset)
        return 0;

    printf("SARC entries=%u unpacked=%u data_offset=0x%X\n", node_count,
           declared_size, data_offset);
    for (uint32_t i = 0; i < node_count; i++) {
        const uint8_t* node = data + nodes_offset + (size_t)i * 0x10u;
        uint32_t hash = read_u32(node, little_endian);
        uint32_t name_field = read_u32(node + 4, little_endian);
        uint32_t file_start = read_u32(node + 8, little_endian);
        uint32_t file_end = read_u32(node + 12, little_endian);
        const char* name = "<unnamed>";
        if ((name_field >> 24) != 0) {
            size_t name_offset = names_offset +
                                 (size_t)(name_field & 0x00FFFFFFu) * 4u;
            if (name_offset >= data_offset ||
                !memchr(data + name_offset, 0, data_offset - name_offset)) {
                return 0;
            }
            name = (const char*)(data + name_offset);
        }
        printf("%4u  %08X-%08X  %10u  %08X  %s\n", i,
               data_offset + file_start, data_offset + file_end,
               file_end - file_start, hash, name);
    }
    return 1;
}

static int list_archive(const char* path) {
    uint8_t* stored = NULL;
    uint8_t* decoded = NULL;
    size_t stored_size = 0;
    size_t decoded_size = 0;
    if (!read_file(path, &stored, &stored_size)) {
        fprintf(stderr, "asset error: cannot read: %s\n", path);
        return 1;
    }

    const uint8_t* archive = stored;
    size_t archive_size = stored_size;
    if (stored_size >= 4u && memcmp(stored, "Yaz0", 4) == 0) {
        if (!decode_yaz0(stored, stored_size, &decoded, &decoded_size)) {
            fprintf(stderr, "asset error: invalid Yaz0 stream: %s\n", path);
            free(stored);
            return 1;
        }
        archive = decoded;
        archive_size = decoded_size;
    }

    int result = 0;
    if (!list_sarc(archive, archive_size)) {
        fprintf(stderr, "asset error: invalid SARC archive: %s\n", path);
        result = 1;
    }
    free(decoded);
    free(stored);
    return result;
}

static int list_archive_offset(const char* path, const char* offset_text) {
    char* end = NULL;
    errno = 0;
    unsigned long parsed_offset = strtoul(offset_text, &end, 0);
    if (errno != 0 || end == offset_text || *end != '\0') {
        fprintf(stderr, "asset error: invalid archive offset: %s\n",
                offset_text);
        return 1;
    }

    uint8_t* stored = NULL;
    uint8_t* decoded = NULL;
    size_t stored_size = 0;
    size_t decoded_size = 0;
    if (!read_file(path, &stored, &stored_size)) {
        fprintf(stderr, "asset error: cannot read: %s\n", path);
        return 1;
    }

    const uint8_t* archive = stored;
    size_t archive_size = stored_size;
    if (stored_size >= 4u && memcmp(stored, "Yaz0", 4) == 0) {
        if (!decode_yaz0(stored, stored_size, &decoded, &decoded_size)) {
            fprintf(stderr, "asset error: invalid Yaz0 stream: %s\n", path);
            free(stored);
            return 1;
        }
        archive = decoded;
        archive_size = decoded_size;
    }

    size_t offset = (size_t)parsed_offset;
    int result = 0;
    if (offset >= archive_size ||
        !list_sarc(archive + offset, archive_size - offset)) {
        fprintf(stderr,
                "asset error: invalid embedded SARC at 0x%lX: %s\n",
                parsed_offset, path);
        result = 1;
    }
    free(decoded);
    free(stored);
    return result;
}

static int hexdump_archive_offset(const char* path, const char* offset_text) {
    char* end = NULL;
    errno = 0;
    unsigned long parsed_offset = strtoul(offset_text, &end, 0);
    if (errno != 0 || end == offset_text || *end != '\0') {
        fprintf(stderr, "asset error: invalid archive offset: %s\n",
                offset_text);
        return 1;
    }

    uint8_t* stored = NULL;
    uint8_t* decoded = NULL;
    size_t stored_size = 0;
    size_t decoded_size = 0;
    if (!read_file(path, &stored, &stored_size)) {
        fprintf(stderr, "asset error: cannot read: %s\n", path);
        return 1;
    }

    const uint8_t* archive = stored;
    size_t archive_size = stored_size;
    if (stored_size >= 4u && memcmp(stored, "Yaz0", 4) == 0) {
        if (!decode_yaz0(stored, stored_size, &decoded, &decoded_size)) {
            fprintf(stderr, "asset error: invalid Yaz0 stream: %s\n", path);
            free(stored);
            return 1;
        }
        archive = decoded;
        archive_size = decoded_size;
    }

    size_t offset = (size_t)parsed_offset;
    if (offset >= archive_size) {
        fprintf(stderr, "asset error: offset 0x%lX is outside: %s\n",
                parsed_offset, path);
        free(decoded);
        free(stored);
        return 1;
    }

    size_t count = archive_size - offset;
    if (count > 64u)
        count = 64u;
    printf("decoded archive offset 0x%08lX (%zu bytes shown)\n",
           parsed_offset, count);
    for (size_t row = 0; row < count; row += 16u) {
        size_t row_count = count - row;
        if (row_count > 16u)
            row_count = 16u;
        printf("%08lX  ", parsed_offset + (unsigned long)row);
        for (size_t column = 0; column < 16u; ++column) {
            if (column < row_count)
                printf("%02X ", archive[offset + row + column]);
            else
                printf("   ");
        }
        printf(" ");
        for (size_t column = 0; column < row_count; ++column) {
            uint8_t value = archive[offset + row + column];
            putchar(value >= 32u && value <= 126u ? (int)value : '.');
        }
        putchar('\n');
    }

    free(decoded);
    free(stored);
    return 0;
}

static void record_failure(AssetStats* stats, const char* path,
                           const char* reason) {
    fprintf(stderr, "asset error: %s: %s\n", reason, path);
    stats->failures++;
}

static void verify_file(const char* path, AssetStats* stats) {
    uint8_t* stored = NULL;
    size_t stored_size = 0;
    stats->files++;
    if (!read_file(path, &stored, &stored_size)) {
        record_failure(stats, path, "cannot read");
        return;
    }
    stats->stored_bytes += stored_size;
    if (stored_size == 0) {
        record_failure(stats, path, "empty file");
        free(stored);
        return;
    }

    if (has_extension(path, ".szs")) {
        uint8_t* decoded = NULL;
        size_t decoded_size = 0;
        const uint8_t* archive = stored;
        size_t archive_size = stored_size;
        if (stored_size >= 4u && memcmp(stored, "Yaz0", 4) == 0) {
            if (!decode_yaz0(stored, stored_size, &decoded, &decoded_size)) {
                record_failure(stats, path, "invalid Yaz0 stream");
                free(stored);
                return;
            }
            archive = decoded;
            archive_size = decoded_size;
        }
        uint64_t entries = 0;
        if (!validate_sarc(archive, archive_size, &entries))
            record_failure(stats, path, "invalid SARC archive");
        else {
            stats->archives++;
            stats->archive_entries += entries;
            stats->unpacked_bytes += archive_size;
        }
        free(decoded);
    } else if (has_extension(path, ".sarc")) {
        uint64_t entries = 0;
        if (!validate_sarc(stored, stored_size, &entries))
            record_failure(stats, path, "invalid SARC archive");
        else {
            stats->archives++;
            stats->archive_entries += entries;
            stats->unpacked_bytes += stored_size;
        }
    } else if (has_extension(path, ".bfstm") &&
               (stored_size < 4u || memcmp(stored, "FSTM", 4) != 0)) {
        record_failure(stats, path, "invalid BFSTM header");
    } else if (has_extension(path, ".bfsar") &&
               (stored_size < 4u || memcmp(stored, "FSAR", 4) != 0)) {
        record_failure(stats, path, "invalid BFSAR header");
    } else if (has_extension(path, ".msbt") &&
               (stored_size < 8u || memcmp(stored, "MsgStdBn", 8) != 0)) {
        record_failure(stats, path, "invalid MSBT header");
    }
    free(stored);

    if (stats->archives && (stats->archives % 250u) == 0u)
        printf("verified %llu archives...\n",
               (unsigned long long)stats->archives);
}

#ifdef _WIN32
static void verify_tree(const char* root, AssetStats* stats) {
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    struct __finddata64_t entry;
    intptr_t search = _findfirst64(pattern, &entry);
    if (search == -1) {
        record_failure(stats, root, "cannot enumerate directory");
        return;
    }
    do {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s\\%s", root, entry.name);
        if (entry.attrib & _A_SUBDIR)
            verify_tree(path, stats);
        else
            verify_file(path, stats);
    } while (_findnext64(search, &entry) == 0);
    _findclose(search);
}
#else
static void verify_tree(const char* root, AssetStats* stats) {
    DIR* directory = opendir(root);
    if (!directory) {
        record_failure(stats, root, "cannot enumerate directory");
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0) {
            record_failure(stats, path, "cannot stat");
        } else if (S_ISDIR(info.st_mode)) {
            verify_tree(path, stats);
        } else if (S_ISREG(info.st_mode)) {
            verify_file(path, stats);
        }
    }
    closedir(directory);
}
#endif

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--list") == 0)
        return list_archive(argv[2]);
    if (argc == 4 && strcmp(argv[1], "--list-offset") == 0)
        return list_archive_offset(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "--hexdump-offset") == 0)
        return hexdump_archive_offset(argv[2], argv[3]);

    const char* root = argc > 1 ? argv[1] : "../SuperMario3dworld/content";
    AssetStats stats;
    memset(&stats, 0, sizeof(stats));
    printf("verifying Wii U assets in %s\n", root);
    verify_tree(root, &stats);
    printf("asset verification: files=%llu archives=%llu entries=%llu "
           "stored=%llu unpacked=%llu failures=%llu\n",
           (unsigned long long)stats.files,
           (unsigned long long)stats.archives,
           (unsigned long long)stats.archive_entries,
           (unsigned long long)stats.stored_bytes,
           (unsigned long long)stats.unpacked_bytes,
           (unsigned long long)stats.failures);
    return stats.failures == 0 ? 0 : 1;
}
