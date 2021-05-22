#include <stdio.h>
#include <stdlib.h>

#include "meshhash2.h"

/* Define for little speedup on 64-bit-systems (Intel/AMD) with gcc. */
/*
#define GCC64INLINE_ASSEMBLER
*/

#ifdef GCC64INLINE_ASSEMBLER
static Word ROT(Word w, int b)
{
	__asm__ ("rorq %%cl, %0"			
			: "=r" (w)		
			: "0" (w), "c" (b)	
			: "cc");
	return w;
}
#define ROT37(w) 			\
	__asm__ ("rorq $37, %0"		\
			: "=r" (w)	\
			: "0" (w)	\
			: "cc")
#else
#define ROT(w,b) ((b) == 0 ? (w) : ((((w) & 0xffffffffffffffffULL) >> (b) ^ (w) << (64 - (b))) & 0xffffffffffffffffULL))
#define ROT37(w) w = (((w) & 0xffffffffffffffffULL) >> 37 ^ (w) << 27) & 0xffffffffffffffffULL
#endif


static Word sbox(Word w)
{
	w = w * 0x9e3779b97f4a7bb9ULL + 0x5e2d58d8b3bcdef7ULL;
	ROT37(w);
	w = w * 0x9e3779b97f4a7bb9ULL + 0x5e2d58d8b3bcdef7ULL;
	ROT37(w);
	return w;
}

static void add_to_counter(Word counter[], DataLength to_add)
{
	Word temp;
	int i, carry;
	
	carry = 0;
	for (i = 0; i < COUNTER_LENGTH; i++)
	{
		temp = (to_add & 0xffffffffffffffffULL) + carry & 0xffffffffffffffffULL;
		counter[i] += temp;
		counter[i] &= 0xffffffffffffffffULL;
		if (carry == 1 && temp == 0)
			carry = 1;
		else if (counter[i] < temp)
			carry = 1;
		else
			carry = 0;
		to_add >>= 32; /* should be ">>= 64", but is undefined for 64-bit long long */
		to_add >>= 32; /* is necessary for long long with more than 64 bits */
	}
	return;
}

static void normal_round(hashState *state, Word data)
{
	int i;
	
	/* for simplicity */
	Word *pipe;
	pipe = state->pipe;

	/* save current state of pipe[0] */
	pipe[state->number_of_pipes] = pipe[0];

	/* process every pipe */
	for (i = 0; i < state->number_of_pipes; i++)
	{
		pipe[i] = pipe[i] ^ i*0x0101010101010101ULL ^ data;
		pipe[i]	= ROT(pipe[i], i*37 & 63);
		pipe[i]	= sbox(pipe[i]);
		pipe[i] = pipe[i] + pipe[i+1] & 0xffffffffffffffffULL;
	}
	
	/* save feedback */
	state->feedback[state->block_counter[0] & 1ULL][state->block_round_counter] = pipe[state->block_round_counter];

	/* update block_round_counter */
	state->block_round_counter++;
	return;
}

static void final_block_round(hashState *state)
{
	int i,j;
	
	/* for simplicity */
	int number_of_pipes;
	Word *pipe;
	pipe = state->pipe;
	number_of_pipes = state->number_of_pipes;

	/* reset block_round_counter for next block */
	state->block_round_counter = 0;
	
	/* process block_counter */
	for (i = 0; i < number_of_pipes; i++)
		pipe[i] = sbox(pipe[i] ^ state->block_counter[i % COUNTER_LENGTH]);
	add_to_counter(state->block_counter, 1);

	/* process key */
	if (state->key_length > 0)
	{
		for (i = state->key_counter; i < state->key_length + state->key_counter; i += number_of_pipes)
			for (j = 0; j < number_of_pipes; j++)
				pipe[j] = sbox(pipe[j] ^ state->key[(i + j) % state->key_length]);
		
		state->key_counter = (state->key_counter + 1) % state->key_length;
		
		/* process key_length */
		for (i = 0; i < number_of_pipes; i++)
			state->pipe[i] = sbox(state->pipe[i] ^ (Word)state->key_length ^ i*0x0101010101010101ULL);
	}

	/* process feedback */
	for (i = 0; i < number_of_pipes; i++)
	{
		pipe[i] = sbox(pipe[i] ^ state->feedback[ state->block_counter[0] & 1ULL   ][i]);	/* feedback of previous block */
		pipe[i] = sbox(pipe[i] ^ state->feedback[(state->block_counter[0] & 1ULL)^1][i]);	/* feedback of actual block */
	}


	return;
}

