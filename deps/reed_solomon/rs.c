//
// rs.c
// Reed-Solomon GF(2^8) erasure coding codec.
// Irreducible polynomial: 0x11d (x^8 + x^4 + x^3 + x^2 + 1)
// by Dylan Kress
//

#include "rs.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// GF(2^8) arithmetic with log/exp tables
//=============================================================================

#define GF_POLY 0x11d  // x^8 + x^4 + x^3 + x^2 + 1

static uint8_t gf_log[256];
static uint8_t gf_exp[512];  // doubled for convenience (mod 255 wraps)
static int     gf_tables_init = 0;

static void gf_init_tables(void)
{
    if (gf_tables_init) return;

    int x = 1;
    for (int i = 0; i < 255; i++)
    {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100)
        {
            x ^= GF_POLY;
        }
    }

    // log[0] is undefined but we set it to 0 for safety
    gf_log[0] = 0;

    // Duplicate the table for easy mod-255 access
    for (int i = 255; i < 512; i++)
    {
        gf_exp[i] = gf_exp[i - 255];
    }

    gf_tables_init = 1;
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

static inline uint8_t gf_div(uint8_t a, uint8_t b)
{
    if (b == 0) return 0;  // division by zero — should not happen
    if (a == 0) return 0;
    return gf_exp[(gf_log[a] + 255 - gf_log[b]) % 255];
}

static inline uint8_t gf_inv(uint8_t a)
{
    if (a == 0) return 0;
    return gf_exp[255 - gf_log[a]];
}

static inline uint8_t gf_pow(uint8_t base, uint8_t exp)
{
    if (exp == 0) return 1;
    if (base == 0) return 0;
    int log_result = ((int)gf_log[base] * (int)exp) % 255;
    return gf_exp[log_result];
}

//=============================================================================
// Matrix operations in GF(2^8)
//=============================================================================

// Allocate a rows x cols matrix
static uint8_t *matrix_alloc(int rows, int cols)
{
    return (uint8_t *)calloc((size_t)rows * (size_t)cols, 1);
}

// Get element at (r, c) in a rows x cols matrix
static inline uint8_t matrix_get(const uint8_t *m, int cols, int r, int c)
{
    return m[r * cols + c];
}

// Set element at (r, c)
static inline void matrix_set(uint8_t *m, int cols, int r, int c, uint8_t val)
{
    m[r * cols + c] = val;
}

// Invert a k x k matrix in-place using Gauss-Jordan elimination in GF(2^8).
// Returns 1 on success (result in inv_out), 0 if singular.
static int matrix_invert(const uint8_t *src, uint8_t *inv_out, int k)
{
    // Work on a copy augmented with identity: [src | I]
    uint8_t *aug = matrix_alloc(k, 2 * k);
    if (!aug) return 0;

    for (int r = 0; r < k; r++)
    {
        for (int c = 0; c < k; c++)
        {
            matrix_set(aug, 2 * k, r, c, matrix_get(src, k, r, c));
        }
        matrix_set(aug, 2 * k, r, k + r, 1);
    }

    // Forward elimination
    for (int col = 0; col < k; col++)
    {
        // Find pivot row
        int pivot = -1;
        for (int r = col; r < k; r++)
        {
            if (matrix_get(aug, 2 * k, r, col) != 0)
            {
                pivot = r;
                break;
            }
        }
        if (pivot < 0)
        {
            free(aug);
            return 0;  // Singular
        }

        // Swap rows if needed
        if (pivot != col)
        {
            for (int c = 0; c < 2 * k; c++)
            {
                uint8_t tmp = matrix_get(aug, 2 * k, col, c);
                matrix_set(aug, 2 * k, col, c, matrix_get(aug, 2 * k, pivot, c));
                matrix_set(aug, 2 * k, pivot, c, tmp);
            }
        }

        // Scale pivot row so pivot element = 1
        uint8_t scale = gf_inv(matrix_get(aug, 2 * k, col, col));
        for (int c = 0; c < 2 * k; c++)
        {
            matrix_set(aug, 2 * k, col, c,
                        gf_mul(matrix_get(aug, 2 * k, col, c), scale));
        }

        // Eliminate column in all other rows
        for (int r = 0; r < k; r++)
        {
            if (r == col) continue;
            uint8_t factor = matrix_get(aug, 2 * k, r, col);
            if (factor == 0) continue;
            for (int c = 0; c < 2 * k; c++)
            {
                uint8_t val = matrix_get(aug, 2 * k, r, c) ^
                              gf_mul(factor, matrix_get(aug, 2 * k, col, c));
                matrix_set(aug, 2 * k, r, c, val);
            }
        }
    }

    // Extract inverse from right half
    for (int r = 0; r < k; r++)
    {
        for (int c = 0; c < k; c++)
        {
            matrix_set(inv_out, k, r, c, matrix_get(aug, 2 * k, r, k + c));
        }
    }

    free(aug);
    return 1;
}

