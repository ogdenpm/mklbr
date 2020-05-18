#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#pragma warning(disable : 4996)

#define MAXITEM 256     // maximum number of items the tool supports in lbr

#ifdef _WIN32
#define DIRSEP  ":\\/"
#else
#define DIRSEP "/"
#endif

typedef struct {
    const char *loc;
    const char *name;
    size_t fileSize;
    uint16_t secCnt;
    time_t ctime;
    time_t mtime;

} item_t;

// LBR directory offsets
#define DIRSIZE 32
enum hdrOffsets {
    Status = 0, Name = 1, Ext = 9, Index = 12,
    Length = 14, Crc = 16, CreateDate = 18, ChangeDate = 20,
    CreateTime = 22, ChangeTime = 24, PadCnt = 26, Filler = 27
};

int cnt = 1;
int entries = 4;
item_t items[MAXITEM] = { "" };          // list of items to add, item 0 is the header
uint8_t *hdr;
uint8_t *ioBuf;

uint16_t calcCrc(uint8_t *buf, int len) {
    uint8_t x;
    uint16_t crc = 0;

    while (len-- > 0) {
        x = (crc >> 8) ^ *buf++;
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc;
}



void addItem(const char *s) {
    char *cs = strdup(s);       // copy for persistence
    if (cnt >= MAXITEM) {
        fprintf(stderr, "Too many files, recompile with larger MAXITEM\n");
        exit(1);
    }
    items[cnt].loc = cs;

    while (cs = strchr(cs, ':')) {
        if (cs[1] == ':') {       // double :: indicates name follows
            *cs = 0;
            items[cnt].name = cs + 2;
            break;
        }
        cs++;
    }
    struct stat stbuf;
    if (stat(items[cnt].loc, &stbuf) != 0) {
        fprintf(stderr, "cannot find %s -- ignoring\n", items[cnt].loc);
        free((void *)items[cnt].loc);
    } else {
        items[cnt].fileSize = stbuf.st_size;
        items[cnt].secCnt = (items[cnt].fileSize + 127) / 128;
        items[cnt].ctime = stbuf.st_ctime;
        items[cnt].mtime = stbuf.st_mtime;
        cnt++;
    }
}

void loadRecipe(const char *name) {
    name++;             // past @
    FILE *fp;
    if ((fp = fopen(name, "rt")) == NULL) {
        fprintf(stderr, "cannot open %s\n", name);
        exit(1);
    }
    char line[256];
    while (fgets(line, 256, fp)) {
        char *s;
        if (s = strchr(line, '\n'))
            *s = 0;
        if(*line && *line != ' ' && *line != '#')
            addItem(line);
    }
    fclose(fp);
}

void time2Lbr(uint8_t *s, time_t tval) {
    uint16_t d = tval / 86400 - 2921;   // CP/M day 0 is unix day 2921
    uint8_t h = tval % 86400 / 3600;   // hour
    uint8_t m = tval % 3600 / 60;      // minute
    uint8_t s2 = tval % 60 / 2;        // 2 second
    uint16_t ltime = (h << 11) + (m << 5) + s2;
    s[0] = d & 0xff;
    s[1] = d >> 8;
    s[4] = ltime & 0xff;
    s[5] = ltime >> 8;
}

void setName(int i) {
    const char *name = items[i].name;
    char *s;
    const char *t;

    if (!name) {                // get name from input location if not explicit
        for (name = items[i].loc; s = strpbrk(name, DIRSEP);)
            name = s + 1;
    }
    s = &hdr[i * DIRSIZE + Name];
    memset(s, ' ', 11);
    t = name;
    for (int j = 0; j < 8 && *t && *t != '.'; j++)
        *s++ = toupper(*t);
    if (*t && *t != '.')
        fprintf(stderr, "Truncating %s to 8 char name\n", name);
    while (*t && *t++ != '.')
        ;
    s = &hdr[i * DIRSIZE + Ext];
    for (int j = 0; j < 3 && *t; j++)
        *s++ = toupper(*t);
    if (*t)
        fprintf(stderr, "Truncating %s to 3 char extent\n", name);

}



void initHdr() {
    uint16_t index = 0;
    uint16_t largest = 0;
    entries = ((cnt + 3) & ~3);
    items[0].fileSize = entries * DIRSIZE;

    hdr = calloc(entries, DIRSIZE);
    for (int i = 0; i < cnt; i++) {
        hdr[i * DIRSIZE + Status] = 0;
        setName(i);
        time2Lbr(&hdr[i * DIRSIZE + CreateDate], items[i].ctime);
        time2Lbr(&hdr[i * DIRSIZE + ChangeDate], items[i].mtime);
        hdr[i * DIRSIZE + Index] = index & 0xff;
        hdr[i * DIRSIZE + Index + 1] = index >> 8;
        hdr[i * DIRSIZE + Length] = items[i].secCnt & 0xff;
        hdr[i * DIRSIZE + Length + 1] = items[i].secCnt >> 8;
        hdr[i * DIRSIZE + PadCnt] = items[i].secCnt * 128 - items[i].fileSize;
        index += items[i].secCnt;
        if (items[i].secCnt > largest)
            largest = items[i].secCnt;
    }

    for (int i = cnt; i < entries; i++)
        hdr[i * DIRSIZE] = 0xff;
    ioBuf = malloc(largest * 128);
}


void buildLbr(const char *name) {
    FILE *fp;
    uint16_t crc;

    if ((fp = fopen(name, "wb")) == NULL) {
        fprintf(stderr, "cannot create %s\n", name);
        exit(1);
    }
    initHdr();
    if (fwrite(hdr, DIRSIZE, entries, fp) != entries) {
        fprintf(stderr, "cannot write header\n");
        exit(1);
    }
    for (int i = 1; i < cnt; i++) {
        FILE *fpin = fopen(items[i].loc, "rb");
        if (fpin == NULL) {
            fprintf(stderr, "cannot read %s\n", items[i].loc);
            exit(1);
        }
        if (fread(ioBuf, 1, items[i].fileSize, fpin) != items[i].fileSize) {
            fprintf(stderr, "error reading %s\n", items[i].loc);
            exit(1);
        }
        fclose(fpin);
        if (items[i].fileSize % 128)
            memset(ioBuf + items[i].fileSize, 0x1a, 128 - (items[i].fileSize % 128)); 
        if (fwrite(ioBuf, 128, items[i].secCnt, fp) != items[i].secCnt) {
            fprintf(stderr, "error writing %s to lbr\n", items[i].loc);
            exit(1);
        }
        crc = calcCrc(ioBuf, items[i].secCnt * 128);
        hdr[i * DIRSIZE + Crc] = crc & 0xff;
        hdr[i * DIRSIZE + Crc + 1] = crc >> 8;
    }
    // now calculate the headers own CRC
    crc = calcCrc(hdr, items[0].secCnt * 128);
    hdr[Crc] = crc & 0xff;
    hdr[Crc + 1] = crc >> 8;
    rewind(fp);
    if (fwrite(hdr, DIRSIZE, entries, fp) != entries) {
        fprintf(stderr, "failed to update header\n");
        exit(1);
    }
    fclose(fp);
}




int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: mklbr lbrfile [file | @file]+\n");
        exit(1);
    }
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '@')
            loadRecipe(argv[i]);
        else
            addItem(argv[i]);
    }
    buildLbr(argv[1]);
}