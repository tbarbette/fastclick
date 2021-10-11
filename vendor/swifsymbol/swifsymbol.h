/**
 * SWiF Codec: an open-source sliding window FEC codec in C
 * https://github.com/irtf-nwcrg/swif-codec
 */

#ifndef SWIF_SYMBOL_H
#define SWIF_SYMBOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#define symbol_sub_scaled symbol_add_scaled
#define gf256_add(a, b) (a^b)
#define gf256_sub gf256_add
    
/*---------------------------------------------------------------------------*/

/**
 * @brief Take a symbol and add another symbol multiplied by a 
 *        coefficient, e.g. performs the equivalent of: p1 += coef * p2
 * @param[in,out] p1     First symbol (to which coef*p2 will be added)
 * @param[in]     coef2  Coefficient by which the second packet is multiplied
 * @param[in]     p2     Second symbol
 */
void symbol_add_scaled
(void *symbol1, uint8_t coef, const void *symbol2, uint32_t symbol_size, uint8_t *mul);

bool symbol_is_zero(void *symbol, uint32_t symbol_size);

void symbol_mul
(uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t *mul);

uint8_t gf256_mul(uint8_t a, uint8_t b, uint8_t *mul);

uint8_t gf256_mul_formula(uint8_t a, uint8_t b);

void assign_inv(uint8_t *array);

/*---------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* SWIF_SYMBOL_H */