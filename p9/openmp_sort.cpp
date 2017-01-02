#include <iostream>
#include <string>
#include <stdexcept>

#include <omp.h>

/* Elements count to sort macro */
#define ELEMENTS_COUNT 10000000

/** Short way to deliver arguments to merge function */
struct PartInfo
{
	/** Array the part is contained in */
	int* array_beginning;
	/** The index of the first element of the part */
	unsigned int begin;
	/** The size of the part */
	unsigned int size;
};

/** Checks for parts in-memory intersection
  *		@param part1 - the first part to check;
  *		@param part2 - the second part to check;
  *		@return True if there is an intersection, false otherwise.
  */
bool intersect(PartInfo& part1, PartInfo& part2)
{
	int* checking_part_begin = part1.array_beginning + part1.begin;
	int* checking_part_end = checking_part_begin + part1.size;
	int* other_part_begin = part2.array_beginning + part2.begin;

	/* Check whenever the second part beginning is between the begining and the end of the first part */
	if (
		(checking_part_begin <= other_part_begin) &&
		(checking_part_end > other_part_begin)
	) {
		/* Intersection is found */
		return true;
	}

	/* Swap the first and the second parts beginnings */
	int* temp_variable = checking_part_begin;
	checking_part_begin = other_part_begin;
	other_part_begin = temp_variable;
	
	checking_part_end = checking_part_begin + part2.size;

	/* Check whenever the first part beginning is between the begining and the end of the second part */
	if (
		(checking_part_begin <= other_part_begin) &&
		(checking_part_end > other_part_begin)
	) {
		/* Intersection is found */
		return true;
	}

	/* No intersection */
	return false;
}

/** Merges two sorted parts of array into one sorted array
  *		@param part1_in - the first sorted part details;
  *		@param part1_in - the second sorted part details;
  *		@param res_out - memory area to write out the result;
  * @warning Parts & output memory ranges must not intersect each other.
  */
void merge(PartInfo& part1_in, PartInfo& part2_in, PartInfo& res_out)
{
	/* Check for intersections and size correspondence */
	if (intersect(part1_in, part2_in)) {
		throw std::runtime_error("Input parts intersect.");
	} else if (intersect(part1_in, res_out) || intersect(part2_in, res_out)) {
		throw std::runtime_error("Input part intersects with output buffer.");
	} else if (res_out.size != part1_in.size + part2_in.size) {
		throw std::runtime_error("Output buffer size doesn't correspond to parts' sizes.");
	}

	/* Index of current element in the first part */
	unsigned int part1_index = part1_in.begin;
	/* The index of the element which follows last element of the first part */
	unsigned int part1_end = part1_in.begin + part1_in.size;
	/* Index of current element in the second part */
	unsigned int part2_index = part2_in.begin;
	/* The index of the element which follows last element of the second part */
	unsigned int part2_end = part2_in.begin + part2_in.size;
	/* Index of current element in the result array */
	unsigned int res_index = res_out.begin;

	/* Write out the lesser element between the current elements in the parts while possible */
	while ((part1_index < part1_end) && (part2_index < part2_end)) {
		if (part1_in.array_beginning[part1_index] <= part2_in.array_beginning[part2_index]) {
			res_out.array_beginning[res_index] = part1_in.array_beginning[part1_index];
			++part1_index;
		} else {
			res_out.array_beginning[res_index] = part2_in.array_beginning[part2_index];
			++part2_index;
		}
		++res_index;
	}

	/* Append remaining elements if any */
	for (; part1_index < part1_end; ++part1_index) {
		res_out.array_beginning[res_index] = part1_in.array_beginning[part1_index];
		++res_index;
	}
	for (; part2_index < part1_end; ++part2_index) {
		res_out.array_beginning[res_index] = part2_in.array_beginning[part2_index];
		++res_index;
	}
}

