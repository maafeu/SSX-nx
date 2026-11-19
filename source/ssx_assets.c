/* ssx_assets.c -- the APK's assets/, read in place, for the NDK's
 * AAssetManager (libgame.so's imports) and Java's AssetManager (ssx_java.c).
 *
 * SSX's APK holds 1.75 GB of assets in 11,200 entries. Its media -- the
 * movies (.mp4), the music streams (.mus/.sbs), the UI movies (.gfx) and the
 * level .bin files -- are STORED (aapt -0): they are read straight out of the
 * APK at their offset, and AAsset_openFileDescriptor hands the game a plain
 * descriptor on the APK with that offset, as Android's does. The rest
 * (textures, geometry, sound banks, scripts) is DEFLATED: an asset of up to
 * SSX_ASSET_WHOLE bytes is inflated whole when opened; a larger one is
 * inflated as it is read (a backward seek starts it again).
 *
 * The central directory is read once at boot into a sorted table with a hash
 * on top: an open is a lookup, no file system walk. Reads go through the
 * runtime's APK cache (dcr_apkcache.c) and otherwise through one FsFile with
 * positional reads (fsFileRead), which any number of threads can share. MIT.
 */
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "dcr_apkcache.h"
#include "rt_apkfind.h"
#include "ssx.h"
#include "util.h"

int ssx_assetpatch_wants(const char *name); /* ssx_assetpatch.c */
uint8_t *ssx_assetpatch_apply(const char *name, const uint8_t *data, uint32_t len, uint32_t *out_len);

/* Inflated whole when opened up to this size; larger ones stream. */
#define SSX_ASSET_WHOLE (8u << 20)
#define STREAM_IN (64u << 10)

typedef struct {
  uint32_t name;      /* into g_pool: the path under assets/ */
  uint32_t lho;       /* local header, from the APK's start */
  uint32_t comp, size;
  uint8_t method;     /* 0 stored, 8 deflated */
  uint32_t crc;       /* the zip's CRC-32 of the uncompressed bytes */
  volatile uint32_t data; /* data offset, 0 until first known */
} Entry;

static Entry *g_ent;
static int g_count;
static char *g_pool;
static size_t g_pool_len, g_pool_cap;
static int32_t *g_hash, *g_hash_ci; /* entry index + 1; 0 = empty */
static uint32_t g_hmask;
static FsFile g_apk;
static int g_apk_open;
static char g_apk_path[512];

static struct {
  volatile unsigned long opens, misses, ci_hits, stored, whole, streamed, fd_opens;
  volatile unsigned long long bytes;
} g_st;

static const char *ename(const Entry *e) { return g_pool + e->name; }

static uint32_t hash_str(const char *s, int ci) {
  uint32_t h = 0x811C9DC5u;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (ci && c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
    h = (h ^ c) * 0x01000193u;
  }
  return h;
}

/* ------------------------------------------------------------ the index */
static int collect(const RtZipEntry *z, void *ctx) {
  if (z->name_len <= 7 || strncmp(z->name, "assets/", 7) || z->name[z->name_len - 1] == '/')
    return 0;
  if (z->name_len >= sizeof z->name)
    return 0; /* cut: not a name the game can ask for */
  if (g_count % 1024 == 0) {
    Entry *n = realloc(g_ent, (size_t)(g_count + 1024) * sizeof *g_ent);
    if (!n)
      return 1;
    g_ent = n;
  }
  size_t len = z->name_len - 7 + 1;
  if (g_pool_len + len > g_pool_cap) {
    size_t cap = g_pool_cap ? g_pool_cap * 2 : (1u << 20);
    while (cap < g_pool_len + len)
      cap *= 2;
    char *p = realloc(g_pool, cap);
    if (!p)
      return 1;
    g_pool = p, g_pool_cap = cap;
  }
  memcpy(g_pool + g_pool_len, z->name + 7, len);
  Entry *e = &g_ent[g_count++];
  e->name = (uint32_t)g_pool_len;
  e->lho = z->lho;
  e->comp = z->comp_size;
  e->size = z->size;
  e->method = (uint8_t)z->method;
  e->crc = z->crc;
  e->data = 0;
  g_pool_len += len;
  return 0;
}

