/* fatlabel.h - v1 - single-file library to do stuff with FAT filesystem labels.
 * By: Patrick Gaskin
 *
 * Note: Requires C99 to compile. Only supports little-endian (due to the lack
 * of big-endian handling for the ints read from the FAT). Also uses _GNU_SOURCE
 * for asprintf. fatlabel_search only works on Linux.
 */

#ifndef FATLABEL_H
#define FATLABEL_H
#define _GNU_SOURCE // asprintf
#include <stdint.h>

#define FAT12_MAX 0xff4
#define FAT16_MAX 0xfff4
#define FAT32_MAX 0x0ffffff6

#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIR       0x10
#define FAT_ATTR_LONG_NAME 0x0f
#define FAT_ATTR_MASK      0x3f
#define FAT_ENTRY_FREE     0xe5

struct vfat_super_block {
    uint8_t  boot_jump[3];
    uint8_t  sysid[8];
    uint16_t sector_size_bytes;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sct;
    uint8_t  fats;
    uint16_t dir_entries;
    uint16_t sectors;
    uint8_t  media;
    uint16_t fat_length;
    uint16_t secs_track;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total_sect;
    union {
        struct fat_super_block {
            uint8_t unknown[3];
            uint8_t serno[4];
            uint8_t label[11];
            uint8_t magic[8];
            uint8_t dummy2[192];
            uint8_t pmagic[2];
        } __attribute__((packed)) fat;
        struct fat32_super_block {
            uint32_t fat32_length;
            uint16_t flags;
            uint8_t  version[2];
            uint32_t root_cluster;
            uint16_t insfo_sector;
            uint16_t backup_boot;
            uint16_t reserved2[6];
            uint8_t  unknown[3];
            uint8_t  serno[4];
            uint8_t  label[11];
            uint8_t  magic[8];
            uint8_t  dummy2[164];
            uint8_t  pmagic[2];
        } __attribute__((packed)) fat32;
    } __attribute__((packed)) type;
} __attribute__((packed));

struct vfat_dir_entry {
    uint8_t  name[11];
    uint8_t  attr;
    uint16_t time_creat;
    uint16_t date_creat;
    uint16_t time_acc;
    uint16_t date_acc;
    uint16_t cluster_high;
    uint16_t time_write;
    uint16_t date_write;
    uint16_t cluster_low;
    uint32_t size;
} __attribute__((packed));

/* fatlabel_get gets the label of a FAT filesystem in fd. It will return an error
 * if fd is not readable or is not a FAT filesystem. All output pointers are set
 * to null at the start, and need to be freed afterwards if they are not null
 * afterwards. If not found, boot_label or volume_label may be null. If the return
 * value is nonzero, err will be set to a more descriptive error message.
 */
int fatlabel_get(const int fd, char **boot_label, char **volume_label, char **err);

/* fatlabel_search searches for a device in /proc/partitions which has a specified
 * label (not case-sensitive). The returned path must be freed.
 */
char* fatlabel_search(const char *label);
#endif

#ifdef FATLABEL_IMPLEMENTATION
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

static uint8_t* vfat_dir_entry_get_volume_label(struct vfat_dir_entry *dir, int count) {
    for (; --count >= 0; dir++) {
        if (dir->name[0] == 0x00)
            break;
        if (dir->name[0] == FAT_ENTRY_FREE)
            continue;
        if ((dir->attr & FAT_ATTR_MASK) == FAT_ATTR_LONG_NAME)
            continue;
        if ((dir->attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIR)) != FAT_ATTR_VOLUME_ID)
            continue;
        if (dir->cluster_high != 0 || dir->cluster_low != 0)
            continue;
        return dir->name;
    }
    return NULL;
}

/* fatlabel_clean cleans an 11-character FAT label by removing trailing spaces
 * and converting it into a heap-allocated null-terminated string. The string
 * must be freed by the caller.
 */
static char* fatlabel_clean(const uint8_t lbl[11]) {
    char *nlbl;
    asprintf(&nlbl, "%.11s", lbl);
    for (int i = 11; i > 1; i--) {
        if ((nlbl[i] == ' ' || nlbl[i] == 0) && nlbl[i-1] != ' ') {
            nlbl[i] = 0;
            break;
        }
    }
    return nlbl;
}

