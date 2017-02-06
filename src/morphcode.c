#include <stdlib.h>
#include <assert.h>

#include "morphcode.h"
#include "morphpoly.h"
#include "morphdebug.h"


morph_cipher_t* morph_cipher_new(int size)
{
  morph_cipher_t* new_cipher = malloc(sizeof(morph_cipher_t));
  new_cipher->poly_array = malloc(sizeof(morph_poly_t*) * size);
  new_cipher->size = size;

  for (int i = 0; i < size; ++i) new_cipher->poly_array[i] = NULL;

  return new_cipher;
}

void  morph_cipher_realloc(morph_cipher_t* cipher, int size)
{
  if (size <= cipher->size) return;
  cipher->poly_array = realloc(cipher->poly_array, sizeof(morph_poly_t*) * size);

  for (int i = cipher->size; i < size; ++i) cipher->poly_array[i] = NULL;
  cipher->size = size;

}

int morph_encode_u32(morph_state_t* state, morph_poly_t* coded_message, uint32_t message)
{
  morph_poly_realloc_coeff_array(coded_message, 32);

  for (int i = 0; i < 32; ++i) {
    morph_poly_set_coeff_ui(coded_message, i, (message >> i) % 2);
  }
  
  return 0;
}

uint32_t morph_decode_u32(morph_state_t* state, morph_poly_t* coded_message)
{
  // FIXME: check that coeff above 32 are equal to zero
  int degree = coded_message->degree < 32 ? coded_message->degree : 32;
  uint32_t value = 0;
  for (int i = 0; i < degree; ++i)
  {
    uint32_t local_value = coded_message->coeff_array[i];//mpz_get_ui(poly->coeff_array[i]);
    assert(local_value <= 1 || local_value >= -1);
    uint32_t bit_value = (local_value != 0 ? 1 : 0);
    value |= (bit_value << i);
  }

  return value;
}

int morph_encrypt(morph_secret_t* secret, morph_cipher_t* ciphertext, morph_poly_t* plaintext) 
{
  morph_cipher_realloc(ciphertext, 2);

  int n = secret->state->n;

  morph_poly_t* public_a = morph_poly_new(secret->state, n);
  morph_poly_sample(secret->distrib_a, public_a, n);

  morph_poly_t* secret_e = morph_poly_new(secret->state, n);
  morph_poly_sample(secret->distrib_e, secret_e, n);
  morph_poly_scale_ui(secret_e, secret_e, 2);


  morph_poly_t*  c1 = morph_poly_new(secret->state, n); 

  morph_poly_mult(c1, secret->secret_s, public_a);

  morph_poly_add(c1, c1, secret_e);

  morph_poly_add(c1, c1, plaintext);

  morph_poly_free(secret_e);

  morph_poly_neg(public_a, public_a);

  ciphertext->poly_array[0] = c1;
  ciphertext->poly_array[1] = public_a;

  return 0;
}

int morph_decrypt(morph_secret_t* secret, morph_poly_t* plaintext, morph_cipher_t* ciphertext) 
{
  morph_poly_t* pow_s = morph_poly_new(secret->state, secret->state->n);
  morph_poly_t* mult_s_c = morph_poly_new(secret->state, secret->state->n);
  morph_poly_set_coeff_ui(pow_s, 0, 1);

  assert(ciphertext->size >= 1);

  // zero-ing plaintext
  for (int i = 0; i < plaintext->degree; ++i) morph_poly_set_coeff_ui(plaintext, i, 0);

  for (int i = 0; i < ciphertext->size; ++i) {
    morph_poly_mult(mult_s_c, pow_s, ciphertext->poly_array[i]);
    morph_poly_add(plaintext, mult_s_c, plaintext);
    morph_poly_mult(mult_s_c, pow_s, secret->secret_s);
    morph_poly_mod(plaintext, plaintext, plaintext->state->poly_mod); 
    morph_poly_t* exch = pow_s;
    pow_s = mult_s_c;
    mult_s_c = exch;
  }

  morph_poly_coeffs_mod_ui(plaintext, 2);

  morph_poly_free(pow_s);
  morph_poly_free(mult_s_c);

  return 0;
}