static int cmp_entry(const void *a, const void *b) {
  return strcmp(ename((const Entry *)a), ename((const Entry *)b));
}

static int fs_path(const char *real, char *out, size_t cap) {
  const char *p = strchr(real, ':');
  snprintf(out, cap, "%s", p ? p + 1 : real);
  return 0;
}

int ssx_assets_init(const char *apk) {
  u64 t0 = armGetSystemTick();
  snprintf(g_apk_path, sizeof g_apk_path, "%s", apk);
  if (rt_zip_walk_path(apk, collect, NULL) != 0 || g_count == 0) {
    debugPrintf("[assets] %s: no assets/ in it\n", apk);
    return -1;
  }
  qsort(g_ent, (size_t)g_count, sizeof *g_ent, cmp_entry);
  uint32_t n = 1;
  while (n < (uint32_t)g_count * 2)
    n <<= 1;
  g_hmask = n - 1;
  g_hash = calloc(n, sizeof *g_hash);
  g_hash_ci = calloc(n, sizeof *g_hash_ci);
  if (!g_hash || !g_hash_ci)
    return -1;
  for (int i = 0; i < g_count; i++) {
    uint32_t h = hash_str(ename(&g_ent[i]), 0) & g_hmask;
    while (g_hash[h])
      h = (h + 1) & g_hmask;
    g_hash[h] = i + 1;
    h = hash_str(ename(&g_ent[i]), 1) & g_hmask;
    while (g_hash_ci[h])
      h = (h + 1) & g_hmask;
    g_hash_ci[h] = i + 1;
  }
  char fp[512];
  fs_path(apk, fp, sizeof fp);
  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  if (fs && R_SUCCEEDED(fsFsOpenFile(fs, fp, FsOpenMode_Read, &g_apk)))
    g_apk_open = 1;
  else
    debugPrintf("[assets] WARNING: %s: no FsFile, reads go through stdio\n", apk);
  unsigned stored = 0;
  unsigned long long bytes = 0;
  for (int i = 0; i < g_count; i++) {
    stored += g_ent[i].method == 0;
    bytes += g_ent[i].size;
  }
  debugPrintf("[assets] %d assets (%u stored), %llu MB, indexed in %llu ms\n", g_count, stored, bytes >> 20,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  return 0;
}

static const Entry *find(const char *name) {
  if (!g_hash || !name)
    return NULL;
  while (name[0] == '/' || (name[0] == '.' && name[1] == '/'))
    name += name[0] == '/' ? 1 : 2;
  uint32_t h = hash_str(name, 0) & g_hmask;
  for (int i; (i = g_hash[h]); h = (h + 1) & g_hmask)
    if (!strcmp(ename(&g_ent[i - 1]), name))
      return &g_ent[i - 1];
  h = hash_str(name, 1) & g_hmask;
  for (int i; (i = g_hash_ci[h]); h = (h + 1) & g_hmask)
    if (!strcasecmp(ename(&g_ent[i - 1]), name)) {
      if (g_st.ci_hits++ < 8)
        debugPrintf("[assets] %s found as %s (case)\n", name, ename(&g_ent[i - 1]));
      return &g_ent[i - 1];
    }
  return NULL;
}

/* ------------------------------------------------------------ reading */
static int apk_pread(uint64_t off, void *buf, size_t n) {
  ssize_t c = dcr_apkcache_read(off, buf, n);
  if (c == (ssize_t)n)
    return 0;
  if (g_apk_open) {
    u64 got = 0;
    if (R_SUCCEEDED(fsFileRead(&g_apk, (s64)off, buf, n, FsReadOption_None, &got)) && got == n)
      return 0;
    return -1;
  }
  static Mutex m;
  mutexLock(&m);
  static FILE *f;
  if (!f)
    f = fopen(g_apk_path, "rb");
  int r = -1;
  if (f && fseeko(f, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n)
    r = 0;
  mutexUnlock(&m);
  return r;
}

static uint32_t data_offset(Entry *e) {
  uint32_t d = e->data;
  if (d)
    return d;
  uint8_t lh[30];
  if (apk_pread(e->lho, lh, sizeof lh) != 0 || lh[0] != 'P' || lh[1] != 'K' || lh[2] != 3 || lh[3] != 4)
    return 0;
  unsigned nl = lh[26] | lh[27] << 8, xl = lh[28] | lh[29] << 8;
  d = e->lho + 30 + nl + xl;
  e->data = d;
  return d;
}

struct SsxAsset {
  Entry *e;
  uint32_t data;
  uint32_t pos;     /* the reader's position in the uncompressed bytes */
  uint32_t size;    /* what the game sees: the entry's, or the edited copy's */
  uint8_t *whole;   /* deflated, inflated whole; or an edited copy (ssx_assetpatch.c) */
  /* deflated, streamed */
  mz_stream z;
  int z_up;
  uint8_t *in;
  uint32_t in_used; /* compressed bytes read so far */
  uint32_t zpos;    /* uncompressed bytes produced so far */
};

static int inflate_whole(SsxAsset *a) {
  Entry *e = a->e;
  uint8_t *src = malloc(e->comp ? e->comp : 1);
  a->whole = malloc(e->size ? e->size : 1);
  if (!src || !a->whole) {
    free(src);
    debugPrintf("[assets] out of memory inflating %s (%u -> %u)\n", ename(e), e->comp, e->size);
    return -1;
  }
  int ok = apk_pread(a->data, src, e->comp) == 0;
  if (ok) {
    mz_stream z;
    memset(&z, 0, sizeof z);
    z.next_in = src;
    z.avail_in = e->comp;
    z.next_out = a->whole;
    z.avail_out = e->size;
    ok = mz_inflateInit2(&z, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK;
    if (ok) {
      int r = mz_inflate(&z, MZ_FINISH);
      ok = (r == MZ_STREAM_END || r == MZ_OK) && z.total_out == e->size;
      /* checked against the zip's CRC (every asset inflated whole) */
      static volatile unsigned bad;
      if (ok && (uint32_t)mz_crc32(MZ_CRC32_INIT, a->whole, e->size) != e->crc && bad++ < 16)
        debugPrintf("[assets] %s: CRC MISMATCH after inflating (%u bytes)\n", ename(e), e->size);
      mz_inflateEnd(&z);
    }
  }
  free(src);
  if (!ok)
    debugPrintf("[assets] %s: inflate failed\n", ename(e));
  return ok ? 0 : -1;
}

static int stream_reset(SsxAsset *a) {
  if (a->z_up)
    mz_inflateEnd(&a->z);
  memset(&a->z, 0, sizeof a->z);
  a->z_up = mz_inflateInit2(&a->z, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK;
  a->in_used = 0;
  a->zpos = 0;
  return a->z_up ? 0 : -1;
}

/* Inflate into out (NULL: discard) until zpos reaches `upto` or n bytes. */
static int stream_produce(SsxAsset *a, uint8_t *out, uint32_t n) {
  static uint8_t scratch[16384];
  uint32_t done = 0;
  while (done < n) {
    if (a->z.avail_in == 0 && a->in_used < a->e->comp) {
      uint32_t c = a->e->comp - a->in_used;
      if (c > STREAM_IN)
        c = STREAM_IN;
      if (apk_pread((uint64_t)a->data + a->in_used, a->in, c) != 0)
        return -1;
      a->z.next_in = a->in;
      a->z.avail_in = c;
      a->in_used += c;
    }
    uint32_t want = n - done;
    uint8_t *dst = out ? out + done : scratch;
    if (!out && want > sizeof scratch)
      want = sizeof scratch;
    a->z.next_out = dst;
    a->z.avail_out = want;
    int r = mz_inflate(&a->z, MZ_NO_FLUSH);
    uint32_t got = want - a->z.avail_out;
    done += got;
    a->zpos += got;
    if (r == MZ_STREAM_END)
      break;
    if (r != MZ_OK && !(r == MZ_BUF_ERROR && got))
      return -1;
  }
  return (int)done;
}

SsxAsset *ssx_asset_open(const char *name) {
  Entry *e = (Entry *)find(name);
  g_st.opens++;
  if (!e) {
    g_st.misses++;
    return NULL;
  }
  SsxAsset *a = calloc(1, sizeof *a);
  if (!a)
    return NULL;
  a->e = e;
  a->data = data_offset(e);
  if (!a->data) {
    debugPrintf("[assets] %s: bad local header\n", name);
    free(a);
    return NULL;
  }
  if (e->method == 0) {
    g_st.stored++;
  } else if (e->method != 8) {
    debugPrintf("[assets] %s: compression method %u\n", name, e->method);
    free(a);
    return NULL;
  } else if (e->size <= SSX_ASSET_WHOLE) {
    g_st.whole++;
    if (inflate_whole(a) != 0) {
      free(a->whole);
      free(a);
      return NULL;
    }
  } else {
    g_st.streamed++;
    a->in = malloc(STREAM_IN);
    if (!a->in || stream_reset(a) != 0) {
      free(a->in);
      free(a);
      return NULL;
    }
  }
  a->size = e->size;
  /* the two text files edited in memory (ssx_assetpatch.c) */
  if (e->size <= SSX_ASSET_WHOLE && ssx_assetpatch_wants(ename(e))) {
    if (!a->whole && e->method == 0) {
      a->whole = malloc(e->size ? e->size : 1);
      if (a->whole && apk_pread(a->data, a->whole, e->size) != 0) {
        free(a->whole);
        a->whole = NULL;
      }
    }
    uint32_t n;
    uint8_t *p = a->whole ? ssx_assetpatch_apply(ename(e), a->whole, e->size, &n) : NULL;
    if (p) {
      free(a->whole);
      a->whole = p;
      a->size = n;
    }
  }
  return a;
}

int ssx_asset_read(SsxAsset *a, void *buf, size_t n) {
  if (!a)
    return -1;
  Entry *e = a->e;
  if (a->pos >= a->size || n == 0)
    return 0;
  if (n > a->size - a->pos)
    n = a->size - a->pos;
  if (a->whole) {
    memcpy(buf, a->whole + a->pos, n);
  } else if (e->method == 0) {
    if (apk_pread((uint64_t)a->data + a->pos, buf, n) != 0)
      return -1;
  } else {
    if (a->pos < a->zpos && stream_reset(a) != 0)
      return -1;
    if (a->pos > a->zpos && stream_produce(a, NULL, a->pos - a->zpos) < 0)
      return -1;
    int got = stream_produce(a, buf, (uint32_t)n);
    if (got < 0)
      return -1;
    n = (size_t)got;
  }
  a->pos += (uint32_t)n;
  g_st.bytes += n;
  return (int)n;
}

long ssx_asset_seek(SsxAsset *a, long off, int whence) {
  if (!a)
    return -1;
  long long to = whence == SEEK_SET ? off : whence == SEEK_CUR ? (long long)a->pos + off
               : whence == SEEK_END ? (long long)a->size + off : -1;
  if (to < 0 || to > (long long)a->size)
    return -1;
  a->pos = (uint32_t)to;
  return (long)to;
}

long ssx_asset_size(const SsxAsset *a) { return a ? (long)a->size : -1; }
long ssx_asset_tell(const SsxAsset *a) { return a ? (long)a->pos : -1; }

void ssx_asset_close(SsxAsset *a) {
  if (!a)
    return;
  if (a->z_up)
    mz_inflateEnd(&a->z);
  free(a->in);
  free(a->whole);
  free(a);
}

/* ------------------------------------------------------------ folders */
/* The first entry whose name is >= key. */
static int lower_bound(const char *key) {
  int lo = 0, hi = g_count;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (strcmp(ename(&g_ent[mid]), key) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

int ssx_asset_exists(const char *name) {
  if (find(name))
    return 1;
  char key[520];
  while (name[0] == '/')
    name++;
  size_t len = strlen(name);
  while (len && name[len - 1] == '/')
    len--;
  if (len == 0)
    return 2;
  snprintf(key, sizeof key, "%.*s/", (int)len, name);
  int i = lower_bound(key);
  return i < g_count && !strncmp(ename(&g_ent[i]), key, strlen(key)) ? 2 : 0;
}

long ssx_asset_size_of(const char *name) {
  const Entry *e = find(name);
  if (e && ssx_assetpatch_wants(ename(e))) { /* an edited copy's size */
    SsxAsset *a = ssx_asset_open(name);
    long n = a ? (long)a->size : (long)e->size;
    ssx_asset_close(a);
    return n;
  }
  return e ? (long)e->size : -1;
}

int ssx_asset_list(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx) {
  char key[520];
  while (dir[0] == '/')
    dir++;
  size_t len = strlen(dir);
  while (len && dir[len - 1] == '/')
    len--;
  if (len)
    snprintf(key, sizeof key, "%.*s/", (int)len, dir);
  else
    key[0] = 0;
  size_t kl = strlen(key);
  int n = 0;
  char last[256] = "";
  for (int i = lower_bound(key); i < g_count && !strncmp(ename(&g_ent[i]), key, kl); i++) {
    const char *rest = ename(&g_ent[i]) + kl;
    const char *slash = strchr(rest, '/');
    size_t cl = slash ? (size_t)(slash - rest) : strlen(rest);
    if (cl == 0 || cl >= sizeof last)
      continue;
    if (strlen(last) == cl && !strncmp(last, rest, cl))
      continue; /* the same subfolder again */
    memcpy(last, rest, cl);
    last[cl] = 0;
    cb(last, ctx);
    n++;
  }
  return n;
}

void ssx_assets_report(void) {
  debugPrintf("[assets] %lu opens (%lu missing, %lu by case), %lu stored, %lu inflated whole, %lu streamed, "
              "%lu as descriptors; %llu MB read\n",
              g_st.opens, g_st.misses, g_st.ci_hits, g_st.stored, g_st.whole, g_st.streamed, g_st.fd_opens,
              g_st.bytes >> 20);
}

/* ------------------------------------------------------------ the NDK's AAssetManager */
/* libandroid.so's AAssetManager_fromJava(env, assetManager): one manager. */
static int g_mgr;
void *b_AAssetManager_fromJava(void *env, void *jmgr) { return &g_mgr; }

void *b_AAssetManager_open(void *mgr, const char *filename, int mode) {
  SsxAsset *a = ssx_asset_open(filename);
  if (!a && g_st.misses < 64)
    debugPrintf("[assets] AAssetManager_open(%s): not in the APK\n", filename ? filename : "(null)");
  return a;
}

int b_AAsset_read(void *asset, void *buf, size_t count) { return ssx_asset_read(asset, buf, count); }

/* off_t is 32-bit in this ABI */
long b_AAsset_seek(void *asset, long offset, int whence) { return ssx_asset_seek(asset, offset, whence); }

void b_AAsset_close(void *asset) { ssx_asset_close(asset); }

int b_open(const char *path, int lflags, ...); /* bionic_io.c: a descriptor the game can read */

/* A descriptor on the APK and where the stored asset is in it. Android
 * refuses compressed assets (-1); so do we. */
int b_AAsset_openFileDescriptor(void *asset, long *out_start, long *out_length) {
  SsxAsset *a = asset;
  if (!a || a->e->method != 0) {
    if (a)
      debugPrintf("[assets] openFileDescriptor(%s): compressed, refused\n", ename(a->e));
    return -1;
  }
  int fd = b_open(g_apk_path, 0 /* O_RDONLY */);
  if (fd < 0) {
    debugPrintf("[assets] openFileDescriptor(%s): cannot open the APK\n", ename(a->e));
    return -1;
  }
  g_st.fd_opens++;
  if (out_start)
    *out_start = (long)a->data;
  if (out_length)
    *out_length = (long)a->e->size;
  return fd;
}
