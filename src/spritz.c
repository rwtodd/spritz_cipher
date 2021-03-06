/* 
 * A straight-forward implementation of
 * the spritz stream cipher.
 */

#include "spritz.h"
#include<stdlib.h>
#include<unistd.h>

#define N 256

struct s_spritz_state
{
  uint8_t i, j, k, z, a, w;
  uint8_t mem[N];
};

static void
reset_state (spritz_state s)
{
  s->i = s->j = s->k = s->z = s->a = 0;
  s->w = 1;
  for (int idx = 0; idx < N; ++idx)
    {
      s->mem[idx] = idx;
    }
}

/* creates memory that should be destroyed by
 * call to destroy_spritz
 */
spritz_state
create_spritz (void)
{
  spritz_state ans = malloc (sizeof (struct s_spritz_state));
  if (ans == NULL)
    return NULL;
  reset_state (ans);
  return ans;
}

void
destroy_spritz (spritz_state s)
{
  free (s);
}

static inline void
swap (uint8_t * const arr, size_t el1, size_t el2)
{
  uint8_t tmp = arr[el1];
  arr[el1] = arr[el2];
  arr[el2] = tmp;
}

/* when adding indices... need them modulus 256 */
#define smem(x)  s->mem[ (x) & 0xff ]

static void
update (spritz_state s, int times)
{
  uint8_t mi = s->i;
  uint8_t mj = s->j;
  uint8_t mk = s->k;
  const uint8_t mw = s->w;

  while (times--)
    {
      mi += mw;
      mj = mk + smem (mj + s->mem[mi]);
      mk = mi + mk + s->mem[mj];
      swap (s->mem, mi, mj);
    }

  s->i = mi;
  s->j = mj;
  s->k = mk;
}


static void
whip (spritz_state s, const int amt)
{
  update (s, amt);
  s->w += 2;
}


static void
crush (spritz_state s)
{
  for (size_t v = 0; v < (N / 2); ++v)
    {
      if (s->mem[v] > s->mem[N - 1 - v])
        swap (s->mem, v, N - 1 - v);
    }
}

static void
shuffle (spritz_state s)
{
  whip (s, N * 2);
  crush (s);
  whip (s, N * 2);
  crush (s);
  whip (s, N * 2);
  s->a = 0;
}

static inline void
absorb_nibble (spritz_state s, uint8_t x)
{
  if (s->a == N / 2)
    shuffle (s);
  swap (s->mem, s->a, (N / 2 + x));
  s->a++;
}

inline void
spritz_absorb (spritz_state s, const uint8_t b)
{
  absorb_nibble (s, b & 0x0f);
  absorb_nibble (s, b >> 4);
}

void
spritz_absorb_many (spritz_state s, const uint8_t * bytes, size_t len)
{
  const uint8_t *const end = bytes + len;
  while (bytes != end)
    {
      spritz_absorb (s, *bytes++);
    }
}

void
spritz_absorb_stop (spritz_state s)
{
  if (s->a == N / 2)
    shuffle (s);
  s->a++;
}

static uint8_t
drip_one (spritz_state s)
{
  update (s, 1);
  s->z = smem (s->j + smem (s->i + smem (s->z + s->k)));
  return s->z;
}

uint8_t
spritz_drip (spritz_state s)
{
  if (s->a > 0)
    shuffle (s);
  return drip_one (s);
}

void
spritz_drip_many (spritz_state s, uint8_t * arr, size_t len)
{
  uint8_t *const end = arr + len;
  if (s->a > 0)
    shuffle (s);
  while (arr != end)
    {
      *arr++ = drip_one (s);
    }
}

/* used for encryption/decryption */
void
spritz_xor_many (spritz_state s, uint8_t * arr, size_t len)
{
  uint8_t *const end = arr + len;
  if (s->a > 0)
    shuffle (s);
  while (arr != end)
    {
      *arr++ ^= drip_one (s);
    }
}

/* absorb_number is a helper function which absorbs the bytes
 * of a number, one at a time.  Used as part of the hashing
 * process for large hash sizes.  Note that there is no
 * practical chance of blowing the stack with this recursive
 * funcion, as any reasonable hash size is 2 bytes or less.
 */
static void
absorb_number (spritz_state s, size_t number)
{
  if (number > 255)
    {
      absorb_number (s, number >> 8);
    }
  spritz_absorb (s, (uint8_t) (number & 0xff));
}

/*
 * fills user-provided memory with hashed bytes.
 */
bool
spritz_file_hash (int fd, uint8_t * hash, size_t size)
{
  uint8_t *const buffer = malloc (BUFSIZ * sizeof (uint8_t));
  if (buffer == NULL)
    return false;

  struct s_spritz_state s;
  reset_state (&s);
  bool result = false;

  ssize_t rsz;
  while ((rsz = read (fd, buffer, BUFSIZ)) > 0)
    {
      spritz_absorb_many (&s, buffer, rsz);
    }

  if (rsz < 0)
    goto done;

  spritz_absorb_stop (&s);
  absorb_number (&s, size);
  spritz_drip_many (&s, hash, size);
  result = true;
done:
  free (buffer);
  return result;
}

/* 
 *  fills user-provided memory with hashed bytes.
 */
void
spritz_mem_hash (const uint8_t * const mem, size_t len, uint8_t * const hash,
                 size_t bytes)
{
  struct s_spritz_state s;
  reset_state (&s);

  spritz_absorb_many (&s, mem, len);
  spritz_absorb_stop (&s);
  absorb_number (&s, bytes);

  spritz_drip_many (&s, hash, bytes);
}
