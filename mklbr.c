#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#if _MSC_VER
#include <sys/utime.h>
#else
#include <unistd.h>
#include <utime.h>
#endif
#include <ctype.h>
#include <time.h>

#define CPMDAY0 2921

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
    uint32_t ctime;
    uint32_t mtime;

} item_t;

// LBR directory offsets
#define DIRSIZE 32
enum hdrOffsets {
    Status = 0, Name = 1, Ext = 9, Index = 12,
    Length = 14, Crc = 16, CreateDate = 18, ChangeDate = 20,
    CreateTime = 22, ChangeTime = 24, PadCnt = 26, Filler = 27
};

int cnt = 0;
int entries = 4;
item_t items[MAXITEM];          // list of items to add, item 0 is the header
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

// as far as I can tell lbr stores date in local time format
// tval here is in utc so adjust to local time first
uint32_t time2Lbr(time_t tval) {
    // adjust tval to reflect localtime vs. UTC
    struct tm *timestamp = gmtime(&tval);           // get raw utc time
    int yday = timestamp->tm_yday;                  // and the relevant day
    timestamp = localtime(&tval);                   // the time in local time

    uint32_t d = tval / 86400 - CPMDAY0;            //  adjust for CP/M day 0
    d += (timestamp->tm_yday - yday);               // update day if local time is day before/after gmt
    return (d << 16) + (timestamp->tm_hour << 11) + (timestamp->tm_min << 5) + timestamp->tm_sec / 2;
}


// set modify and access times convert lbr time to windows/unix time
// compensate for localtime shift
void setFileTime(char const *path, uint32_t lftime) {
    uint16_t d = (lftime >> 16) + CPMDAY0;
    int32_t t = ((lftime >> 11) % 32) * 3600 + ((lftime >> 5) % 64) * 60 + (lftime % 32 * 2);
    time_t ftime = d * 86400 + t;
    struct tm *timestamp = gmtime(&ftime);
    int yday = timestamp->tm_yday;
    timestamp = localtime(&ftime);
    d -= (timestamp->tm_yday - yday);
    ftime = d * 86400 + 2 * t - (timestamp->tm_hour * 3600 + timestamp->tm_min * 60 + timestamp->tm_sec);


    struct utimbuf times = { ftime, ftime };
    utime(path, &times);

}

// items in the recipe file have the following format
// src?[name]?[ctime]?[mtime]
// where src is the file to include, except the first which is the target lbr file
// note src and name cannot contain ?
// name is the name to be used in the lbr, if omitted filename part of src is used
// note name is always converted to upper case
// ctime is the create time in format yyyy-mm-dd hh:mm:ss  or 0 if force set to 0
// mtime is the modify time in same format
// note if ctime/mtime are missing the file timestamps are used, except for the lbr file
// which will use the lastest ctime/mtime value from the input files and sets both its
// ctime and mtime to this value
// Note, trailling question marks can be ommitted
// blank lines or lines beginning with a space or # are ignored

const char *parseToken(char **line) {
    char *token = *line;
    if (*line = strchr(token, '?')) {
        *(*line)++ = 0;
    } else
        *line = strchr(token, '\0');
    // trim leading/trailing white space
    while (isspace(*token))
        token++;
    for (char *t = strchr(token, '\0'); --t >= token && isspace(*t); )
        *t = 0;
    return token;
}

uint32_t parseTimeStamp(const char *s) {
    struct tm tbuf;
    if (!*s)
        return ~0;
    if (*s == '-')
        return 0;
    
    if (sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &tbuf.tm_year, &tbuf.tm_mon, &tbuf.tm_mday, &tbuf.tm_hour, &tbuf.tm_min, &tbuf.tm_sec) == 6) {
        tbuf.tm_mon--;
        tbuf.tm_year -= 1900;
        return time2Lbr(mktime(&tbuf));
    }
    return ~0;
}