HashReturn Update(hashState *state, const BitSequence *data, DataLength databitlen)
{
	const BitSequence *end;

	/* data has to be an array of bytes */

	/* update bit_counter */
	add_to_counter(state->bit_counter, databitlen);

	/* process given data stream */
	while (databitlen > 0)
	{
		/* process as many words as possible */
		if (state->data_counter == 0)
		{
			/* compute the address of the last whole word */
			end = data + (databitlen / 8 & ~(DataLength)7);
			databitlen -= databitlen & ~(DataLength)63;
			while(data != end)
			{
				normal_round(state, (Word)*data << 56 
							^ (Word)*(data+1) << 48
							^ (Word)*(data+2) << 40
							^ (Word)*(data+3) << 32
							^ (Word)*(data+4) << 24
							^ (Word)*(data+5) << 16
							^ (Word)*(data+6) << 8
							^ (Word)*(data+7));
				if (state->block_round_counter == state->number_of_pipes)
					final_block_round(state);
				data += 8;
			}
		}
		
		/* process bits and bytes that can not be processed as part of a word above */
		if (databitlen >= 8)
		{
			/* store a byte in data_buffer */
			state->data_buffer <<= 8;
			state->data_buffer |= *data;
			data++;
			databitlen -= 8;
			state->data_counter++;
		}
		else if (databitlen > 0)
		{
			/* process the last bits of a message */
			state->data_buffer <<= 8;
			state->data_buffer |= *data >> 8-databitlen << 8-databitlen & 0xff;
			databitlen = 0;
			state->data_counter++;
		}
		
		/* if a whole word is in data_buffer, process it */
		if (state->data_counter == 8)
		{
			normal_round(state, state->data_buffer);
			if (state->block_round_counter == state->number_of_pipes)
				final_block_round(state);
			state->data_counter = 0;
			state->data_buffer = 0;
		}
	}

	return SUCCESS;
}

static void final_rounds(hashState *state)
{
	int i,j;
	
	/* empty data_buffer */
	if (state->data_counter > 0)
	{
		state->data_buffer <<= 8 * (8 - state->data_counter);
		normal_round(state, state->data_buffer);
		state->data_counter = 0;
		state->data_buffer = 0;
	}

	/* finish actual block */
	if (state->block_round_counter != 0)
	{
		while (state->block_round_counter < state->number_of_pipes)
			normal_round(state, 0);
		final_block_round(state);
	}

	/* one extra block for feedback */
	for (i = 0; i < state->number_of_pipes; i++)
		normal_round(state, 0);
	final_block_round(state);


	/* process bit_counter */
	for (i = 0; i < COUNTER_LENGTH; i++)
		for (j = 0; j < state->number_of_pipes; j++)
			state->pipe[j] = sbox(state->pipe[j] ^ state->bit_counter[i] ^ j*0x0101010101010101ULL);
	
	/* process hashbitlen */
	for (i = 0; i < state->number_of_pipes; i++)
		state->pipe[i] = sbox(state->pipe[i] ^ (Word)state->hashbitlen ^ i*0x0101010101010101ULL);
	
	/* set sponge-mode to squeezing */
	state->squeezing = 1;
}

HashReturn SqueezeNBytes(hashState *state, BitSequence *hashval, int rounds)
{
	int i,j;
	Word temp;

	/* do the final rounds, if not already happened */
	if (!state->squeezing)
		final_rounds(state);

	/* process as many rounds as required */
	for (i = 0; i < rounds; i++)
	{
		normal_round(state, 0);
		
		/* compute a byte of the hashvalue from this round */
		temp = 0;
		for (j = 0; j < state->number_of_pipes; j += 2)
			temp ^= state->pipe[j];
		hashval[i] = temp & 0xff;

		/* if we are at the end of a block do final_block_round */
		if (state->block_round_counter == state->number_of_pipes)
			final_block_round(state);
	}

	return SUCCESS;
}

void Deinit(hashState *state)
{
	free(state->pipe);
	free(state->feedback[0]);
	free(state->feedback[1]);
	if (state->key_length > 0)
		free(state->key);
}

HashReturn Final(hashState *state, BitSequence *hashval)
{
	HashReturn error;

	error = SqueezeNBytes(state, hashval, state->hashbitlen / 8);
	Deinit(state);
	return error;
}

HashReturn Hash2(int hashbitlen, const BitSequence *data, DataLength databitlen, BitSequence *hashval, int keybytelen, ByteSequence *key)
{
	HashReturn error;
	hashState state;

	error = Init2(&state, hashbitlen, keybytelen, key);
	if (error != SUCCESS)
		return error;

	error = Update(&state, data, databitlen);
	if (error != SUCCESS)
		return error;

	error = Final(&state, hashval);
	if (error != SUCCESS)
		return error;

	return SUCCESS;
}