int main(int argc, char* argv[])
{
	/* Check for the command line argument */
	if (argc < 2) {
		std::cerr << "ERROR: Use --standart or --openmp as an argument." << std::endl;
		return 1;
	}

	std::string arg_run_mode(argv[1]);
	if ((arg_run_mode != std::string("--standart")) && (arg_run_mode != std::string("--openmp"))) {
		std::cerr << "ERROR: Use --standart or --openmp as an argument." << std::endl;
		return 1;
	}
	
	/* Determine whenever to use openmp */
	bool use_openmp = (arg_run_mode == std::string("--openmp"));

	/* Allocate buffers */
	int current_buffer = 0;
	int* temp_buffers[2] = {nullptr, nullptr};
	try {
		temp_buffers[0] = new int[ELEMENTS_COUNT];
		temp_buffers[1] = new int[ELEMENTS_COUNT];
	} catch (...) {
		std::cerr << "ERROR: Unable to allocate memory for buffers to sort." << std::endl;
		/* Deallocate buffers if necessary */
		if (temp_buffers[0]) {
			delete[] temp_buffers[0];
		}
		if (temp_buffers[1]) {
			delete[] temp_buffers[1];
		}
		return 2;
	}

	/* Fill the buffer with initial values to sort */
	for (int i = 0; i < ELEMENTS_COUNT; ++i) {
		temp_buffers[current_buffer][i] = ELEMENTS_COUNT - i;
	}

	/* Begin the time counting */
	double program_time = omp_get_wtime();

	unsigned int part_size = 1;

	while (part_size < ELEMENTS_COUNT) {
		/* Each step of the loop merges two parts of array */
		int cycle_step = 2 * (int)part_size;

		if (use_openmp) {
			# pragma omp parallel for
			for (int i = 0; i < ELEMENTS_COUNT; i += cycle_step) {
				unsigned int remaining_size = ELEMENTS_COUNT - (unsigned int)i;
				PartInfo part1_info = {
					temp_buffers[current_buffer],
					(unsigned int)i,
					/* Check whenever the part size is lesser than planned */
					(remaining_size < part_size)? remaining_size: part_size
				};

				/* Check whenever there is another part to merge. Consider no other part as zero length part */
				remaining_size = (remaining_size < part_size)? 0: remaining_size - part_size;
				PartInfo part2_info = {
					temp_buffers[current_buffer],
					(unsigned int)i + part_size,
					/* Check whenever the part size is lesser than planned */
					(remaining_size < part_size)? remaining_size: part_size
				};

				PartInfo output_info = {
					temp_buffers[1 - current_buffer],
					(unsigned int)i,
					part1_info.size + part2_info.size
				};

				/* Merge parts */
				merge(part1_info, part2_info, output_info);
			}
		} else {
			for (int i = 0; i < ELEMENTS_COUNT; i += cycle_step) {
				unsigned int remaining_size = ELEMENTS_COUNT - (unsigned int)i;
				PartInfo part1_info = {
					temp_buffers[current_buffer],
					(unsigned int)i,
					/* Check whenever the part size is lesser than planned */
					(remaining_size < part_size)? remaining_size: part_size
				};

				/* Check whenever there is another part to merge. Consider no other part as zero length part */
				remaining_size = (remaining_size < part_size)? 0: remaining_size - part_size;
				PartInfo part2_info = {
					temp_buffers[current_buffer],
					(unsigned int)i + part_size,
					/* Check whenever the part size is lesser than planned */
					(remaining_size < part_size)? remaining_size: part_size
				};

				PartInfo output_info = {
					temp_buffers[1 - current_buffer],
					(unsigned int)i,
					part1_info.size + part2_info.size
				};

				/* Merge parts */
				merge(part1_info, part2_info, output_info);
			}
		}

		/* Switch the current buffer (will contain the result of the merge) */
		current_buffer = 1 - current_buffer;
		/* Increase part size */
		part_size *= 2;
	}

	/* Count spent time */
	program_time = omp_get_wtime() - program_time;

	/* Check whenever the result is right */
	bool right_sorted = true;
	for (int i = 0; i < ELEMENTS_COUNT; ++i) {
		if (temp_buffers[current_buffer][i] != i + 1) {
			right_sorted = false;
			break;
		}
	}

	/* Deallocate buffers */
	delete[] temp_buffers[0];
	delete[] temp_buffers[1];

	/* Write details */
	std::cout << (right_sorted? "Right": "Wrong") << "; Time: " << (program_time * 1000.0) << " ms." << std::endl;

	return 0;
}