int fatlabel_get(const int fd, char **boot_label, char **volume_label, char **err) {
    #define reterrs(...) {asprintf(err, __VA_ARGS__); free(sb); return -1;}
    *boot_label = NULL;
    *volume_label = NULL;
    *err = NULL;

    struct vfat_super_block *sb = (struct vfat_super_block*) malloc(sizeof(struct vfat_super_block));
    if (pread(fd, sb, sizeof(*sb), 0) != sizeof(*sb))
        reterrs("error reading fat superblock: %s", strerror(errno));

    if (sb->media != 0xf8 && sb->media != 0xf0)
        reterrs("unknown media type (probably not a FAT filesystem): %X", sb->media);
    if (sb->fats < 1 || sb->fats > 16)
        reterrs("unreasonable number of fats (probably not a FAT filesystem): %d", sb->fats)
    if (sb->sector_size_bytes == 0)
        reterrs("zero sector size (probably not a FAT filesystem)");

    uint16_t sct_bytes = sb->sector_size_bytes;
    uint16_t reserved_sct = sb->reserved_sct;
    uint32_t total_sct = sb->sectors ? sb->sectors : sb->total_sect;
    uint32_t fats_sct = (sb->fat_length ? sb->fat_length : sb->type.fat32.fat32_length) * sb->fats;
    uint16_t dirents = sb->dir_entries;
    uint16_t dirent_sct = (dirents*sizeof(struct vfat_dir_entry) + (sct_bytes-1)) / sct_bytes;
    uint32_t clusters = (total_sct - (reserved_sct + fats_sct + dirent_sct)) / sb->sectors_per_cluster;

    if (clusters >= FAT16_MAX) {
        *boot_label = fatlabel_clean(sb->type.fat32.label);

        uint32_t start_data_sct = reserved_sct + fats_sct;
        uint32_t cluster_size = sb->sectors_per_cluster * sct_bytes;
        uint32_t root_cluster = sb->type.fat32.root_cluster;
        int entries_per_cluster = cluster_size / sizeof(struct vfat_dir_entry);

        int maxloop = 100;
        uint32_t next_cluster = root_cluster;
        while (--maxloop) {
            uint64_t next_off_sct = (uint64_t)(next_cluster - 2) * sb->sectors_per_cluster;
            uint64_t next_off = (start_data_sct + next_off_sct) * sct_bytes;

            struct vfat_dir_entry* ents = (struct vfat_dir_entry*) malloc(cluster_size);
            if (pread(fd, ents, sizeof(*ents), next_off) != sizeof(*ents))
                reterrs("error reading root dirents: %s", strerror(errno));

            uint8_t *lbl = vfat_dir_entry_get_volume_label(ents, entries_per_cluster);
            if (lbl) {
                *volume_label = fatlabel_clean(lbl);
                break;
            }

            uint64_t fat_entry_off = (reserved_sct * sct_bytes) + (next_cluster * sizeof(uint32_t));
            if (pread(fd, &next_cluster, sizeof(next_cluster), fat_entry_off) != sizeof(next_cluster))
                reterrs("error reading next dirent cluster chain offset: %s", strerror(errno));

            next_cluster &= 0x0fffffff;

            if (next_cluster < 2 || next_cluster > FAT32_MAX)
                break;

            free(ents);
        }
    } else {
        *boot_label = fatlabel_clean(sb->type.fat.label);

        uint64_t root_off = (reserved_sct + fats_sct) * sct_bytes;
        struct vfat_dir_entry *ents = (struct vfat_dir_entry*) malloc(dirents * sizeof(struct vfat_dir_entry));
        if (pread(fd, ents, sizeof(*ents), root_off) != sizeof(*ents))
            reterrs("error reading root dirents: %s", strerror(errno));

        uint8_t *lbl = vfat_dir_entry_get_volume_label(ents, dirents);
        if (lbl)
            *volume_label = fatlabel_clean(lbl);

        free(ents);
    }

    free(sb);
    return 0;
    #undef reterrs
}

char* fatlabel_search(const char *label) {
    FILE *f = fopen("/proc/partitions", "r");

    int fd, matches;
    char buf[1024], dev[1024];
    char *path, *boot_label, *volume_label, *err;
    while (fgets(buf, 1024, f)) {
        if (sscanf(buf, " %*d %*d %*d %[^\n ]", dev) != 1)
            continue;

        asprintf(&path, "/dev/%s", dev);
        if ((fd = open(path, O_RDONLY)) < 0)
            continue;

        // we don't need to handle the error, as the labels will be null if
        // they don't exist or there is an error (and we don't care for the
        // error message)
        fatlabel_get(fd, &boot_label, &volume_label, &err);

        // printf("dev=%s boot=%s vol=%s err=%s\n", path, boot_label, volume_label, err);
        matches =
            (boot_label && strcasecmp(label, boot_label) == 0) ||
            (volume_label && strcasecmp(label, volume_label) == 0);

        free(volume_label);
        free(boot_label);
        free(err);
        close(fd);

        if (matches) {
            fclose(f);
            return path;
        }

        free(path);
    }

    fclose(f);
    return NULL;
}
#endif
