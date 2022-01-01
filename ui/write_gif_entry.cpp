
#ifdef TITAN_WRITEGIF
#include "GifEncoder.h"
#endif

#include "ui.h"
#include "common.h"
#include "bwgame.h"
#include "replay.h"

#include <chrono>
#include <thread>

using namespace bwgame;

using ui::log;

FILE *log_file = nullptr;

#ifdef TITAN_WRITEGIF
GifEncoder gifEncoder;
#endif

namespace bwgame
{

	namespace ui
	{

		void log_str(a_string str)
		{
			fwrite(str.data(), str.size(), 1, stdout);
			fflush(stdout);
			if (!log_file)
				log_file = fopen("log.txt", "wb");
			if (log_file)
			{
				fwrite(str.data(), str.size(), 1, log_file);
				fflush(log_file);
			}
		}

		void fatal_error_str(a_string str)
		{
			log("fatal error: %s\n", str);
			std::terminate();
		}

	} // namespace ui

} // namespace bwgame

struct saved_state
{
	state st;
	action_state action_st;
	std::array<apm_t, 12> apm;
};

struct main_t
{
	ui_functions ui;

	main_t(game_player player) : ui(std::move(player)) {}

	std::chrono::high_resolution_clock clock;
	std::chrono::high_resolution_clock::time_point last_tick;

	std::chrono::high_resolution_clock::time_point last_fps;
	int fps_counter = 0;

	a_map<int, std::unique_ptr<saved_state>> saved_states;
	unit_t *screen_show_unit = NULL;
	int screen_show_unit_cooldown = 0;

	void reset()
	{
		saved_states.clear();
		ui.reset();
	}

	bool update()
	{
		auto now = clock.now();

		auto tick_speed = std::chrono::milliseconds((fp8::integer(42) / ui.game_speed).integer_part());

		if (now - last_fps >= std::chrono::seconds(1))
		{
			//log("game fps: %g\n", fps_counter / std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 1>>>(now - last_fps).count());
			last_fps = now;
			fps_counter = 0;
		}

		auto next = [&]()
		{
			int save_interval = 10 * 1000 / 42;
			if (ui.st.current_frame == 0 || ui.st.current_frame % save_interval == 0)
			{
				auto i = saved_states.find(ui.st.current_frame);
				if (i == saved_states.end())
				{
					auto v = std::make_unique<saved_state>();
					v->st = copy_state(ui.st);
					v->action_st = copy_state(ui.action_st, ui.st, v->st);
					v->apm = ui.apm;

					a_map<int, std::unique_ptr<saved_state>> new_saved_states;
					new_saved_states[ui.st.current_frame] = std::move(v);
					while (!saved_states.empty())
					{
						auto i = saved_states.begin();
						auto v = std::move(*i);
						saved_states.erase(i);
						new_saved_states[v.first] = std::move(v.second);
					}
					std::swap(saved_states, new_saved_states);
				}
			}

			ui.replay_functions::next_frame();
			for (auto &v : ui.apm)
				v.update(ui.st.current_frame);
		};

		if (!ui.is_done() || ui.st.current_frame != ui.replay_frame)
		{
			if (ui.st.current_frame != ui.replay_frame)
			{
				if (ui.st.current_frame != ui.replay_frame)
				{
					auto i = saved_states.lower_bound(ui.replay_frame);
					if (i != saved_states.begin())
						--i;
					auto &v = i->second;
					if (ui.st.current_frame > ui.replay_frame || v->st.current_frame > ui.st.current_frame)
					{
						ui.st = copy_state(v->st);
						ui.action_st = copy_state(v->action_st, v->st, ui.st);
						ui.apm = v->apm;
					}
				}
				if (ui.st.current_frame < ui.replay_frame)
				{
					for (size_t i = 0; i != 32 && ui.st.current_frame != ui.replay_frame; ++i)
					{
						for (size_t i2 = 0; i2 != 4 && ui.st.current_frame != ui.replay_frame; ++i2)
						{
							next();
						}
						if (clock.now() - now >= std::chrono::milliseconds(50))
							break;
					}
				}
				last_tick = now;
			}
			else
			{
				if (ui.is_paused)
				{
					last_tick = now;
				}
				else
				{
					auto tick_t = now - last_tick;
					if (tick_t >= tick_speed * 16)
					{
						last_tick = now - tick_speed * 16;
						tick_t = tick_speed * 16;
					}
					auto tick_n = tick_speed.count() == 0 ? 128 : tick_t / tick_speed;
					for (auto i = tick_n; i;)
					{
						--i;
						++fps_counter;
						last_tick += tick_speed;

						if (!ui.is_done())
							next();
						else
							break;
						if (i % 4 == 3 && clock.now() - now >= std::chrono::milliseconds(50))
							break;
					}
					ui.replay_frame = ui.st.current_frame;
				}
			}
		}

		ui.update();

#ifdef TITAN_WRITEGIF
		if (!ui.is_done())
		{
			int w;
			int h;
			uint32_t *px;
			std::tie(w, h, px) = ui.get_rgba_buffer();
			int delay = 20;

			gifEncoder.push(GifEncoder::PIXEL_FORMAT_RGBA, (uint8_t *)px, w, h, delay);

			if (!screen_show_unit && screen_show_unit_cooldown == 0)
			{
				for (size_t i = 7; i != 0;)
				{
					--i;
					for (unit_t *u : ptr(ui.st.player_units[i]))
					{
						if (!ui.unit_visble_on_minimap(u))
							continue;
						if (u->air_weapon_cooldown || u->ground_weapon_cooldown)
						{
							screen_show_unit = u;
							screen_show_unit_cooldown = 5 + std::rand() % 5;
						}
					}
				}
			}
			if (screen_show_unit)
			{
				unit_t *u = screen_show_unit;
				if (screen_show_unit_cooldown)
				{
					ui.screen_pos = xy(u->position.x - ui.view_width / 2, u->position.y - ui.view_height / 2);
					ui.draw_ui_minimap = false;
				}
				else
				{
					ui.draw_ui_minimap = true;
					screen_show_unit = NULL;
					screen_show_unit_cooldown = 10 + std::rand() % 5;
				}
			}
			if (screen_show_unit_cooldown)
			{
				screen_show_unit_cooldown--;
			}
		}
		else
		{
			log("saving gif");
			if (!gifEncoder.close())
			{
				fprintf(stderr, "Error close gif file\n");
			}
			return false;
		}
#endif
		return true;
	}
};

