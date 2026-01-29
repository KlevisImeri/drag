/*
 * MIT License
 *
 * Copyright (c) 2026 Klevis Imeri
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DRAG_SHARED_H
#define DRAG_SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "macros.h"
#define FONT8x16_IMPLEMENTATION
#include "font8x16.h"


#define COLOR_BG   0xFF222222
#define COLOR_TEXT 0xFFFFFFFF

static const int CHAR_W = 8;
static const int CHAR_H = 16;
static const int PADDING_X = 12;
static const int PADDING_Y = 8;

// RFC 3986
// Turns '/home/user/My File.txt' into 'file:///home/user/My%20File.txt\r\n'
char* CreateUriList(const char *path) {
  if (!path) return NULL;

  size_t len = strlen(path);
  char *output = malloc(len * 3 + 16); 
  if (!output) return NULL;

  char *p = output;
  p += sprintf(p, "file://");

  if (path[0] != '/') *p++ = '/';

  for (const char *s = path; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || 
        c == '_' || c == '~' || c == '/') {
      *p++ = c;
    } else {
      p += sprintf(p, "%%%02X", c);
    }
  }

  *p++ = '\r';
  *p++ = '\n';
  *p = '\0';
  return output;
}

typedef struct {
  char *uri;
  char *name;
} FileInfo;

void FileInfoFree(FileInfo *info) {
  if (!info) return;
  if (info->uri) free(info->uri);
  if (info->name) free(info->name);
  free(info);
}

FileInfo* CommandLineArguments(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s <file_path>\n", argv[0]);
    return NULL;
  }

  char *path = realpath(argv[1], NULL);
  if (!path) {
    LOG("Error resolving path %s", argv[1]);
    return NULL;
  }
  defer { free(path); };

  char *uri = CreateUriList(path);
  if (!uri) {
    LOG("Error creating uri");
    return NULL; 
  }
  defer { if (uri) free(uri); };

  FileInfo *result = calloc(1, sizeof(FileInfo));
  if (!result) {
    LOG("Memory allocation failed");
    return NULL; 
  }
  defer { if (result) FileInfoFree(result); };

  result->uri = uri;
  uri = NULL; 

  char *name_ptr = strrchr(path, '/');
  if (name_ptr) name_ptr++;
  else name_ptr = path;

  result->name = strdup(name_ptr);

  if (!result->name) {
    LOG("String duplication failed");
    return NULL;
  }

  LOG("Dragging: %s, Name: %s", result->uri, result->name);
 
  FileInfo *retval = result;
  result = NULL;

  return retval;
}

static void GetTextSize(const char *text, int *w, int *h) {
  int len = strlen(text);
  *w = (len * CHAR_W) + (PADDING_X * 2);
  *h = CHAR_H + (PADDING_Y * 2);
}

static void RenderTextToBuffer(const char *text, unsigned int *pixels, int w, int h) {
  int len = strlen(text);

  for (int i = 0; i < w * h; i++) {
    pixels[i] = COLOR_BG;
  }

  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    for (int r = 0; r < 16; r++) { 
      for (int col = 0; col < 8; col++) {
        if (font8x16[c][r] & (0x80 >> col)) {
          int x = PADDING_X + (i * CHAR_W) + col;
          int y = PADDING_Y + r;
          pixels[y * w + x] = 0xFFFFFFFF;
        }
      }
    }
  }
}

#endif // DRAG_SHARED_H
