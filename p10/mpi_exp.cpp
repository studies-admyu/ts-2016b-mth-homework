#include <iostream>

#include <mpi.h>

int main(int argc, char* argv[])
{
	/* Precision parameter (number of terms) */
	const unsigned int terms_count = 10;

	/* Init MPI */
	MPI_Init(&argc, &argv);

	/* Rank of the process */
	int comm_rank = -1;
	MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);

	/* Comminicator size */
	int comm_size = 0;
	MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

	/* Local part of sum */
	double local_sum = 0.0;
	/* Current term of the sum (1 / factorial) */
	double local_factorial = 1.0;
	/* Factorial multiplier */
	int local_factorial_multiplier = 0;

	/* Initial value: each process will receive (1 / k!) where k is communicator rank of the process */
	for (unsigned int i = 1; i <= comm_rank; ++i) {
		++local_factorial_multiplier;
		local_factorial /= (double)local_factorial_multiplier;
	}

	/* Process will add (1 / (i * s + k)!) where s is communicator size and k is communicator rank of the process */
	for (unsigned int i = 0; i < terms_count; ++i) {
		local_sum += local_factorial;
		for (unsigned int j = 0; j < comm_size; ++j) {
			++local_factorial_multiplier;
			local_factorial /= (double)local_factorial_multiplier;
		}
	}

	/* Final result */
	double result = 0.0;

	MPI_Reduce(&local_sum, &result, 1, MPI_LONG_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	if (comm_rank == 0) {
		std::cout.precision(20);
		std::cout << result << std::endl;
	}

	/* Finalize */
	MPI_Finalize();
	return 0;
}
