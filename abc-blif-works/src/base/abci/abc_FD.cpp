#include <iostream>
#include <sstream>
#include <stdint.h>
#include <vector>
#include <fstream>
#include "abc_FD.h"

using namespace::std;

#define DEBUG 1


//--------------------------------------------------------------------------

int bin_pow(int n)
{
	return (1 << n);
}

//--------------------------------------------------------------------------

void to_binary(int n, bool *arr, int bitwidth)
{
	for (int i = bitwidth - 1; i >= 0; i--)
	{
		arr[i] = (n % 2) ? true: false;
		n /= 2;
	}
}

//--------------------------------------------------------------------------

void read_var_list(int *var_list, int n)
{
	for (int i = 0; i < n; i++)
	{	
		cin >> var_list[i];
	}
}
//--------------------------------------------------------------------------

void read_table(bool *table, int n)
{
	for (int i = 0; i < n; i++)
		cin >> table[i];

}
//--------------------------------------------------------------------------

int projection(int orig_num, int num_vars, int *var_list)
{
	int x = 0;
	for (int i = 0; i < num_vars; i++)
	{
		if (orig_num & (1 << var_list[i]))
			x |= (1 << i);
	}

	return x;
}

//--------------------------------------------------------------------------

struct rectangle_entry{
	bool f_value;
	bool g_value;
	int group_id;
};

//--------------------------------------------------------------------------

class brick
{
	private:
	int num_vars;
	int *var_list;
	bool *table;

	public:
	brick(int vars);
	void read_brick();

	int get_num_vars() {return num_vars;}
	int *get_var_list() {return var_list;}
	bool *get_table() {return table;}

	void fill_rectangle(rectangle_entry *rectangle, int count);
};

//--------------------------------------------------------------------------

brick::brick(int vars)
{
	num_vars = vars;
	var_list = new int[num_vars];
	table = new bool[bin_pow(num_vars)];
}

//--------------------------------------------------------------------------

	void
brick::read_brick()
{
	read_var_list(var_list, num_vars);
	read_table(table, bin_pow(num_vars));
}

//--------------------------------------------------------------------------

	void 
brick::fill_rectangle(rectangle_entry *rectangles, int count)
{
	int size = bin_pow(count);
	for (int i = 0; i < size; i++)
	{
		rectangles[i].g_value = table[projection(i, num_vars, var_list)];        	
	}
}

//--------------------------------------------------------------------------

void read_inps(bool *table, int num_inps, brick **brick_list, int num_functions )
{
	read_table(table, bin_pow(num_inps));

	for (int i = 0; i < num_functions; i++)
	{
		int vars;
		cin >> vars;

		brick_list[i] = new brick(vars);
		brick_list[i]->read_brick();
	}
}

//--------------------------------------------------------------------------

void show_table(bool *table, int num_inps)
{
	bool *arr = new bool[num_inps];
	int size = bin_pow(num_inps);

	for (int i = 0; i < size; i++)
	{
		to_binary(i, arr, num_inps);
		for (int j = 0; j < num_inps; j++)
			std::cout << arr[j] << " ";

		std::cout << " -->  " << table[i] << std::endl;
	}

	delete[] arr;
}

//--------------------------------------------------------------------------

// only for debugging purposes
void show_rectangles(rectangle_entry *rectangles, int vars, int num_groups)
{
	int size = bin_pow(vars);
	vector<vector<int> > pos_groups (num_groups, vector<int>());
	vector<vector<int> > neg_groups (num_groups, vector<int>());

	for (int i = 0; i < size; i++)
	{
		if (rectangles[i].group_id == -1)
			continue;

		if (rectangles[i].f_value)
		{
			pos_groups[rectangles[i].group_id].push_back(i);
		}
		else
		{
			neg_groups[rectangles[i].group_id].push_back(i);
		}
	}

	std::cout << "*******************************************" << std::endl;
	for (int i = 0; i < num_groups; i++)
	{
		std::cout << "rectangle " << i << ":" << std::endl;

		std::cout << "{";
		for (int j = 0; j < pos_groups[i].size(); j++)
		{
			std::cout << pos_groups[i][j];
			if (j != pos_groups[i].size() - 1)
				std::cout << ", ";
		}
		std::cout << "}";

		std::cout << " X ";

		std::cout << "{";
		for (int j = 0; j < neg_groups[i].size(); j++)
		{
			std::cout << neg_groups[i][j];
			if (j != neg_groups[i].size() - 1)
				std::cout << ", ";
		}
		std::cout << "}";

		std::cout << std::endl << std::endl;
	}
}

//--------------------------------------------------------------------------

int remove_degenerate(int *arr, int size)
{	
	int count = 0;
	for (int i = 0; i < size; i++)
	{
		if (arr[i] == 3)
		{
			arr[i] = count++;
		}
		else
		{
			arr[i] = -1;
		}
	}

	return count;
}

//--------------------------------------------------------------------------