main_t *g_m = nullptr;

uint32_t freemem_rand_state = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
auto freemem_rand()
{
	freemem_rand_state = freemem_rand_state * 22695477 + 1;
	return (freemem_rand_state >> 16) & 0x7fff;
}

void out_of_memory()
{
	printf("out of memory :(\n");
	throw std::bad_alloc();
}

size_t bytes_allocated = 0;

void free_memory()
{
	if (!g_m)
		out_of_memory();
	size_t n_states = g_m->saved_states.size();
	printf("n_states is %d\n", n_states);
	if (n_states <= 2)
		out_of_memory();
	size_t n;
	if (n_states >= 300)
		n = 1 + freemem_rand() % (n_states - 2);
	else
	{
		auto begin = std::next(g_m->saved_states.begin());
		auto end = std::prev(g_m->saved_states.end());
		n = 1;
		int best_score = std::numeric_limits<int>::max();
		size_t i_n = 1;
		for (auto i = begin; i != end; ++i, ++i_n)
		{
			int score = 0;
			for (auto i2 = begin; i2 != end; ++i2)
			{
				if (i2 != i)
				{
					int d = i2->first - i->first;
					score += d * d;
				}
			}
			if (score < best_score)
			{
				best_score = score;
				n = i_n;
			}
		}
	}
	g_m->saved_states.erase(std::next(g_m->saved_states.begin(), n));
}

struct dlmalloc_chunk
{
	size_t prev_foot;
	size_t head;
	dlmalloc_chunk *fd;
	dlmalloc_chunk *bk;
};

size_t alloc_size(void *ptr)
{
	dlmalloc_chunk *c = (dlmalloc_chunk *)((char *)ptr - sizeof(size_t) * 2);
	return c->head & ~7;
}

extern "C" void *dlmalloc(size_t);
extern "C" void dlfree(void *);

size_t max_bytes_allocated = 160 * 1024 * 1024;

int main()
{

	using namespace bwgame;

	log("v25\n");

	size_t screen_width = 1280;
	size_t screen_height = 800;
	/*
		260x416
	*/

	std::chrono::high_resolution_clock clock;
	auto start = clock.now();

	auto load_data_file = data_loading::data_files_directory("D:\\dev\\openbw\\openbw-original\\openbw-original\\Debug\\");
	log("mpqs loaded\n");

	game_player player(load_data_file);

	main_t m(std::move(player));
	auto &ui = m.ui;

	m.ui.load_all_image_data(load_data_file);

	ui.load_data_file = [&](a_vector<uint8_t> &data, a_string filename)
	{
		load_data_file(data, std::move(filename));
	};

	ui.init();

#ifndef EMSCRIPTEN
	//ui.load_replay_file("C:\\Users\\alexp\\Downloads\\25555-Star_kras-PvT.rep");
	//ui.load_replay_file("D:\\dev\\titan-reactor\\packages\\downgrade-replay\\test\\out.116.rep");
	//ui.load_replay_file("C:\\Users\\alexp\\Downloads\\bonyth\\broken\\gol-000720,(3)Whiteout1.2.rep");
	ui.load_replay_file("C:\\Users\\alexp\\Downloads\\gol-BWCL_vs_masucci2.rep");

#endif

#ifdef TITAN_WRITEGIF
	screen_width = ui.game_st.map_tile_width + 4;
	screen_height = ui.game_st.map_tile_height + 4;
	ui.create_window = true;
	ui.draw_ui_elements = false;
	ui.game_speed = fp8::integer(8000);
#endif

	if (ui.create_window)
	{
		auto &wnd = ui.wnd;
		wnd.create("Titan Reactor / OpenBW", 0, 0, screen_width, screen_height);
	}

	ui.resize(screen_width, screen_height);

	ui.screen_pos = {(int)ui.game_st.map_width / 2 - (int)screen_width / 2, (int)ui.game_st.map_height / 2 - (int)screen_height / 2};

	ui.set_image_data();

	log("loaded in %dms\n", std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start).count());

	//set_malloc_fail_handler(malloc_fail_handler);
#ifdef TITAN_WRITEGIF
	int numFrames = ui.replay_st.end_frame / 8000;
	int preAllocSize = screen_width * screen_height * 3 * numFrames;

	if (!gifEncoder.open("test.gif", screen_width, screen_height, 30, true, 0, 0))
	{
		log("FAILED");
		fprintf(stderr, "Error open gif file\n");
		return 1;
	}
#endif // TITAN_WRITEGIF

	::g_m = &m;
	while (m.update())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	::g_m = nullptr;

	return 0;
}
