/* mklbr - create .lbr archives from recipes
 * Copyright (C) - 2020-2023 Mark Ogden
 *
 * mklbr.c - main program
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if _MSC_VER
#include <sys/utime.h>
#define timegm _mkgmtime
#else
#include <unistd.h>
#include <utime.h>
#endif
#include "showVersion.h"
#include <ctype.h>
#include <time.h>

#define CPMDAY0 2921

#pragma warning(disable : 4996)

#define MAXITEM 512 // maximum number of items the tool supports in lbr

#ifdef _WIN32
#define DIRSEP ":\\/"
#else
#define DIRSEP "/"
#endif

#define BADCHAR     " =?*:;<>" // illegal in CP/M 2 & 3
#define PROBLEMCHAR ",_[]|"    // illegal dependent on version of CP/M

typedef struct {
    char *loc;
    char *name;
    size_t fileSize;
    uint16_t secCnt;
    time_t ctime; // times are stored in utc format
    time_t mtime;

} item_t;

// LBR directory offsets
#define DIRSIZE 32
typedef uint8_t dir_t[DIRSIZE];
enum hdrOffsets {
    Status     = 0,
    Name       = 1,
    Ext        = 9,
    Index      = 12,
    Length     = 14,
    Crc        = 16,
    CreateDate = 18,
    ChangeDate = 20,
    CreateTime = 22,
    ChangeTime = 24,
    PadCnt     = 26,
    Filler     = 27
};

int cnt     = 0;
int entries = 4;
item_t items[MAXITEM];         // list of items to add, item 0 is the header
uint8_t hdr[MAXITEM][DIRSIZE]; // constructed header
uint8_t *ioBuf;
bool verbose;

time_t parseTimeStamp(char **line);

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

// set modify and access times
void setFileTime(char const *path, time_t ftime) {
    struct utimbuf times = { ftime, ftime };
    utime(path, &times);
}

// items in the recipe file have the following format
// src [ '|' name] [mtime] [ctime]
// where src is the file to include, except the first which is the target lbr file
// to include spaces in the src name enclose it in <>
// name is the name to be used in the lbr, if omitted filename part of src is used
// note name is always converted to upper case. If filename part of src contains spaces
// then name must be present
// time formats are stored in UTC. Options supported are
//  yyyy-mm-dd hh:mm:ss     explicit date/time
//  *                       use corresponding time from src file
//  -                       set to 0
// mtime is the modify time
// ctime is the create time
// If mtime is before ctime then ctime is set to mtime, if ctime expliclty set this will issue a
// warning Note, trailing options can be omitted. For times * is assumed, except for the lbr file
// whcih will use the lastest ctime/mtime value from the input files and sets both its
// ctime and mtime to this value
// blank lines or lines beginning with a space or # are ignored. If the src file starts with a #
// enclose the name in <>

char *basename(char *path) {
    char *s;
#ifdef _WIN32
    if (path[0] && path[1] == ':') // skip leading device
        path += 2;
#endif
    while ((s = strpbrk(path, DIRSEP))) // skip all directory components
        path = s + 1;
    return path;
}

void trim(char *s) {
    char *t = strchr(s, '\0') - 1;
    while (t >= s && *t == ' ')
        *t-- = '\0';
}

char *skipWS(char *s) {
    while (isspace(*s))
        s++;
    return s;
}

char *skipNonWS(char *s) {
    while (*s && !isspace(*s))
        s++;
    return s;
}

// split recipe line
// return true if parsed, else false
bool parseLine(char *line, int cnt) {
    char *src  = line;
    char *name = NULL;

    if (*src == '<') {
        src  = skipWS(src + 1);
        line = strchr(src, '>');
        if (!line)
            return -1;
        *line++ = '\0'; // replace trailing '>'
        trim(src);
        line = skipWS(line);
        if (*line == '|')
            name = ++line;
    } else {
        line = skipNonWS(line);
        if (*line)
            *line++ = '\0';
        char *s = strchr(src, '|');
        if (s) {
            *s   = '\0';
            name = s + 1;
        } else {
            line = skipWS(line);
            if (*line == '|')
                name = ++line;
        }
    }
    if (name) {
        name = skipWS(name);
        line = skipNonWS(name);
        if (*line)
            *line++ = '\0';
    } else
        name = basename(src);
    char *s;
    if (!*name || strpbrk(name, BADCHAR) ||
        ((s = strchr(name, '.')) && (!s[1] || strchr(s + 1, '.')))) {
        fprintf(stderr, "Bad CP/M name '%s'\n", name);
        return false;
    }
    if (strpbrk(name, PROBLEMCHAR))
        fprintf(stderr, "Warning: Version dependent CP/M name '%s'\n", name);
    items[cnt].loc   = strdup(src);
    items[cnt].name  = strdup(name);
    items[cnt].mtime = parseTimeStamp(&line);
    items[cnt].ctime = parseTimeStamp(&line);
    return true;
}

time_t parseTimeStamp(char **line) {
    char *s = skipWS(*line);

    if (!*s || *s == '-' || *s == '*') {
        *line = *s ? s + 1 : s;
        return *s == '-' ? 0 : -1;
    }
    struct tm tbuf;

    char *end = skipNonWS(skipWS(skipNonWS(s)));
    *line     = *end ? end + 1 : end;
    *end      = '\0';

    int scnt  = sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &tbuf.tm_year, &tbuf.tm_mon, &tbuf.tm_mday,
                       &tbuf.tm_hour, &tbuf.tm_min, &tbuf.tm_sec);
    if (scnt >= 5) {
        if (scnt == 5)
            tbuf.tm_sec = 0;
        tbuf.tm_mon--;
        tbuf.tm_year -= 1900;
        return timegm(&tbuf);
    }
    fprintf(stderr, "Warning: invalid timestamp information %s\n", s);
    return -1;
}

void addItem(char *line) {
    if (cnt >= MAXITEM) {
        fprintf(stderr, "Too many files, recompile with larger MAXITEM\n");
        exit(1);
    }
    if (!parseLine(line, cnt))
        return;

    if (cnt) { // first is lbr file which has special handling of auto timestamps
        struct stat stbuf;
        if (stat(items[cnt].loc, &stbuf) != 0) {
            fprintf(stderr, "cannot find %s -- ignoring\n", items[cnt].loc);
            free(items[cnt].loc);
            free(items[cnt].name);
            return;
        } else {
            items[cnt].fileSize = stbuf.st_size;
            items[cnt].secCnt   = (uint16_t)((items[cnt].fileSize + 127) / 128);
            // set default timestamps
            bool autoCtime = items[cnt].ctime == -1;
            if (autoCtime)
                items[cnt].ctime = timegm(localtime(&stbuf.st_ctime));
            if (items[cnt].mtime == -1)
                items[cnt].mtime = timegm(localtime(&stbuf.st_mtime));
            if (0 < items[cnt].mtime && items[cnt].mtime < items[cnt].ctime) {
                if (!autoCtime)
                    fprintf(
                        stderr,
                        "%s: modify time before create time. Setting both to earliest timestamp\n",
                        items[cnt].loc);
                items[cnt].ctime = items[cnt].mtime;
            }
        }
    }
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
        char *s = strchr(line, '\0') - 1;
        if (s < line || *s != '\n') {
            fprintf(stderr, "Recipe line too long: %s\n", line);
            exit(1);
        }
        s = skipWS(line);
        if (*s && *s != '#')
            addItem(s);
    }
    fclose(fp);
}

void setName(int i) {
    char *s       = &hdr[i][Name];
    const char *t = items[i].name;

    memset(s, ' ', 11);
    if (i == 0)
        return;
    for (int j = 0; j < 8 && *t && *t != '.'; j++)
        *s++ = toupper(*t++);

    if (*t && *t != '.')
        fprintf(stderr, "Truncating %s to 8 char name\n", items[i].name);
    while (*t && *t++ != '.')
        ;
    s = &hdr[i][Ext];
    for (int j = 0; j < 3 && *t; j++)
        *s++ = toupper(*t++);

    if (*t)
        fprintf(stderr, "Truncating %s to 3 char extent\n", items[i].name);
}

void setDate(uint8_t *d, time_t tval) {
    // store the date in utc format, so what user enters matches
    struct tm *timestamp = gmtime(&tval); // get raw utc time
    uint16_t lbrDay =
        (uint16_t)(tval / 86400 - CPMDAY0); //  adjust for CP/M day 0
    uint16_t lbrTime =
        (timestamp->tm_hour << 11) + (timestamp->tm_min << 5) + timestamp->tm_sec / 2;

    // note lbr day and time are split
    d[0] = lbrDay % 256;
    d[1] = lbrDay / 256;
    d[4] = lbrTime % 256;
    d[5] = lbrTime / 256;
}

void initHdr() {
    uint16_t index    = 0;
    uint16_t largest  = 0;
    entries           = (cnt + 3) / 4 * 4;
    items[0].fileSize = entries * DIRSIZE;
    items[0].secCnt   = entries * DIRSIZE / 128;

    time_t now;
    time(&now);

    if (items[0].mtime < 0) {
        for (int i = 1; i < cnt; i++)
            if (items[0].mtime < items[i].mtime)
                items[0].mtime = items[i].mtime;
        if (items[0].mtime < 0)
            items[0].mtime = now;
    }
    if (items[0].ctime < 0)
        items[0].ctime = items[0].mtime;
    else if (items[0].ctime > items[0].mtime) {
        fprintf(stderr, "library create time later than modify time, setting to modify time\n");
        items[0].ctime = items[0].mtime;
    }

    for (int i = 0; i < cnt; i++) {
        hdr[i][Status] = 0;
        setName(i);
        setDate(&hdr[i][CreateDate], items[i].ctime);
        setDate(&hdr[i][ChangeDate], items[i].mtime);
        hdr[i][Index]      = index % 256;
        hdr[i][Index + 1]  = index / 256;
        hdr[i][Length]     = items[i].secCnt % 256;
        hdr[i][Length + 1] = items[i].secCnt / 256;
        hdr[i][PadCnt]     = (uint8_t)(items[i].secCnt * 128 - items[i].fileSize);
        index += items[i].secCnt;
        if (items[i].secCnt > largest)
            largest = items[i].secCnt;
    }

    for (int i = cnt; i < entries; i++)
        hdr[i][0] = 0xff;
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
        crc             = calcCrc(ioBuf, items[i].secCnt * 128);
        hdr[i][Crc]     = crc % 256;
        hdr[i][Crc + 1] = crc / 256;
    }
    // now calculate the headers own CRC
    items[0].fileSize = ftell(fp);
    crc               = calcCrc(hdr[0], items[0].secCnt * 128);
    hdr[0][Crc]       = crc % 256;
    hdr[0][Crc + 1]   = crc / 256;
    rewind(fp);
    if (fwrite(hdr, DIRSIZE, entries, fp) != entries) {
        fprintf(stderr, "failed to update header\n");
        exit(1);
    }
    fclose(fp);
    if (items[0].mtime)
        setFileTime(lbrname, items[0].mtime);
}

void displayDate(const time_t date) {
    struct tm const *timeptr = gmtime(&date);
    printf("%04d-%02d-%02d %02d:%02d:%02d", 1900 + timeptr->tm_year, timeptr->tm_mon + 1,
           timeptr->tm_mday, timeptr->tm_hour, timeptr->tm_min, timeptr->tm_sec);
}

void list() {
    char cpmName[12];

    printf("%-18s %7s  %-4s      %-19s  %s\n", "File", "Size", "CRC", "Modify Time", "Create Time");
    printf("%-18s  %6zd  %02X%02X  ", basename(items[0].loc), items[0].fileSize, hdr[0][Crc + 1],
           hdr[0][Crc]);
    displayDate(items[0].mtime);
    putchar(' ');
    putchar(' ');
    displayDate(items[0].ctime);
    putchar('\n');
    for (int i = 1; i < cnt; i++) {
        char *s = cpmName;
        for (int j = 0; j < 8 && hdr[i][Name + j] != ' '; j++)
            *s++ = hdr[i][Name + j];
        if (hdr[i][Ext] != ' ') {
            *s++ = '.';
            for (int j = 0; j < 3 && hdr[i][Ext + j] != ' '; j++)
                *s++ = hdr[i][Ext + j];
        }
        *s = '\0';
        printf("  %-16s  %6zd  %02X%02X  ", cpmName, items[i].fileSize, hdr[i][Crc + 1],
               hdr[i][Crc]);
        if (items[i].mtime)
            displayDate(items[i].mtime);
        else if (items[i].ctime)
            printf("%-15s ", "");
        if (items[i].ctime) {
            putchar(' ');
            putchar(' ');
            displayDate(items[i].ctime);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    CHK_SHOW_VERSION(argc, argv);
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        argc--, argv++;
        verbose = true;
    }
    if (argc < 2) {
        fprintf(
            stderr,
            "Usage: mklbr -v | -V | [-v] (recipefile | lbrfile files+)\n"
            "Where a single -v or -V shows version information\n"
            "Otherwise -v provides additional information on the created lbrfile\n"
            "The recipe file option makes it easier to handle multiple timestamps and CP/M file "
            "naming\n"
            "However lbrfile and files are recipes and can be quoted to include more than the "
            "sourcefile\n"
            "Each recipe has the following format\n"
            "  sourcefile [ '|' lbrname] [modifytime] [createtime]\n"
            "\n"
            "If lbrname is omitted then filename part of sourcefile is used\n"
            "the name will be converted to uppercase\n"
            "The sourcefile can be surrounded by <> to allow embedded spaces, e.g. directory path\n"
            "However if there are embedded spaces in the filename part, lbrname must be specified\n"
            "\n"
            "Time information is one of\n"
            "  yyyy-mm-dd hh:mm[:ss] -- explicitly set UTC time\n"
            "  -                     -- zero timestamps\n"
            "  *                     -- use file timestamps (default)\n"
            "If createtime is sepecified then modifytime has to be specified\n"
            "Additionally createtime is set to modifytime if it is later\n"
            "If this occurs when an explicit timestamp is used, a is warning issued\n"
            "Note the first source file should be the name of the lbr file to create\n"
            "in this case when time information is missing the max timestamps from the source "
            "files is used\n"
            "The current time is used if all timestamps are set to 0 and lbr is not "
            "explicitly set\n"
            "\n");
        exit(1);
    }
    if (argc == 2)
        loadRecipe(argv[1]);
    else
        for (int i = 1; i < argc; i++)
            addItem(argv[i]);
    if (cnt == 0)
        fprintf(stderr, "Library has no files\n");
    else {
        buildLbr();
        if (verbose)
            list();
    }
}