uint64_t get_uncovered(rectangle_entry *rectangles, int vars, int num_groups, 
		bool should_consider_brick)
{
	if (should_consider_brick)
		num_groups <<= 1;

	vector<uint64_t> pos_groups (num_groups, 0);
	vector<uint64_t> neg_groups (num_groups, 0);

	int size = bin_pow(vars);

	for (int i = 0; i < size; i++)
	{
		if (rectangles[i].group_id == -1)
			continue;

		int t = rectangles[i].group_id;
		if (should_consider_brick)
		{
			t <<= 1;
			if (rectangles[i].g_value)
				t |= 1;
		}

		if (rectangles[i].f_value)
			pos_groups[t]++;
		else
			neg_groups[t]++;
	}

	uint64_t sum = 0;
	for (int i = 0; i < num_groups; i++)
		sum += pos_groups[i] * neg_groups[i];

	return sum;
}

//--------------------------------------------------------------------------

uint64_t get_information_coverage(rectangle_entry *rectangles, brick *b, 
		int vars, int num_groups, uint64_t orig_uncovered = 0)
{
	if (orig_uncovered == 0)
	{
		orig_uncovered = get_uncovered(rectangles, vars, num_groups, false);
	}

	b->fill_rectangle(rectangles, vars);

	uint64_t uncovered = get_uncovered(rectangles, vars, num_groups, true);
	return (orig_uncovered - uncovered);
}

//--------------------------------------------------------------------------

void split(rectangle_entry *rectangles, brick *b, int vars, int *num_groups)
{
	b->fill_rectangle(rectangles, vars);

	int size = bin_pow(vars);
	int *groups = new int[2 * (*num_groups)];

	for (int i = 0; i < 2 * (*num_groups); i++)
		groups[i] = 0;

	// assigning the ids to the splitted rectangles
	for (int i = 0; i < size; i++)
	{
		if (rectangles[i].group_id == -1)
			continue;

		rectangles[i].group_id <<= 1;
		if (rectangles[i].g_value)
			rectangles[i].group_id |= 1;

		if (rectangles[i].f_value)
		{
			groups[rectangles[i].group_id] |= 1;
		}
		else
		{
			groups[rectangles[i].group_id] |= 2;
		}
	}

	// removing the degenerate rectangles
	*num_groups = remove_degenerate(groups, 2 * (*num_groups));

	// reassigning the new ids
	for (int i = 0; i < size; i++)
	{
		if (rectangles[i].group_id == -1)
			continue;

		rectangles[i].group_id = groups[rectangles[i].group_id];
	}

	delete[] groups;
}

//--------------------------------------------------------------------------

//TODO: use minisat for this part
int is_functionally_dependent(bool *table, int num_inps, brick **brick_list, 
		int num_bricks, vector<int> &pos_group, vector<int> &neg_group)
{
	int size = bin_pow(num_inps);
	rectangle_entry *rectangles = new rectangle_entry[size];

	for (int i = 0; i < size; i++)
	{
		rectangles[i].f_value = table[i];
		rectangles[i].group_id = 0;
	}

	int num_groups = 1;

	if (DEBUG)
	{
		std::cout << "Original uncovered rectangle" << std::endl;
		show_rectangles(rectangles, num_inps, num_groups);
	}

	for (int i = 0; i < num_bricks; i++)
	{
		split(rectangles, brick_list[i], num_inps, &num_groups);
		if (DEBUG)
		{
			std::cout << "Uncovered rectangles after brick " << i << std::endl;
			show_rectangles(rectangles, num_inps, num_groups);		
		}

		if (num_groups == 0)
			return i;
	}

	for (int i = 0; i < size; i++)
	{
		if (rectangles[i].group_id != 0)
			continue;

		if (rectangles[i].f_value)
			pos_group.push_back(i);
		else
			neg_group.push_back(i);
	}

	delete[] rectangles; 
	return -1;
}

//--------------------------------------------------------------------------

int check_FD( int n)
{
	int fd;
	ostringstream oss;
	oss << "my_in" << n << ".txt";
	const char * input_file_name = oss.str().c_str();
	filebuf inputFileBuffer;
	inputFileBuffer.open(input_file_name, ios::in);
	if (!inputFileBuffer.is_open())
	{
		cerr << "Could not open input file: " << string("input")  << ", using stdin instead" << endl;
	} else {
		istream is(&inputFileBuffer);
		cin.rdbuf(is.rdbuf());

		int num_inps, num_bricks;
		cin >> num_inps >> num_bricks;

		int size = bin_pow(num_inps);
		bool *table = new bool[size];

		brick **brick_list = new brick*[num_bricks];	
		read_inps(table, num_inps, brick_list, num_bricks);

		if (DEBUG)
		{
			std::cout << "Reading done successfully" << std::endl;
			std::cout << "------------------------------------------" << std::endl;
		}

		vector<int> pos_group;
		vector<int> neg_group;

		fd = is_functionally_dependent(table, num_inps, brick_list, num_bricks, 
				pos_group, neg_group);

		if (fd != -1 )
		{
			std::cout << "functional dependency exist" << std::endl;
		}
		else
		{
			std::cout << "functional dependency does not exist" << std::endl;
		}
		inputFileBuffer.close();
	}
	return fd;
}
