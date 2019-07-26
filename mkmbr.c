// mkmbr - v1 - generate a disk image out of partition images
// gcc -Wall -std=c99 -pedantic-errors -o mkmbr mkmbr.c
// Copyright 2019 Patrick Gaskin
// License: MIT License

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>

#define SECTOR_SIZE         512
#define HEADS_PER_CYLINDER  16
#define SECTORS_PER_HEAD    63

typedef struct chs_t {
    uint8_t head;      // head
    uint8_t sector;    // 2 high bits of cylinder + 6-bit sector.
    uint8_t cylinder;  // 8 low bits of cylinder
} __attribute__((packed)) chs_t;

int chs(uint32_t lba, chs_t* out) {
    uint32_t s = lba % SECTORS_PER_HEAD;   lba /= SECTORS_PER_HEAD;
    uint32_t h = lba % HEADS_PER_CYLINDER; lba /= HEADS_PER_CYLINDER;
    uint32_t c = lba;

    bool overflow = c > 0x3FF || s > 0x3F || h > 0xFF;
    out->head     = overflow ? 0xFF : h;
    out->sector   = overflow ? 0xFF : ((s+1) | ((c>>8)<<6));
    out->cylinder = overflow ? 0xFF : c;
    return overflow ? -EINVAL : 0;
}

int mkmbr(
    uint8_t  bootstrap[446],
    size_t   bootstrap_sz,

    size_t   partition_active,
    char*    partition_files[4],
    uint8_t  partition_types[4],
    size_t   partitions_sz,

    bool     verbose,
    char**   out_err,
    FILE*    out
) {
    #define reterr(rc, ...) { int ret = rc; if (out_err) asprintf(out_err, __VA_ARGS__); return ret ? ret : 1; }
    if (bootstrap_sz > 446 || (bootstrap_sz > 0 && !bootstrap))
        reterr(-EINVAL, "bootstrap must be at most 446 bytes");
    if (partitions_sz > 4 || !partition_files || !partition_types)
        reterr(-EINVAL, "at most 4 partitions must be defined");
    if (partition_active < 1 || partition_active > partitions_sz)
        reterr(-EINVAL, "active partition must exist and be from 1-%lu", partitions_sz);
    if (!out)
        reterr(-EINVAL, "out must point to a FILE");

    // Arrange partitions
    uint64_t partition_pad[4]        = {UINT64_MAX};
    uint32_t partition_lbas[4]       = {UINT32_MAX};
    uint32_t partition_lba_counts[4] = {UINT32_MAX};

    struct stat sb;
    uint32_t cur_sector = 1;
    for (size_t i = 0; i < partitions_sz; i++) {
        if (stat(partition_files[i], &sb))
            reterr(errno, "error stat-ing %s: %s\n", partition_files[i], strerror(errno));
        partition_pad[i]  = (sb.st_size % SECTOR_SIZE) ? SECTOR_SIZE - (sb.st_size % SECTOR_SIZE) : 0;
        partition_lbas[i] = cur_sector;
        partition_lba_counts[i] = (sb.st_size / SECTOR_SIZE) + (partition_pad[i] != 0 ? 1 : 0);
        cur_sector += partition_lba_counts[i];
    }

    // Write MBR
    #define ffn_(fn, exp, s, x) if ((fn) != (exp)) reterr(ferror(s), "error writing %s: %s", x, strerror(ferror(s)));
    #define fwrite_(ptr, size, n, s, x) ffn_(fwrite(ptr, size, n, s), size*n, s, x)
    #define fputc_(c, s, x) {assert(sizeof(c) == 1); ffn_(fputc(c, s), c, s, x);}
    long bs = ftell(out);

    if (verbose)
        printf("mbr (bootstrap_len=%lu) (partitions=%lu) (sectors=%d)\n", bootstrap_sz, partitions_sz, cur_sector);

    if (bootstrap_sz > 0)
        fwrite_(bootstrap, sizeof(*bootstrap), bootstrap_sz, out, "bootstrap");
    for (size_t pad = 446-bootstrap_sz; pad > 0; pad--)
        fputc_((uint8_t) 0x00, out, "bootstrap padding");
    assert(ftell(out)-bs == 446);

    for (size_t i = 0; i < 4; i++) {
        if (i > partitions_sz-1) {
            for (size_t pad = 16; pad > 0; pad--)
                fputc_((uint8_t) 0x00, out, "empty partition");
            continue;
        }

        assert(partition_pad[i] != UINT64_MAX);
        assert(partition_lbas[i] != UINT32_MAX);
        assert(partition_lba_counts[i] != UINT32_MAX);

        if (verbose)
            printf("partition %lu @ %u+%u (pad=%lu) (active=%s) (type=0x%02X): %s\n", i, partition_lbas[i], partition_lba_counts[i], partition_pad[i], (partition_active-1 == i) ? "yes" : "no", partition_types[i], partition_files[i]);

        chs_t cs;
        if (chs(partition_lbas[i], &cs))
            reterr(-EINVAL, "error calculating chs from lba %u", partition_lbas[i]);

        chs_t ce;
        if (chs(partition_lbas[i] + partition_lba_counts[i], &ce))
            reterr(-EINVAL, "error calculating chs from lba %u", partition_lbas[i]);

        long ps = ftell(out);
        fputc_((uint8_t) ((partition_active-1 == i) ? 0x80 : 0x00), out, "boot flag");
        fputc_(cs.head, out, "start chs head");
        fputc_(cs.sector, out, "start chs sector");
        fputc_(cs.cylinder, out, "start chs cylinder");
        fputc_(partition_types[i], out, "partition type");
        fputc_(ce.head, out, "end chs head");
        fputc_(ce.sector, out, "end chs sector");
        fputc_(ce.cylinder, out, "end chs cylinder");
        fwrite_(&(partition_lbas[i]), 1, 4, out, "start lba");
        fwrite_(&(partition_lba_counts[i]), 1, 4, out, "lba count");
        assert(ftell(out)-ps == 16);
    }
    fputc_((uint8_t) 0x55, out, "magic");
    fputc_((uint8_t) 0xAA, out, "magic");
    assert(ftell(out)-bs == SECTOR_SIZE);

    // Write partitions
    for (size_t i = 0; i < partitions_sz; i++) {
        FILE* f = fopen(partition_files[i], "rb");
        if (!f)
            reterr(errno, "error opening file '%s' for partition: %s", partition_files[i], strerror(errno));
        int c;
        while ((c = fgetc(f)) != EOF) {
            fputc_((uint8_t) c, out, "partition contents");
        }
        fclose(f);

        for (size_t pad = partition_pad[i]; pad > 0; pad--)
            fputc_((uint8_t) 0x00, out, "partition padding");

        assert((ftell(out)-bs)%SECTOR_SIZE == 0);
    }

    assert((ftell(out)-bs)/SECTOR_SIZE == cur_sector);

    return 0;
    #undef fputc_
    #undef fwrite_
    #undef ffn_
    #undef reterr
}

