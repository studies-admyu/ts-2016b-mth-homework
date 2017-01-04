#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include <cuda.h>
#include <cuda_runtime_api.h>

/* Number of function values to calculate */
#define RANGE_ELEMENTS_COUNT 20000000
/* Maximum possible CUDA blocks */
#define CUDA_BLOCKS_MAX 65535

/** Calculates function values by using GPU (on device)
  *		@param output - output buffer for calculated values;
  *		@param elements_per_thread - elements to be calculated by one GPU thread.
  */
__global__ void device_sin(double* output, unsigned int elements_per_thread)
{
	/* Pi contant */
	const double const_2pi = 8.0 * atan(1.0);
	/* Cycle counter */
	unsigned int i;

	/* Determine indicies of values to be calculated */
	unsigned int thread_start = (blockIdx.x * blockDim.x + threadIdx.x) * elements_per_thread;
	unsigned int thread_end = thread_start + elements_per_thread;
	
	/* Check whenever the thread is not redundant */
	if (thread_start >= RANGE_ELEMENTS_COUNT) {
		return;
	}
	/* Fix indicies excess if any */
	thread_end = (thread_end <= RANGE_ELEMENTS_COUNT)? thread_end: RANGE_ELEMENTS_COUNT;

	/* Calculate values */
	for (i = thread_start; i < thread_end; ++i) {
		output[i] = sin((const_2pi * i) / (RANGE_ELEMENTS_COUNT - 1));
	}
}

/** Calculates function values by using CPU (on host)
  *		@param output - output buffer for calculated values.
  */
void host_sin(double* output)
{
	/* Pi contant */
	const double const_2pi = 8.0 * atan(1.0);
	/* Cycle counter */
	unsigned int i;

	/* Calculate values */
	for (i = 0; i < RANGE_ELEMENTS_COUNT; ++i) {
		output[i] = sin((const_2pi * i) / (RANGE_ELEMENTS_COUNT - 1));
	}
}

