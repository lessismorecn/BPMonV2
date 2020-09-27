#pragma once
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <iterator>
#include <numeric>

using namespace std;

typedef long double ld;
typedef unsigned int uint;
typedef std::vector<ld>::iterator vec_iter_ld;


template<typename T>
std::ostream &operator<<(std::ostream &os, std::vector<T> vec) {
	os << "[";
	if (vec.size() != 0) {
		std::copy(vec.begin(), vec.end() - 1, std::ostream_iterator<T>(os, " "));
		os << vec.back();
	}
	os << "]";
	return os;
}

/**
* This class calculates mean and standard deviation of a subvector.
* This is basically stats computation of a subvector of a window size qual to "lag".
*/
class VectorStats {
public:
	/**
	* Constructor for VectorStats class.
	*
	* @param start - This is the iterator position of the start of the window,
	* @param end   - This is the iterator position of the end of the window,
	*/
	VectorStats(vec_iter_ld start, vec_iter_ld end) {
		this->start = start;
		this->end = end;
		this->compute();
	}

	/**
	* This method calculates the mean and standard deviation using STL function.
	* This is the Two-Pass implementation of the Mean & Variance calculation.
	*/
	void compute() {
		ld sum = std::accumulate(start, end, 0.0);
		uint slice_size = std::distance(start, end);
		ld mean = sum / slice_size;
		std::vector<ld> diff(slice_size);
		std::transform(start, end, diff.begin(), [mean](ld x) { return x - mean; });
		ld sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
		ld std_dev = std::sqrt(sq_sum / slice_size);

		this->m1 = mean;
		this->m2 = std_dev;
	}

	ld mean() {
		return m1;
	}

	ld standard_deviation() {
		return m2;
	}

private:
	vec_iter_ld start;
	vec_iter_ld end;
	ld m1;
	ld m2;
};

/**
* This is the implementation of the Smoothed Z-Score Algorithm.

*
* @param input - input signal
* @param lag - the lag of the moving window
* @param threshold - the z-score at which the algorithm signals
* @param influence - the influence (between 0 and 1) of new signals on the mean and standard deviation
* @return a vector<int> containing signals.
*/
vector<int> z_score_thresholding(vector<ld> input, int lag, ld threshold, ld influence) {
	//vector<ld> output;

	uint n = (uint)input.size();
	vector<int> signals(input.size());
	vector<ld> filtered_input(input.begin(), input.end());
	vector<ld> filtered_mean(input.size());
	vector<ld> filtered_stddev(input.size());

	VectorStats lag_subvector_stats(input.begin(), input.begin() + lag);
	filtered_mean[lag - 1] = lag_subvector_stats.mean();
	filtered_stddev[lag - 1] = lag_subvector_stats.standard_deviation();

	for (int i = lag; i < n; i++) {
		if (abs(input[i] - filtered_mean[i - 1]) > threshold * filtered_stddev[i - 1]) {
			signals[i] = (int)(input[i] > filtered_mean[i - 1]) ? 1.0 : -1.0;
			filtered_input[i] = influence * input[i] + (1 - influence) * filtered_input[i - 1];
		}
		else {
			signals[i] = 0;
			filtered_input[i] = input[i];
		}
		VectorStats lag_subvector_stats(filtered_input.begin() + (i - lag), filtered_input.begin() + i);
		filtered_mean[i] = lag_subvector_stats.mean();
		filtered_stddev[i] = lag_subvector_stats.standard_deviation();
	}

	return signals;
};