void addItem(char *line) {
    if (cnt >= MAXITEM) {
        fprintf(stderr, "Too many files, recompile with larger MAXITEM\n");
        exit(1);
    }
    const char *s = parseToken(&line);
    if (*s == 0) {
        fprintf(stderr, "File location missing\n");
        return;
    }
    if (cnt == 0)                  // first item is library name which may not be found
        items[cnt].ctime = items[cnt].mtime = ~0;        // sentinal for auto fill
    else {
        struct stat stbuf;
        if (stat(s, &stbuf) != 0) {
            fprintf(stderr, "cannot find %s -- ignoring\n", s);
            return;
        } else {
            items[cnt].fileSize = stbuf.st_size;
            items[cnt].secCnt = (items[cnt].fileSize + 127) / 128;
            items[cnt].ctime = time2Lbr(stbuf.st_ctime);          // set default timestamps
            items[cnt].mtime = time2Lbr(stbuf.st_mtime);
        }
    }
    items[cnt].loc = strdup(s);

    s = parseToken(&line);
    if (cnt == 0)
        s = "";
    else if (!*s) {                                      // implicit name
        char *t;                                         // derive name from src file
        s = items[cnt].loc;
        while (t = strpbrk(s, DIRSEP))
            s = t + 1;
    }
    items[cnt].name = strdup(s);

    uint32_t timestamp = parseTimeStamp(parseToken(&line));
    if (timestamp != ~0)
        items[cnt].ctime = timestamp;
    timestamp = parseTimeStamp(parseToken(&line));
    if (timestamp != ~0)
        items[cnt].mtime = timestamp;
    cnt++;
}

void loadRecipe(const char *name) {
    FILE *fp;
    if ((fp = fopen(name, "rt")) == NULL) {
        fprintf(stderr, "cannot open %s\n", name);
        exit(1);
    }
    char line[256];
    while (fgets(line, 256, fp)) {
        char *s = strchr(line, '\0');
        if(*line && *line != ' ' && *line != '#')
            addItem(line);
    }
    fclose(fp);
}





void setName(int i) {
    char *s  = &hdr[i * DIRSIZE + Name];;
    const char *t = items[i].name;

    memset(s, ' ', 11);

    for (int j = 0; j < 8 && *t && *t != '.'; j++)
        *s++ = toupper(*t++);
    if (*t && *t != '.')
        fprintf(stderr, "Truncating %s to 8 char name\n", items[i].name);
    while (*t && *t++ != '.')
        ;
    s = &hdr[i * DIRSIZE + Ext];
    for (int j = 0; j < 3 && *t; j++)
        *s++ = toupper(*t++);
    if (*t)
        fprintf(stderr, "Truncating %s to 3 char extent\n", items[i].name);

}

void setDate(uint8_t *d, uint32_t timestamp) {
    d[0] = (timestamp >> 16) % 256;
    d[1] = timestamp >> 24;
    d[4] = timestamp % 256;
    d[5] = (timestamp >> 8) % 256;
}

void initHdr() {
    uint16_t index = 0;
    uint16_t largest = 0;
    uint32_t ctime = 0, mtime = 0;
    entries = ((cnt + 3) & ~3);
    items[0].fileSize = entries * DIRSIZE;
    items[0].secCnt = entries * DIRSIZE / 128;

    if (items[0].ctime == ~0) {
        items[0].ctime = 0;
        for (int i = 1; i < cnt; i++)
            if (items[0].ctime < items[i].ctime)
                items[0].ctime = items[i].ctime;
    }
    if (items[0].mtime == ~0) {
        items[0].mtime = 0;
        for (int i = 1; i < cnt; i++)
            if (items[0].mtime < items[i].mtime)
                items[0].mtime = items[i].mtime;
    }

    hdr = calloc(entries, DIRSIZE);
    for (int i = 0; i < cnt; i++) {
        hdr[i * DIRSIZE + Status] = 0;
        setName(i);
        setDate(&hdr[i * DIRSIZE + CreateDate], items[i].ctime);
        if (items[i].ctime > ctime)
            ctime = items[i].ctime;
        setDate(&hdr[i * DIRSIZE + ChangeDate], items[i].mtime);
        if (items[i].mtime > mtime)
            mtime = items[i].mtime;
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


void buildLbr() {
    FILE *fp;
    uint16_t crc;
    const char *lbrname = items[0].loc;

    if ((fp = fopen(lbrname, "wb")) == NULL) {
        fprintf(stderr, "cannot create %s\n", lbrname);
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
    if (items[0].mtime)
        setFileTime(lbrname, items[0].mtime);
}




int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: mklbr recipefile\n");
        exit(1);
    }
    loadRecipe(argv[1]);
    if (cnt == 0)
        fprintf(stderr, "recipe file has no files\n");
    else
        buildLbr();
}