int main(int argc, char* argv[])
{
	/* Whenever to use CUDA flag */
	int use_cuda = 0;
	/* Properly calculated values flag */
	int are_counted_properly = 1;
	/* Cycle counter */
	unsigned int i;

	/* CUDA threads per blocks (user defined) */
	unsigned int cuda_threads_per_block = 1;
	/* CUDA blocks count (to be calculated) */
	unsigned int cuda_blocks_count;
	/* Elements to be calculated per each CUDA thread (to be calculated) */
	unsigned int cuda_elements_per_thread = 0;

	/* Buffer for calculated function values on host (will be calculated by CPU, or copied from GPU memory) */
	double* host_function_values = NULL;
	/* Buffer for calculated function values on host (for checking purposes, will be calculated by CPU) */
	double* host_right_function_values = NULL;
	/* Buffer for calculated function values on device (will be calculated by GPU) */
	double* device_function_values = NULL;
	/* Elapsed calculation time */
	float elapsed_time = 0.0f;

	/* Necessary buffer size for calculated values */
	const unsigned int fv_size = RANGE_ELEMENTS_COUNT * sizeof(double);

	/* CUDA events to count elapsed time */
	cudaEvent_t cuda_event_start, cuda_event_stop;
	/* Variable for CUDA errors processing */
	cudaError_t cuda_error = cudaSuccess;

	/* Check whenever there are proper arguments */
	if ((argc > 1) && (argc < 3)) {
		fprintf(stderr, "ERROR: Bad arguments.\n Usage: cuda_sin [--cuda <threads_per_block>]\n");
		return 1;
	} else if ((argc > 1) && (strncmp(argv[1], "--cuda", 7) != 0)) {
		fprintf(stderr, "ERROR: Bad arguments.\n Usage: cuda_sin [--cuda <threads_per_block>]\n");
		return 1;
	}

	if (argc > 1) {
		/* Switch CUDA usage flag */
		use_cuda = 1;
		/* Try to read the --cuda value */
		if (sscanf(argv[2], "%u", &cuda_threads_per_block) != 1) {
			fprintf(stderr, "ERROR: Bad number of threads_per_block.\n");
			return 1;
		} else if (cuda_threads_per_block == 0) {
			/* Bad threads count value */
			fprintf(stderr, "ERROR: Bad number of threads_per_block.\n");
			return 1;
		}
	}

	/* Calculate CUDA blocks count (must not exceed maximum possible value) and elements to be calculated per each thread */
	do {
		++cuda_elements_per_thread;
		cuda_blocks_count = (RANGE_ELEMENTS_COUNT / (cuda_elements_per_thread * cuda_threads_per_block)) +
			((RANGE_ELEMENTS_COUNT % (cuda_elements_per_thread * cuda_threads_per_block) > 0)? 1: 0);
	} while (cuda_blocks_count > CUDA_BLOCKS_MAX);

	/* Create CUDA events */
	cuda_error = cudaEventCreate(&cuda_event_start);
	if (cuda_error != cudaSuccess) {
		fprintf(stderr, "ERROR: Unable to create CUDA event.\n");
		return 2;
	}

	cuda_error = cudaEventCreate(&cuda_event_stop);
	if (cuda_error != cudaSuccess) {
		cudaEventDestroy(cuda_event_start);
		fprintf(stderr, "ERROR: Unable to create CUDA event.\n");
		return 2;
	}

	/* Allocate host memory for proper values */
	host_right_function_values = (double*)malloc(fv_size);
	if (!host_right_function_values) {
		cudaEventDestroy(cuda_event_start);
		cudaEventDestroy(cuda_event_stop);
		fprintf(stderr, "ERROR: Unable to allocate host memory.\n");
		return 3;
	}

	/* Allocate host memory for values to be checked */
	host_function_values = (double*)malloc(fv_size);
	if (!host_function_values) {
		free(host_right_function_values);
		cudaEventDestroy(cuda_event_start);
		cudaEventDestroy(cuda_event_stop);
		fprintf(stderr, "ERROR: Unable to allocate host memory.\n");
		return 3;
	}

	if (use_cuda) {
		/* Allocate GPU memory for values to be calculated */
		cuda_error = cudaMalloc((void**)&device_function_values, fv_size);
		if (cuda_error == cudaSuccess) {
			/* Asynchronously note that calculation has started */
			cudaEventRecord(cuda_event_start, 0);
			/* Asynchronously run GPU calculations */
			device_sin<<<cuda_blocks_count, cuda_threads_per_block>>>(device_function_values, cuda_elements_per_thread);
			/* Asynchronously note that calculation has been completed */
			cudaEventRecord(cuda_event_stop, 0);
			/* Wait for all the asynchronous calls to be executed and proceeded */
			cudaEventSynchronize(cuda_event_stop);

			/* Copy GPU calculated values into the host memory*/
			cudaMemcpy(host_function_values, device_function_values, fv_size, cudaMemcpyDeviceToHost);

			/* Deallocate GPU memory */
			cudaFree((void*)device_function_values);
		}
	} else {
		/* Asynchronously note that calculation has started */
		cudaEventRecord(cuda_event_start, 0);
		/* Wait for event to be proceeded */
		cudaEventSynchronize(cuda_event_start);
		/* Run CPU calculations */
		host_sin(host_function_values);
		/* Asynchronously note that calculation has been completed */
		cudaEventRecord(cuda_event_stop, 0);
		/* Wait for event to be proceeded */
		cudaEventSynchronize(cuda_event_stop);
	}

	/* Caclulate proper values on host */
	host_sin(host_right_function_values);
	/* Check caclulated values to be proper ones */
	for (i = 0; i < RANGE_ELEMENTS_COUNT; ++i) {
		if (abs(host_function_values[i] - host_right_function_values[i]) > 1e-10) {
			are_counted_properly = 0;
			break;
		}
	}

	if (cuda_error == cudaSuccess) {
		/* Everything was ok with CUDA - calculate elapsed time in ms */
		cudaEventElapsedTime(&elapsed_time, cuda_event_start, cuda_event_stop);
		/* Print information about performed calculations */
		fprintf(
			stdout, "Calculated %s %u values on %s. Time spent: %.02f ms.\n",
			(are_counted_properly)? "properly": "UNproperly",
			RANGE_ELEMENTS_COUNT, (use_cuda)? "device": "host", elapsed_time
		);
	} else {
		/* Something went wrong - just notify the user, cleanup follows */
		fprintf(stderr, "ERROR: Unable to allocate device memory.\n");
	}

	/* Cleanup */
	free(host_function_values);
	free(host_right_function_values);
	
	cudaEventDestroy(cuda_event_start);
	cudaEventDestroy(cuda_event_stop);

	/* If CUDA commands executed successfully - then everything is ok */
	return (cuda_error == cudaSuccess)? 0: 3;
}