int main(int argc, char** argv) {
    short int word = 0x0001;
    char *b = (char *)&word;
    if (!b[0]) {
        printf("Error: only little-endian architectures are currently supported (chs bit stuff and integer writing)\n");
        return EXIT_FAILURE;
    }

    if (argc != 6 && argc != 8 && argc != 10 && argc != 12) {
        printf("Usage: %s OUT_PATH BOOTSTRAP_PATH ACTIVE_PARTITION_NUM PARTITION1_FILE PARTITION1_TYPE [PARTITION2_FILE PARTITION2_TYPE [PARTITION3_FILE PARTITION3_TYPE [PARTITION4_FILE PARTITION4_TYPE]]]\n", argv[0]);
        printf("\nExamples:\n");
        printf("    mkmbr disk.img \"\" 1 partition1.fat16 0x0E\n");
        printf("    mkmbr disk.img bootstrap.bin 1 partition1.fat16 0x0E\n");
        printf("    mkmbr disk.img bootstrap.bin 1 partition1.fat16 0x0E partition2.ext4 0x53\n");
        return EXIT_FAILURE;
    }

    char* out_path = argv[1];

    uint8_t bootstrap[513]; // 513 is so it can be caught if it is longer than 512
    size_t bootstrap_sz;
    if (strlen(argv[2]) > 0) {
        FILE* bf = fopen(argv[2], "rb");
        if (!bf) {
            printf("Error: could not read bootstrap: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if ((bootstrap_sz = fread(bootstrap, 1, 513, bf)) != 513 && !feof(bf)) {
            printf("Error: could not read bootstrap: %s\n", strerror(ferror(bf)));
            return EXIT_FAILURE;
        }
        fclose(bf);
    }

    long tmp = strtol(argv[3], NULL, 10);
    if (tmp < 1 || tmp > 4) {
        printf("Error: invalid active partition number\n");
        return EXIT_FAILURE;
    }
    size_t partition_active = (size_t) tmp;

    // TODO: refactor
    char* partition_files[4];
    uint8_t partition_types[4];
    size_t partitions_sz = -1;
    if (argc >= 6) {
        partitions_sz++;
        partition_files[partitions_sz] = argv[4];
        long t1 = strtol(argv[5], NULL, 16);
        if (t1 < 1 || t1 > 0xFF) {printf("Error: invalid partition 1 type\n"); return EXIT_FAILURE;}
        partition_types[partitions_sz] = (uint8_t) t1;
        assert(partitions_sz == 0);
    }
    if (argc >= 8) {
        partitions_sz++;
        partition_files[partitions_sz] = argv[6];
        long t2 = strtol(argv[7], NULL, 16);
        if (t2 < 1 || t2 > 0xFF) {printf("Error: invalid partition 1 type\n"); return EXIT_FAILURE;}
        partition_types[partitions_sz] = (uint8_t) t2;
        assert(partitions_sz == 1);
    }
    if (argc >= 10) {
        partitions_sz++;
        partition_files[partitions_sz] = argv[8];
        long t3 = strtol(argv[9], NULL, 16);
        if (t3 < 1 || t3 > 0xFF) {printf("Error: invalid partition 1 type\n"); return EXIT_FAILURE;}
        partition_types[partitions_sz] = (uint8_t) t3;
        assert(partitions_sz == 2);
    }
    if (argc >= 12) {
        partitions_sz++;
        partition_files[partitions_sz] = argv[10];
        long t4 = strtol(argv[11], NULL, 16);
        if (t4 < 1 || t4 > 0xFF) {printf("Error: invalid partition 1 type\n"); return EXIT_FAILURE;}
        partition_types[partitions_sz] = (uint8_t) t4;
        assert(partitions_sz == 3);
    }
    partitions_sz++;

    FILE* f = fopen(out_path, "wb");
    if (!f) {
        printf("Error: could not create output: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    char* err;
    if (mkmbr(bootstrap, bootstrap_sz, partition_active, partition_files, partition_types, partitions_sz, true, &err, f)) {
        printf("Error: could not generate image: %s\n", err);
        return EXIT_FAILURE;
    }

    fclose(f);
}