//=============================================================================
// Vandermonde encode matrix construction
//=============================================================================

// Build a systematic n x k encode matrix:
//   Top k rows = identity (data passes through unchanged)
//   Bottom m rows = parity coefficients from Vandermonde
//
// Method: Build Vandermonde V[i][j] = i^j for i=0..n-1, j=0..k-1,
// then multiply by inverse of top k x k submatrix to get systematic form.
static uint8_t *build_encode_matrix(uint8_t k, uint8_t m)
{
    int n = (int)k + (int)m;

    // Build raw Vandermonde: V[i][j] = i^j
    uint8_t *vand = matrix_alloc(n, k);
    if (!vand) return NULL;

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < k; j++)
        {
            matrix_set(vand, k, i, j, gf_pow((uint8_t)i, (uint8_t)j));
        }
    }

    // Extract top k x k submatrix and invert it
    uint8_t *top = matrix_alloc(k, k);
    uint8_t *top_inv = matrix_alloc(k, k);
    if (!top || !top_inv)
    {
        free(vand);
        free(top);
        free(top_inv);
        return NULL;
    }

    for (int r = 0; r < k; r++)
    {
        for (int c = 0; c < k; c++)
        {
            matrix_set(top, k, r, c, matrix_get(vand, k, r, c));
        }
    }

    if (!matrix_invert(top, top_inv, k))
    {
        free(vand);
        free(top);
        free(top_inv);
        return NULL;
    }

    // Multiply: encode = vand * top_inv
    // This makes the top k rows become identity
    uint8_t *encode = matrix_alloc(n, k);
    if (!encode)
    {
        free(vand);
        free(top);
        free(top_inv);
        return NULL;
    }

    for (int r = 0; r < n; r++)
    {
        for (int c = 0; c < k; c++)
        {
            uint8_t val = 0;
            for (int i = 0; i < k; i++)
            {
                val ^= gf_mul(matrix_get(vand, k, r, i),
                               matrix_get(top_inv, k, i, c));
            }
            matrix_set(encode, k, r, c, val);
        }
    }

    free(vand);
    free(top);
    free(top_inv);
    return encode;
}

//=============================================================================
// Public API
//=============================================================================

RS_CODEC *RS_Create(uint8_t k, uint8_t m)
{
    if (k == 0 || m == 0) return NULL;
    if ((int)k + (int)m > 255) return NULL;

    gf_init_tables();

    RS_CODEC *codec = (RS_CODEC *)calloc(1, sizeof(RS_CODEC));
    if (!codec) return NULL;

    codec->k = k;
    codec->m = m;
    codec->n = k + m;

    codec->encode_matrix = build_encode_matrix(k, m);
    if (!codec->encode_matrix)
    {
        free(codec);
        return NULL;
    }

    return codec;
}

void RS_Destroy(RS_CODEC *codec)
{
    if (!codec) return;
    free(codec->encode_matrix);
    free(codec);
}

int RS_Encode(const RS_CODEC *codec, const uint8_t *const *data_shards,
              uint8_t **parity_out, size_t shard_size)
{
    if (!codec || !data_shards || !parity_out || shard_size == 0) return 0;

    uint8_t k = codec->k;
    uint8_t m = codec->m;

    // For each parity shard p, compute:
    //   parity[p][byte] = XOR-sum over j of encode_matrix[(k+p)*k + j] * data[j][byte]
    for (uint8_t p = 0; p < m; p++)
    {
        if (!parity_out[p]) return 0;
        memset(parity_out[p], 0, shard_size);

        for (uint8_t j = 0; j < k; j++)
        {
            uint8_t coeff = matrix_get(codec->encode_matrix, k, k + p, j);
            if (coeff == 0) continue;

            const uint8_t *src = data_shards[j];
            uint8_t *dst = parity_out[p];

            if (coeff == 1)
            {
                // XOR directly (common case for systematic codes)
                for (size_t b = 0; b < shard_size; b++)
                {
                    dst[b] ^= src[b];
                }
            }
            else
            {
                for (size_t b = 0; b < shard_size; b++)
                {
                    dst[b] ^= gf_mul(coeff, src[b]);
                }
            }
        }
    }

    return 1;
}