int morph_homomorphic_add(morph_state_t* state, morph_cipher_t* result, morph_cipher_t* op0, morph_cipher_t* op1)
{
  int max_size = op0->size > op1->size ? op0->size : op1->size;

  morph_cipher_realloc(result, max_size);
  for (int i = 0; i < max_size; ++i) {
    if (!result->poly_array[i]) result->poly_array[i] = morph_poly_new(state, state->n);

    if (i >= op0->size) morph_poly_copy(result->poly_array[i], op1->poly_array[i]);
    else if (i >= op1->size) morph_poly_copy(result->poly_array[i], op0->poly_array[i]);
    else morph_poly_add(result->poly_array[i], op0->poly_array[i], op1->poly_array[i]);
  }

  return 0;
}

int morph_homomorphic_mult(morph_state_t* state, morph_cipher_t* result, morph_cipher_t* op0, morph_cipher_t* op1)
{
  assert(result != op0 && result != op1 && "inplace not supported in cipher mult");

  int result_size = op0->size + op1->size - 1;

  morph_cipher_realloc(result, result_size);
  for (int i = 0; i < result_size; ++i) {
    if (result->poly_array[i]) morph_poly_reset(result->poly_array[i]);
    else result->poly_array[i] = morph_poly_new(state, state->n);
  }

  // temporary multiplication result
  morph_poly_t* tmp = morph_poly_new(state, state->n);

  for (int i = 0; i < op0->size; ++i) {
    for (int j = 0; j < op1->size; ++j) {
      morph_poly_mult(tmp, op0->poly_array[i], op1->poly_array[j]);
      morph_poly_add(result->poly_array[i+j], result->poly_array[i+j], tmp);
    }
  }

  return 0;
}

#if 0
int morph_homomorphic_recrypt(morph_state_t* state, morph_cipher_t* result, morph_cipher_t* op)
{

}

int morph_generate_recrypt(morph_recrypt_pk_t* pk, morph_recrypt_sk_t* sk, morph_secret_t* secret, int m, int l, int D)
{
  pk->m = m;
  pk->D = D;
  pk->z_array = malloc(sizeof(morph_cipher_t*) * m);

  sk->l = l;
  sk->sparse_index_array = malloc(sizeof(int) * l);
  morph_poly_t** s_pow_array = malloc(sizeof(morph_poly_t*) * (D+1));

  /** Computing the power of the secret polynomial */ 
  s_pow_array[0] = morph_poly_new(state, 1);
  morph_poly_set_coeff_ui(s_pow_array[0], 0, 1);

  for (int i = 1; i < l; ++i) {
    s_pow_array[i] = morph_poly_new(state, state->n);
    morph_poly_mult(s_pow_array[i], s_pow_array[i-1], secret->secret_s);
  }

  /** hiding the power within the sparse subset */
  sk->sparse_index_array[0] = rand() % (m / l);
  for (int i = 1; i < l; ++i) {
    int new_index = i * (m / l) + rand() % (m / l);
    assert(new_index != sk->sparse_index_array[i-1] && new_index < m);
    sk->sparse_index_array[i] = new_index;
  }
  for (int i = 0; i < m; ++i)
  {
    pk->z_array[i] = morph_cipher_new(D+1);
    for (int j = 0; j < D+1; ++j) {
      pk->z_array[i]->poly_array[j] = morph_poly_new(secret->state, secret->state->n);
      morph_poly_sample(secret->distrib_e, pk->z_array[i]->poly_array[j], secret->state->n);
    }
  }
  // last vector to fix the sum
  for (int i = 0; i < l - 1; ++i) {
    int z_index = sk->sparse_index_array[i];
    for (int j = 0; j < D+1; ++j) {
      morph_poly_sub(s_pow_array[j], s_pow_array[j], pk->z_array[z_index]->poly_array[j]);
    }
  }
  int z_index = sk->sparse_index_array[l-1];
  for (int j = 0; j < D+1; ++j) {
    morph_poly_copy(pk->z_array[z_index]->poly_array[j], s_pow_array[j]);
  }

  // FIXME free tmp polynomials
  for (int i = 0; i < l; ++i) morph_poly_free(s_pow_array[i]);
}
#endif

void morph_cipher_display(char* title, morph_cipher_t* cipher, char* footer)
{
  printf("%s", title);
  for (int i = 0; i < cipher->size; ++i) {
    printf("cipher-poly[%d]:\n", i);
    morph_poly_display("", cipher->poly_array[i], "\n");
  }
  printf("%s", footer);
}

/*
void morph_cipher_switch_modulo(morph_cipher_t* result, morph_cipher_t* op, int new_modulo)
{
  morph_cipher_realloc(result, op->size);
  for (int i = 0; i < op->size; ++i) morph_poly_switch_modulo(result->poly_array[i], op->poly_array[i], new_modulo);
}*/
