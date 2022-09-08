
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef TITAN_WRITEGIF
#include "GifEncoder.h"
#endif

#include "titan-reactor.h"
#include "common.h"
#include "bwgame.h"
#include "replay.h"
#include "util.h"
#include "titan_util.h"

#include <chrono>
#include <thread>
#include <algorithm>

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
#ifdef EMSCRIPTEN
			const char *p = str.c_str();
			MAIN_THREAD_EM_ASM({ js_callbacks.js_fatal_error($0); }, p);
#endif
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
	titan_replay_functions ui;
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
		ui.clear_frame();

		auto now = clock.now();

		auto tick_speed = std::chrono::milliseconds((fp8::integer(42) / ui.game_speed).integer_part());

		if (now - last_fps >= std::chrono::seconds(1))
		{
			// log("game fps: %g\n", fps_counter / std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 1>>>(now - last_fps).count());
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

#ifndef TITAN_HEADLESS
		ui.update();
#endif

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
#ifdef EMSCRIPTEN
	const char *p = "out of memory :(";
	MAIN_THREAD_EM_ASM({ js_callbacks.js_fatal_error($0); }, p);
#endif
	throw std::bad_alloc();
}

size_t bytes_allocated = 0;

void free_memory()
{
	if (!g_m)
		out_of_memory();
	size_t n_states = g_m->saved_states.size();
	printf("n_states is %zu\n", n_states);
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

namespace bwgame
{
	namespace data_loading
	{

		template <typename file_reader_T = file_reader<>>
		struct simple_reader
		{
			void operator()(a_vector<uint8_t> &dst, a_string filename)
			{
				file_reader_T file = file_reader_T(std::move(filename));
				size_t len = file.size();
				dst.resize(len);
				file.get_bytes(dst.data(), len);
				return;
			}
		};

#ifdef EMSCRIPTEN
		template <bool default_little_endian = true>
		struct js_file_reader
		{
			a_string filename;
			size_t index = ~(size_t)0;
			size_t file_pointer = 0;
			js_file_reader() = default;
			explicit js_file_reader(a_string filename)
			{
				open(std::move(filename));
			}

			int get_file_index(a_string filename)
			{
				return MAIN_THREAD_EM_ASM_INT({ return js_callbacks.js_file_index($0); }, filename.data());
			}

			void open(a_string filename)
			{
				index = get_file_index(filename);
				if (index == 9999)
				{
					ui::xcept("js_file_reader: unknown filename '%s'", filename);
				}
				this->filename = std::move(filename);
			}

			void get_bytes(uint8_t *dst, size_t n)
			{

				MAIN_THREAD_EM_ASM({ js_callbacks.js_read_data($0, $1, $2, $3); }, index, dst, file_pointer, n);
				file_pointer += n;
			}

			void seek(size_t offset)
			{
				file_pointer = offset;
			}
			size_t tell() const
			{
				return file_pointer;
			}

			size_t size()
			{
				return MAIN_THREAD_EM_ASM_INT({ return js_callbacks.js_file_size($0); }, index);
			}
		};
#endif

	} // namespace data_loading
} // namespace bwgame

main_t *m;

int current_width = -1;
int current_height = -1;
extern "C" void ui_resize(int width, int height)
{
	if (width == current_width && height == current_height)
		return;
	if (width <= 0 || height <= 0)
		return;
	current_width = width;
	current_height = height;
	if (!m)
		return;
	m->ui.window_surface.reset();
	m->ui.indexed_surface.reset();
	m->ui.rgba_surface.reset();
	m->ui.wnd.destroy();
	m->ui.wnd.create("test", 0, 0, width, height);
	m->ui.resize(width, height);
}

extern "C" double replay_get_value(int index)
{
	switch (index)
	{
	case 0:
		return m->ui.game_speed.raw_value / 256.0;
	case 1:
		return m->ui.is_paused ? 1 : 0;
	case 2:
		return (double)m->ui.st.current_frame;
	case 3:
		return (double)m->ui.replay_frame;
	case 4:
		return (double)m->ui.replay_st.end_frame;
	case 5:
		return (double)(uintptr_t)m->ui.replay_st.map_name.data();
	case 6:
		return (double)m->ui.replay_frame / m->ui.replay_st.end_frame;
	default:
		return 0;
	}
}

extern "C" void replay_set_value(int index, double value)
{
	switch (index)
	{
	case 0:
		m->ui.game_speed.raw_value = (int)(value * 256.0);
		if (m->ui.game_speed < 1_fp8)
			m->ui.game_speed = 1_fp8;
		break;
	case 1:
		m->ui.is_paused = value != 0.0;
		break;
	case 3:
		m->ui.replay_frame = (int)value;
		if (m->ui.replay_frame < 0)
			m->ui.replay_frame = 0;
		if (m->ui.replay_frame > m->ui.replay_st.end_frame)
			m->ui.replay_frame = m->ui.replay_st.end_frame;
		break;
	case 6:
		m->ui.replay_frame = (int)(m->ui.replay_st.end_frame * value);
		if (m->ui.replay_frame < 0)
			m->ui.replay_frame = 0;
		if (m->ui.replay_frame > m->ui.replay_st.end_frame)
			m->ui.replay_frame = m->ui.replay_st.end_frame;
		break;
	}
}

#ifdef EMSCRIPTEN

#include <emscripten/bind.h>
#include <emscripten/val.h>
using namespace emscripten;

struct js_unit_type
{
	const unit_type_t *ut = nullptr;
	js_unit_type() {}
	js_unit_type(const unit_type_t *ut) : ut(ut) {}
	auto id() const { return ut ? (int)ut->id : 228; }
	auto build_time() const { return ut->build_time; }
};

struct js_unit
{
	unit_t *u = nullptr;
	js_unit() {}
	js_unit(unit_t *u) : u(u) {}
	auto owner() const { return u->owner; }
	auto remaining_build_time() const { return u->remaining_build_time; }
	auto unit_type() const { return u->unit_type; }
	auto build_type() const { return u->build_queue.empty() ? nullptr : u->build_queue.front(); }
};

struct util_functions : state_functions
{
	util_functions(state &st) : state_functions(st) {}

#define STR(a) #a
#define DUMP_RAW(name, value) \
	o.set(STR(name), val(value));

#define DUMP_VAL(name)                      \
	{                                       \
		auto value = decode(dumping->name); \
		o.set(STR(name), val(value));       \
	}

#define DUMP_VAL_AS(aka, name)              \
	{                                       \
		auto value = decode(dumping->name); \
		o.set(STR(aka), val(value));        \
	}

	template <typename T>
	T decode(T &v)
	{
		return v;
	}

	double decode(fp8 &v)
	{
		double as_double = v.raw_value;
		as_double /= (1 << v.fractional_bits);
		return as_double;
	}

	template <typename T>
	int decode(id_type_for<T> *v)
	{
		if (!v)
		{
			return -1;
		}
		return (int)v->id;
	}

	int decode(unit_t *unit)
	{
		util_functions f(m->ui.st);
		if (!unit)
		{
			return -1;
		}
		return f.get_unit_id(unit).raw_value;
	}

	// todo dump debug addresses
	auto dump_unit(int id)
	{
		val o = val::object();

		unit_t *dumping = get_unit(unit_id(id));

		if (!dumping)
		{
			return o;
		}

		const int unit_id = decode(dumping);
		o.set("id", val(unit_id));

		if (dumping->unit_type->flags & dumping->unit_type->flag_resource && dumping->status_flags & dumping->status_flag_completed)
		{
			DUMP_RAW(resourceAmount, dumping->building.resource.resource_count);
		}

		if (dumping->current_build_unit)
		{
			o.set("remainingTrainTime", (float)dumping->current_build_unit->remaining_build_time / (float)dumping->current_build_unit->unit_type->build_time);
		}

		if (dumping->order_type->id == Orders::Upgrade && dumping->building.upgrading_type)
		{
			val upgrade = val::object();
			upgrade.set("id", val((int)dumping->building.upgrading_type->id));
			upgrade.set("level", val(dumping->building.upgrading_level));
			upgrade.set("time", val(dumping->building.upgrade_research_time));
			o.set("upgrade", upgrade);
		}
		else if (dumping->order_type->id == Orders::ResearchTech && dumping->building.researching_type)
		{
			val research = val::object();
			research.set("id", val((int)dumping->building.researching_type->id));
			research.set("time", val(dumping->building.upgrade_research_time));
			o.set("research", research);
		}

		int j = 0;
		val loaded = val::array();

		for (int i = 0; i < dumping->loaded_units.size(); ++i)
		{
			if (dumping->loaded_units[i].raw_value > 0)
			{
				unit_t *u = get_unit(dumping->loaded_units[i]);
				if (u)
				{
					val uo = val::object();
					uo.set("id", (int)dumping->loaded_units[i].raw_value);
					uo.set("typeId", (int)u->unit_type->id);
					uo.set("hp", decode(u->hp));
					loaded.set(j, uo);
					j++;
				}
			}
		}
		if (j > 0)
		{
			o.set("loaded", loaded);
		}

		if (dumping->build_queue.size() > 0)
		{
			val queue = val::array();

			int i = 0;
			for (const unit_type_t *ut : dumping->build_queue)
			{
				queue.set(i, val((int)ut->id));
				i++;
			}
			o.set("buildQueue", queue);
		}
		return o;
	}

	int get_fow_size()
	{
		if (m->ui.fow.size() != m->ui.st.tiles.size())
		{
			m->ui.fow.resize(m->ui.st.tiles.size());
			for (int i = 0; i < m->ui.fow.size(); i++)
			{
				m->ui.fow[i] = 0;
			}
		}
		return m->ui.fow.size();
	}
};

optional<util_functions> m_util_funcs;

util_functions &get_util_funcs()
{
	m_util_funcs.emplace(m->ui.st);
	return *m_util_funcs;
}

std::string getExceptionMessage(int exceptionPtr)
{
	return std::string(reinterpret_cast<std::exception *>(exceptionPtr)->what());
}

EMSCRIPTEN_BINDINGS(openbw)
{
	register_vector<js_unit>("vector_js_unit");

	function("getExceptionMessage", &getExceptionMessage);

	class_<util_functions>("util_functions")
		.function("dump_unit", &util_functions::dump_unit, allow_raw_pointers());

	function("get_util_funcs", &get_util_funcs);
}

// return m->ui.st.players.at(player).controller == player_t::controller_occupied ? 1 : 0;
// 	case 14:
// 		return (double)m->ui.apm.at(player).current_apm;

extern "C" void *get_buffer(int index)
{
	switch (index)
	{
	case 0:
		return reinterpret_cast<void *>(m->ui.st.tiles.data());
	case 1:
		return reinterpret_cast<void *>(m->ui.st.sprites_on_tile_line.data());
	case 2:
		return reinterpret_cast<void *>(m->ui.st.player_units.data());
	case 3:
		return reinterpret_cast<void *>(m->ui.deleted_images.data());
	case 4:
		return reinterpret_cast<void *>(m->ui.deleted_sprites.data());
	case 5:
		return reinterpret_cast<void *>(m->ui.deleted_units.data());
	case 6:
		return reinterpret_cast<void *>(&m->ui.st.active_bullets);
	case 7:
		return reinterpret_cast<void *>(m->ui.deleted_bullets.data());
	case 8: // player data
		m->ui.generate_player_data();
		return reinterpret_cast<void *>(m->ui.player_data.data());
	case 9:
		m->ui.generate_production_data();
		return reinterpret_cast<void *>(m->ui.production_data.data());
	case 10:
		return reinterpret_cast<void *>(m->ui.linked_sprites.data());
	case 11:
		return reinterpret_cast<void *>(m->ui.played_sounds.data());
	case 12:
		return reinterpret_cast<void *>(m->ui.global_st.iscript.program_data.size());
	default:
		return nullptr;
	}
}

extern "C" int counts(int index)
{
	switch (index)
	{
	case 0: // tiles
		return m->ui.st.tiles.size();
	case 1: // linked sprites
		return m->ui.linked_sprites.size();
	case 2: // upgrade count
		return 0;
	case 3: // research count
		return 0;
	case 4: // sprite count
		return 0;
	case 5: // image count
		return 0;
	case 6: // sound count
		return (int)m->ui.played_sounds.size();
	case 7: // building queue count
		return 0;
	case 8:
		return 0;
	case 9:
		return 0;
	case 10:
		return util_functions(m->ui.st).get_fow_size();
	case 12:
		return m->ui.global_st.iscript.program_data.size();
	case 13:
		return 0;
	case 14:
		return m->ui.st.sprites_on_tile_line.size();
	case 15:
		return m->ui.deleted_images.size();
	case 16:
		return m->ui.deleted_sprites.size();
	case 17:
		return m->ui.deleted_units.size();
	case 18: // deleted bullets
		return m->ui.deleted_bullets.size();
	default:
		return 0;
	}
}

extern "C" uint8_t *get_fow_ptr(uint8_t player_visibility, bool instant)
{
	int i = 0;
	for (auto &tile : m->ui.st.tiles)
	{
		int v = 15;

		if (~tile.explored & player_visibility)
		{
			v = 55;
		}
		if (~tile.visible & player_visibility)
		{
			v = 255;
		}

		if (instant)
		{
			m->ui.fow[i] = v;
		}
		else
		{
			if (v > m->ui.fow[i])
			{
				m->ui.fow[i] = std::min(v, m->ui.fow[i] + 10);
			}
			else if (v < m->ui.fow[i])
			{
				m->ui.fow[i] = std::max(v, m->ui.fow[i] - 5);
			}
		}

		i++;
	}
	return m->ui.fow.data();
}


extern "C" int next_frame()
{
	m->update();
	return m->ui.st.current_frame;
}

extern "C" void load_replay(const uint8_t *data, size_t len)
{
	m->reset();
	m->ui.load_replay_data(data, len);
}

extern "C" void load_map( uint8_t *data, size_t len)
{
	m->reset();
	game_load_functions game_load_funcs(m->ui.st);
	game_load_funcs.load_map_data(data, len); 
}

extern "C" void create_unit(int unit_type, int player, int x, int y)
{
	m->ui.create_unit(unit_type, player, x, y);
}

extern "C" void  next_no_replay()
{
	m->ui.next_no_replay();
}
#endif

int main()
{
	using namespace bwgame;

	log("openbw-build: 31\n");

	std::chrono::high_resolution_clock clock;
	auto start = clock.now();

	size_t screen_width = 1280;
	size_t screen_height = 800;

#ifdef EMSCRIPTEN
	auto load_data_file = data_loading::simple_reader<data_loading::js_file_reader<>>();
#else
	auto load_data_file = data_loading::data_files_directory("D:\\dev\\openbw\\openbw-original\\openbw-original\\Debug\\");
#endif

	game_player player(load_data_file);

	main_t m(std::move(player));

	auto &ui = m.ui;

#ifndef EMSCRIPTEN
	ui.load_replay_file("D:\\last_replay.rep");
#endif

#ifdef TITAN_WRITEGIF
	screen_width = ui.game_st.map_tile_width + 4;
	screen_height = ui.game_st.map_tile_height + 4;
	ui.create_window = true;
	ui.game_speed = fp8::integer(8000);
	int numFrames = ui.replay_st.end_frame / 8000;
	int preAllocSize = screen_width * screen_height * 3 * numFrames;

	if (!gifEncoder.open("test.gif", screen_width, screen_height, 30, true, 0, 0))
	{
		log("FAILED");
		fprintf(stderr, "Error open gif file\n");
		return 1;
	}
#endif // TITAN_WRITEGIF

#ifndef TITAN_HEADLESS

	m.ui.load_all_image_data(load_data_file);

	ui.load_data_file = [&](a_vector<uint8_t> &data, a_string filename)
	{
		load_data_file(data, std::move(filename));
	};

	ui.init();

	if (ui.create_window)
	{
		auto &wnd = ui.wnd;
		wnd.create("Titan Reactor / OpenBW", 0, 0, screen_width, screen_height);
	}

	ui.resize(screen_width, screen_height);

	ui.screen_pos = {(int)ui.game_st.map_width / 2 - (int)screen_width / 2, (int)ui.game_st.map_height / 2 - (int)screen_height / 2};

	ui.set_image_data();
#endif

	log("loaded in %dms\n", std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start).count());

#ifdef EMSCRIPTEN
	::m = &m;
	::g_m = &m;
	MAIN_THREAD_EM_ASM({ js_callbacks.js_load_done(); });
	emscripten_exit_with_live_runtime();
#else
	::g_m = &m;
	while (m.update())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
#endif
	::g_m = nullptr;

	return 0;
}