int RS_Decode(const RS_CODEC *codec, uint8_t **shards, const uint8_t *present,
              size_t shard_size)
{
    if (!codec || !shards || !present || shard_size == 0) return 0;

    uint8_t k = codec->k;
    uint8_t n = codec->n;

    // Count missing shards
    int missing_count = 0;
    for (uint8_t i = 0; i < n; i++)
    {
        if (!present[i]) missing_count++;
    }

    if (missing_count == 0) return 1;  // Nothing to do
    if (missing_count > codec->m) return 0;  // Too many missing

    // Select k present shards and build the corresponding k x k submatrix
    // from the encode matrix
    uint8_t *sub_indices = (uint8_t *)malloc(k);
    uint8_t *sub_matrix = matrix_alloc(k, k);
    uint8_t *sub_inv = matrix_alloc(k, k);

    if (!sub_indices || !sub_matrix || !sub_inv)
    {
        free(sub_indices);
        free(sub_matrix);
        free(sub_inv);
        return 0;
    }

    int found = 0;
    for (uint8_t i = 0; i < n && found < k; i++)
    {
        if (present[i])
        {
            sub_indices[found] = i;
            // Copy the i-th row of encode_matrix into sub_matrix
            for (uint8_t c = 0; c < k; c++)
            {
                matrix_set(sub_matrix, k, found, c,
                            matrix_get(codec->encode_matrix, k, i, c));
            }
            found++;
        }
    }

    if (found < k)
    {
        free(sub_indices);
        free(sub_matrix);
        free(sub_inv);
        return 0;
    }

    // Invert the submatrix
    if (!matrix_invert(sub_matrix, sub_inv, k))
    {
        free(sub_indices);
        free(sub_matrix);
        free(sub_inv);
        return 0;
    }

    // Gather available shard data into a temporary array
    uint8_t **avail = (uint8_t **)malloc(k * sizeof(uint8_t *));
    if (!avail)
    {
        free(sub_indices);
        free(sub_matrix);
        free(sub_inv);
        return 0;
    }

    for (int i = 0; i < k; i++)
    {
        avail[i] = shards[sub_indices[i]];
    }

    // Reconstruct missing data shards (indices 0..k-1)
    for (uint8_t d = 0; d < k; d++)
    {
        if (present[d]) continue;  // Already have it

        // Ensure buffer is allocated
        if (!shards[d])
        {
            free(avail);
            free(sub_indices);
            free(sub_matrix);
            free(sub_inv);
            return 0;
        }

        // Reconstruct: data[d] = sum over j of sub_inv[d][j] * avail[j]
        memset(shards[d], 0, shard_size);
        for (uint8_t j = 0; j < k; j++)
        {
            uint8_t coeff = matrix_get(sub_inv, k, d, j);
            if (coeff == 0) continue;

            if (coeff == 1)
            {
                for (size_t b = 0; b < shard_size; b++)
                {
                    shards[d][b] ^= avail[j][b];
                }
            }
            else
            {
                for (size_t b = 0; b < shard_size; b++)
                {
                    shards[d][b] ^= gf_mul(coeff, avail[j][b]);
                }
            }
        }
    }

    // Reconstruct missing parity shards by re-encoding
    for (uint8_t p = 0; p < codec->m; p++)
    {
        uint8_t idx = k + p;
        if (present[idx]) continue;

        if (!shards[idx])
        {
            free(avail);
            free(sub_indices);
            free(sub_matrix);
            free(sub_inv);
            return 0;
        }

        // Re-encode this parity shard from the (now complete) data shards
        memset(shards[idx], 0, shard_size);
        for (uint8_t j = 0; j < k; j++)
        {
            uint8_t coeff = matrix_get(codec->encode_matrix, k, idx, j);
            if (coeff == 0) continue;

            if (coeff == 1)
            {
                for (size_t b = 0; b < shard_size; b++)
                {
                    shards[idx][b] ^= shards[j][b];
                }
            }
            else
            {
                for (size_t b = 0; b < shard_size; b++)
                {
                    shards[idx][b] ^= gf_mul(coeff, shards[j][b]);
                }
            }
        }
    }

    free(avail);
    free(sub_indices);
    free(sub_matrix);
    free(sub_inv);
    return 1;
}
