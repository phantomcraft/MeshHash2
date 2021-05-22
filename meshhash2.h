/* Usage as
 *
 * Hash: Init, Update*, Final
 * 	 or only Hash
 *
 * Hash with key: Init2, Update*, Final
 * 		  or only Hash2
 *
 * Sponge: InitSponge, Update*, SqueezeNBytes*, Deinit
 *
 * Sponge with key: InitSponge2, Update*, SqueezeNBytes*, Deinit
 *
 * PRF / HMAC: same as Hash with key
 *
 * PRG / Key derivation: same as Sponge with key
 *
 * For testing: Init3, Update*, SqueezeNBytes*, Deinit
 *
 * the "*" means: Call as often as needed.
 */


#define MIN_NUMBER_OF_PIPES 4
/* should be >= 4 */
#define MAX_NUMBER_OF_PIPES 256
/* must be <= 256 */
#define NUMBER_OF_EXTRA_PIPES 1
/* for extra security, can be even negative, but should not be to large, should only be changed if flaws in design are detected,
 * can also be an expression depending on number_of_pipes */

#define COUNTER_LENGTH 4
/* must be of type int, positive, and <= MIN_NUMBER_OF_PIPES */

typedef unsigned char BitSequence;
typedef BitSequence ByteSequence;	/* for the given key */

typedef unsigned long long DataLength;	/* should be a typical 64-bit value (long long) */

typedef unsigned long long Word;	/* 64-bit word (or longer) */

typedef enum {
	SUCCESS = 0,
	FAIL = 1,
	BAD_HASHBITLEN = 2,
	BAD_KEY_LENGTH = 3,
	MEMORY_ALLOCATION_FAILED = 4
} HashReturn;

typedef struct {
     int hashbitlen,		/* length of hash value in bits */
	 number_of_pipes,	
	 key_length,		/* length in words */
	 block_round_counter,	/* actual round in block */
	 key_counter,		/* actual offset in key */
	 data_counter,		/* counter for buffering data */
	 squeezing;		/* indicating if the whole message is processed and the computing of the hash value has started */
     Word *pipe,
	  *key,
	  *feedback[2],
	  data_buffer,				/* buffer if message is not given whole words */
	  bit_counter[COUNTER_LENGTH],		/* counter for message bits */
	  block_counter[COUNTER_LENGTH];	/* counter for processed blocks */
     } hashState;


HashReturn Init3(hashState *state, int hashbitlen, int number_of_pipes, int keybytelen, ByteSequence *key);

HashReturn Init2(hashState *state, int hashbitlen, int keybytelen, ByteSequence *key);

HashReturn Init(hashState *state, int hashbitlen);

HashReturn InitSponge2(hashState *state, int number_of_pipes, int keybytelen, ByteSequence *key);

HashReturn InitSponge(hashState *state, int number_of_pipes);

HashReturn Update(hashState *state, const BitSequence *data, DataLength databitlen);

HashReturn SqueezeNBytes(hashState *state, BitSequence *hashval, int rounds);

HashReturn Final(hashState *state, BitSequence *hashval);

void Deinit(hashState *state);

HashReturn Hash(int hashbitlen, const BitSequence *data, DataLength databitlen, BitSequence *hashval);

HashReturn Hash2(int hashbitlen, const BitSequence *data, DataLength databitlen, BitSequence *hashval, int keybytelen, ByteSequence *key);

int ComputeNumberOfPipes(int hashbitlen);	/* computes number of pipes needed for given hash length or key length in bits */


