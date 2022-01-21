#ifndef COMMON_H
#define COMMON_H

#include "../containers.h"
#include "../strf.h"
#include "../util.h"

#include <mutex>

namespace bwgame {

struct apm_t
	{
		a_deque<int> history;
		int current_apm = 0;
		int last_frame_div = 0;
		static const int resolution = 1;
		void add_action(int frame)
		{
			if (!history.empty() && frame / resolution == last_frame_div)
			{
				++history.back();
			}
			else
			{
				if (history.size() >= 10 * 1000 / 42 / resolution)
					history.pop_front();
				history.push_back(1);
				last_frame_div = frame / 12;
			}
		}
		void update(int frame)
		{
			if (history.empty() || frame / resolution != last_frame_div)
			{
				if (history.size() >= 10 * 1000 / 42 / resolution)
					history.pop_front();
				history.push_back(0);
				last_frame_div = frame / resolution;
			}
			if (frame % resolution)
				return;
			if (history.size() == 0)
			{
				current_apm = 0;
				return;
			}
			int sum = 0;
			for (auto &v : history)
				sum += v;
			current_apm = (int)(sum * ((int64_t)256 * 60 * 1000 / 42 / resolution) / history.size() / 256);
		}
	};
	
namespace ui {

void log_str(a_string str);
void fatal_error_str(a_string str);

template<typename...T>
void log(const char* fmt, T&&... args) {
	log_str(format(fmt, std::forward<T>(args)...));
}


template<typename... T>
void fatal_error(const char* fmt, T&&... args) {
	fatal_error_str(format(fmt, std::forward<T>(args)...));
}

template<typename... T>
void xcept(const char* fmt, T&&... args) {
	fatal_error_str(format(fmt, std::forward<T>(args)...));
}

}

}

#endif