HashReturn Hash(int hashbitlen, const BitSequence *data, DataLength databitlen, BitSequence *hashval)
{
	HashReturn error;
	hashState state;

	error = Init(&state, hashbitlen);
	if (error != SUCCESS)
		return error;

	error = Update(&state, data, databitlen);
	if (error != SUCCESS)
		return error;

	error = Final(&state, hashval);
	if (error != SUCCESS)
		return error;

	return SUCCESS;
}

HashReturn Init3(hashState *state, int hashbitlen, int number_of_pipes, int keybytelen, ByteSequence *key)
{
	int i;
	HashReturn error;

	/* check hashbitlen */
	if (hashbitlen % 8 != 0 || hashbitlen < 0 || hashbitlen > 0x7fff)
		return BAD_HASHBITLEN;
	
	/* set state-values hashbitlen and numbers_of_pipes */
	state->hashbitlen = hashbitlen;
	state->number_of_pipes = number_of_pipes;

	/* check keybytelen and init key */
	if (keybytelen % 8 != 0 || keybytelen < 0 || keybytelen > 0x7fff)
		return BAD_KEY_LENGTH;
	state->key_length = keybytelen/8;
	if (state->key_length > 0)
	{
		state->key = calloc(state->key_length, sizeof(Word));
		if (state->key == NULL)
			return MEMORY_ALLOCATION_FAILED;
		for (i = 0; i < state->key_length; i++)
			state->key[i] = (Word)(key[8*i] & 0xff) << 56
					^ (Word)(key[8*i+1] & 0xff) << 48
					^ (Word)(key[8*i+2] & 0xff) << 40
					^ (Word)(key[8*i+3] & 0xff) << 32
					^ (Word)(key[8*i+4] & 0xff) << 24
					^ (Word)(key[8*i+5] & 0xff) << 16
					^ (Word)(key[8*i+6] & 0xff) << 8
					^ (Word)(key[8*i+7] & 0xff);
	}
	else
		state->key = NULL;

	/* set some state-values */
	state->block_round_counter = 0;
	state->key_counter = 0;
	state->data_counter = 0;
	state->data_buffer = 0;
	state->squeezing = 0;

	/* init pipe */
	state->pipe = calloc(number_of_pipes + 1, sizeof(Word));
	if (state->pipe == NULL)
		return MEMORY_ALLOCATION_FAILED;

	/* init feedback */
	state->feedback[0] = calloc(number_of_pipes, sizeof(Word));
	if (state->feedback[0] == NULL)
		return MEMORY_ALLOCATION_FAILED;
	state->feedback[1] = calloc(number_of_pipes, sizeof(Word));
	if (state->feedback[1] == NULL)
		return MEMORY_ALLOCATION_FAILED;

	/* init bit_counter and block_counter */
	for (i = 0; i < COUNTER_LENGTH; i++)
	{
		state->bit_counter[i] = 0;
		state->block_counter[i] = 0;
	}

	/* if no key is used then we are ready */
	error = SUCCESS;
	if (state->key_length > 0)
		error = Update(state, key, 8*keybytelen);
	
	/* if have processed a key, we have to reset bit_counter again */
	for (i = 0; i < COUNTER_LENGTH; i++)
	{
		state->bit_counter[i] = 0;
	}

	return error;
}

int ComputeNumberOfPipes(int hashbitlen)
{
	int number_of_pipes;

	number_of_pipes = (hashbitlen + 63) / 64;
	number_of_pipes += (NUMBER_OF_EXTRA_PIPES);
	if (number_of_pipes < MIN_NUMBER_OF_PIPES)
		number_of_pipes = MIN_NUMBER_OF_PIPES;
	if (number_of_pipes > MAX_NUMBER_OF_PIPES)
		number_of_pipes = MAX_NUMBER_OF_PIPES;
	return number_of_pipes;
}

HashReturn Init2(hashState *state, int hashbitlen, int keybytelen, ByteSequence *key)
{
	return Init3(state, hashbitlen, ComputeNumberOfPipes(hashbitlen), keybytelen, key);
}

HashReturn Init(hashState *state, int hashbitlen)
{
	return Init3(state, hashbitlen, ComputeNumberOfPipes(hashbitlen), 0, NULL);
}

HashReturn InitSponge2(hashState *state, int number_of_pipes, int keybytelen, ByteSequence *key)
{
	return Init3(state, 0, number_of_pipes, keybytelen, key);
}

HashReturn InitSponge(hashState *state, int number_of_pipes)
{
	return Init3(state, 0, number_of_pipes, 0, NULL);
}



