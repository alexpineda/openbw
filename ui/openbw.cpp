
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef TITAN_WRITEGIF
#include "GifEncoder.h"
#endif

#include "boop.h"
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

	sprite_t *sprite_dumps[2500];
	std::map<int, unit_dump_t> unit_dumps;

	void reset_dumps()
	{
		unit_dumps.clear();
	}

	void reset()
	{
		saved_states.clear();
		ui.reset();
		reset_dumps();
	}

	bool update()
	{
		ui.played_sounds.clear();
		ui.deleted_images.clear();
		ui.deleted_sprites.clear();
		ui.deleted_units.clear();
		ui.deleted_bullets.clear();

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
		m->reset_dumps();
		break;
	case 6:
		m->ui.replay_frame = (int)(m->ui.replay_st.end_frame * value);
		if (m->ui.replay_frame < 0)
			m->ui.replay_frame = 0;
		if (m->ui.replay_frame > m->ui.replay_st.end_frame)
			m->ui.replay_frame = m->ui.replay_st.end_frame;
		m->reset_dumps();
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
#define DUMP_RAW(name, value)     \
	o.set(STR(name), val(value)); \
	is_dirty = true;

#define DUMP_VAL(name)                      \
	{                                       \
		auto value = decode(dumping->name); \
		o.set(STR(name), val(value));       \
		is_dirty = true;                    \
	}

#define DUMP_VAL_AS(aka, name)              \
	{                                       \
		auto value = decode(dumping->name); \
		o.set(STR(aka), val(value));        \
		is_dirty = true;                    \
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

	val dump_sound(played_sound_t *dumping)
	{
		val o = val::object();
		o.set("typeId", val(dumping->id));
		o.set("x", val(dumping->x));
		o.set("y", val(dumping->y));
		o.set("unitTypeId", val(dumping->unit_type_id));
		return o;
	}

	auto dump_unit(unit_t *dumping, const bool dirty_check)
	{
		val o = val::object();

		const int unit_id = decode(dumping);
		bool is_dirty = false;
		const auto is_new = std::get<1>(m->unit_dumps.emplace(decode(dumping), unit_dump_t{}));
		const auto in = m->unit_dumps.find(decode(dumping));
		unit_dump_t &out = in->second;

		// always set id
		o.set("_addr", val((size_t)dumping));
		o.set("id", val(unit_id));

		DUMP_RAW(typeId, (int)dumping->unit_type->id);
		DUMP_VAL(owner);
		DUMP_RAW(x, dumping->position.x);
		DUMP_RAW(y, dumping->position.y);
		DUMP_VAL(hp);
		DUMP_VAL(energy);
		DUMP_VAL_AS(shields, shield_points);
		DUMP_RAW(spriteIndex, dumping->sprite->index);
		DUMP_VAL_AS(statusFlags, status_flags);
		DUMP_RAW(direction, direction_index(dumping->heading));

		if (dumping->unit_type->flags & dumping->unit_type->flag_resource && dumping->status_flags & dumping->status_flag_completed)
		{
			// DUMP_RAW(resourceAmount, dumping->building.resource.resource_count);
			DUMP_RAW(remainingBuildtime, 0);
		}
		else
		{
			// DUMP_RAW(resourceAmount, 0);
			DUMP_VAL_AS(remainingBuildtime, remaining_build_time);
		}

		if (dumping->current_build_unit)
		{
			int remainingTrainTime = ((float)dumping->current_build_unit->remaining_build_time / (float)dumping->current_build_unit->unit_type->build_time) * 255;
			DUMP_RAW(remainingTrainTime, remainingTrainTime);
		}
		else
		{
			DUMP_RAW(resourceAmount, 0);
		}

		DUMP_VAL_AS(kills, kill_count);
		DUMP_RAW(order, (int)dumping->order_type->id);
		DUMP_VAL(subunit);
		DUMP_VAL_AS(orderState, order_state);

		// for battle visualizations
		DUMP_VAL_AS(groundWeaponCooldown, ground_weapon_cooldown);
		DUMP_VAL_AS(airWeaponCooldown, air_weapon_cooldown);
		DUMP_VAL_AS(spellCooldown, spell_cooldown);

		// might need this for bullets?
		// o.set("orderTarget", dump_target(&dumping->order_target));

		// for debugging unit tags
		DUMP_VAL(index);
		DUMP_VAL(unit_id_generation);

		// maybe?
		// DUMP_VAL(previous_unit_type);
		// DUMP_VAL(movement_state);
		// DUMP_VAL(last_attacking_player);

		val loaded = val::object();
		for (int i = 0; i < dumping->loaded_units.size(); ++i)
		{
			loaded.set(i, dumping->loaded_units[i].raw_value);
		}
		// o.set("loaded_units", loaded);
		return o;
	}

	uint32_t sprite_depth_order(const sprite_t *sprite) const
	{
		uint32_t score = 0;
		score |= sprite->elevation_level;
		score <<= 13;
		score |= sprite->elevation_level <= 4 ? sprite->position.y : 0;
		score <<= 1;
		score |= s_flag(sprite, sprite_t::flag_turret) ? 1 : 0;
		return score;
	}

	auto sort_sprites()
	{
		a_vector<std::pair<uint32_t, const sprite_t *>> sorted_sprites;

		sorted_sprites.clear();

		for (size_t i = 0; i != st.sprites_on_tile_line.size(); ++i)
		{
			for (sprite_t *sprite : ptr(st.sprites_on_tile_line[i]))
			{
				if (sprite != nullptr)
				{
					if (s_hidden(sprite))
						continue;
					sorted_sprites.emplace_back(sprite_depth_order(sprite), sprite);
				}
			}
		}

		std::sort(sorted_sprites.begin(), sorted_sprites.end());

		return sorted_sprites;
	}

	double worker_supply(int owner)
	{
		double r = 0.0;
		for (const unit_t *u : ptr(st.player_units.at(owner)))
		{
			if (!ut_worker(u))
				continue;
			if (!u_completed(u))
				continue;
			r += u->unit_type->supply_required.raw_value / 2.0;
		}
		return r;
	}

	double army_supply(int owner)
	{
		double r = 0.0;
		for (const unit_t *u : ptr(st.player_units.at(owner)))
		{
			if (ut_worker(u))
				continue;
			if (!u_completed(u))
				continue;
			r += u->unit_type->supply_required.raw_value / 2.0;
		}
		return r;
	}

	auto get_incomplete_units()
	{
		val r = val::array();
		size_t i = 0;
		for (unit_t *u : ptr(st.visible_units))
		{
			if (u_completed(u))
				continue;
			r.set(i++, u);
		}
		for (unit_t *u : ptr(st.hidden_units))
		{
			if (u_completed(u))
				continue;
			r.set(i++, u);
		}
		return r;
	}

	auto get_completed_units()
	{
		val r = val::array();
		size_t i = 0;
		for (unit_t *u : ptr(st.visible_units))
		{
			if (!u_completed(u))
				continue;
			r.set(i++, u);
		}
		for (unit_t *u : ptr(st.hidden_units))
		{
			if (!u_completed(u))
				continue;
			r.set(i++, u);
		}
		return r;
	}

	auto get_units_debug()
	{
		for (auto &i : m->ui.deleted_units)
		{
			m->unit_dumps.erase(i);
		}

		val r = val::array();
		size_t i = 0;
		for (int owner = 0; owner < 12; owner++)
		{
			for (unit_t *u : ptr(st.player_units[owner]))
			{
				r.set(i++, dump_unit(u, false));
			}
		}
		return r;
	}

	auto get_sounds()
	{
		val r = val::array();
		size_t i = 0;
		for (auto sound : ptr(m->ui.played_sounds))
		{
			r.set(i++, dump_sound(sound));
		}
		return r;
	}

	std::pair<val, bool> dump_image(image_t *dumping, const bool dirty_check, int order)
	{
		val o = val::object();
		o.set("index", val(dumping->index));

		bool is_dirty = false;

		o.set("_addr", val((size_t)dumping));
		DUMP_RAW(typeId, (int)dumping->image_type->id);
		DUMP_VAL(flags);

		DUMP_RAW(order, order);
		DUMP_RAW(x, decode(dumping->offset.x));
		DUMP_RAW(y, decode(dumping->offset.y));

		DUMP_VAL(modifier);
		DUMP_VAL_AS(modifierData1, modifier_data1);
		DUMP_VAL_AS(frameIndex, frame_index);
		DUMP_VAL_AS(frameIndexOffset, frame_index_offset);
		DUMP_VAL_AS(frameIndexBase, frame_index_base);
		return std::make_pair(o, is_dirty);
	}

	auto dump_sprite(sprite_t *dumping, const bool dirty_check)
	{

		val o = val::object();
		bool is_dirty = false;

		o.set("index", val(dumping->index));
		o.set("_addr", val((size_t)dumping));

		// DUMP_VAL(index);
		DUMP_RAW(typeId, (int)dumping->sprite_type->id);
		DUMP_VAL(owner);
		DUMP_VAL_AS(elevation, elevation_level);
		DUMP_VAL(flags);
		DUMP_RAW(mainImageIndex, dumping->main_image->index);

		DUMP_RAW(x, decode(dumping->position.x));
		DUMP_RAW(y, decode(dumping->position.y));

		int image_count = 0;
		for (auto image : ptr(dumping->images))
		{
			image_count++;
		}

		val r = val::array();
		size_t i = 0;
		for (auto image : ptr(dumping->images))
		{
			auto img = dump_image(image, dirty_check, image_count - i);
			r.set(i++, std::get<0>(img));
		}
		o.set("images", r);
		return o;
	}

	auto get_sprites_debug()
	{
		val r = val::array();
		size_t x = 0;

		for (size_t i = 0; i != st.sprites_on_tile_line.size(); ++i)
		{
			for (sprite_t *sprite : ptr(st.sprites_on_tile_line[i]))
			{
				if (sprite != nullptr)
				{
					r.set(x++, dump_sprite(sprite, false));
				}
			}
		}

		return r;
	}

	auto get_completed_upgrades(int owner)
	{
		val r = val::array();
		size_t n = 0;
		for (size_t i = 0; i != 61; ++i)
		{
			int level = player_upgrade_level(owner, (UpgradeTypes)i);
			if (level == 0)
				continue;
			val o = val::object();
			o.set("id", val((int)i));
			o.set("icon", val(get_upgrade_type((UpgradeTypes)i)->icon));
			o.set("level", val(level));
			r.set(n++, o);
		}
		return r;
	}

	auto get_completed_research(int owner)
	{
		val r = val::array();
		size_t n = 0;
		for (size_t i = 0; i != 44; ++i)
		{
			if (!player_has_researched(owner, (TechTypes)i))
				continue;
			val o = val::object();
			o.set("id", val((int)i));
			o.set("icon", val(get_tech_type((TechTypes)i)->icon));
			r.set(n++, o);
		}
		return r;
	}

	auto get_incomplete_upgrades(int owner)
	{
		val r = val::array();
		size_t i = 0;
		for (unit_t *u : ptr(st.player_units[owner]))
		{
			if (u->order_type->id == Orders::Upgrade && u->building.upgrading_type)
			{
				val o = val::object();
				o.set("id", val((int)u->building.upgrading_type->id));
				o.set("icon", val((int)u->building.upgrading_type->icon));
				o.set("level", val(u->building.upgrading_level));
				o.set("remaining_time", val(u->building.upgrade_research_time));
				o.set("total_time", val(upgrade_time_cost(owner, u->building.upgrading_type)));
				r.set(i++, o);
			}
		}
		return r;
	}

	auto get_incomplete_research(int owner)
	{
		val r = val::array();
		size_t i = 0;
		for (unit_t *u : ptr(st.player_units[owner]))
		{
			if (u->order_type->id == Orders::ResearchTech && u->building.researching_type)
			{
				val o = val::object();
				o.set("id", val((int)u->building.researching_type->id));
				o.set("icon", val((int)u->building.researching_type->icon));
				o.set("remaining_time", val(u->building.upgrade_research_time));
				o.set("total_time", val(u->building.researching_type->research_time));
				r.set(i++, o);
			}
		}
		return r;
	}

	auto get_deleted_sprites()
	{
		val r = val::array();
		size_t i = 0;
		for (auto &id : m->ui.deleted_sprites)
		{
			r.set(i++, val(id));
		}
		return r;
	}

	auto get_deleted_images()
	{
		val r = val::array();
		size_t i = 0;
		for (auto &id : m->ui.deleted_images)
		{
			r.set(i++, val(id));
		}
		return r;
	}

	auto get_deleted_units()
	{
		val r = val::array();
		size_t i = 0;
		for (auto &id : m->ui.deleted_units)
		{
			r.set(i++, val(id));
		}
		return r;
	}

	auto count_units()
	{
		int unit_count = 0;
		for (int owner = 0; owner < 12; owner++)
		{
			for (unit_t *u : ptr(st.player_units[owner]))
			{
				unit_count++;
			}
		}
		return unit_count;
	}

	auto count_research()
	{
		int research_count = 0;
		for (int owner = 0; owner < 12; owner++)
		{
			for (unit_t *u : ptr(st.player_units[owner]))
			{
				if (u->order_type->id == Orders::ResearchTech && u->building.researching_type)
				{
					research_count++;
				}
			}
		}
		return research_count;
	}

	auto count_upgrades()
	{
		int upgrade_count = 0;
		for (int owner = 0; owner < 12; owner++)
		{
			for (unit_t *u : ptr(st.player_units[owner]))
			{

				if (u->order_type->id == Orders::Upgrade && u->building.upgrading_type)
				{
					upgrade_count++;
				}
			}
		}
		return upgrade_count;
	}

	auto count_images()
	{
		int image_count = 0;
		for (auto &v : sort_sprites())
		{
			for (const image_t *image : ptr(v.second->images))
			{
				image_count++;
			}
		}
		return image_count;
	}

	auto count_building_queue()
	{
		int build_queue_count = 0;
		for (unit_t *u : ptr(st.visible_units))
		{
			if (u->build_queue.size() > 0)
			{
				build_queue_count++;
			}

			// loaded units included in queue count
			for (bwgame::unit_id uid : u->loaded_units)
			{
				if (uid.raw_value != 0)
				{
					build_queue_count++;
					break;
				}
			}
		}
		return build_queue_count;
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
		.function("worker_supply", &util_functions::worker_supply)
		.function("army_supply", &util_functions::army_supply)
		.function("get_incomplete_units", &util_functions::get_incomplete_units, allow_raw_pointers())
		.function("get_completed_units", &util_functions::get_completed_units, allow_raw_pointers())
		.function("get_units_debug", &util_functions::get_units_debug, allow_raw_pointers())
		.function("get_completed_upgrades", &util_functions::get_completed_upgrades)
		.function("get_completed_research", &util_functions::get_completed_research)
		.function("get_incomplete_upgrades", &util_functions::get_incomplete_upgrades)
		.function("get_incomplete_research", &util_functions::get_incomplete_research)
		.function("get_sounds", &util_functions::get_sounds)
		.function("get_sprites_debug", &util_functions::get_sprites_debug, allow_raw_pointers());

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
	default:
		return nullptr;
	}
}

extern "C" int counts(int player, int index)
{
	if (player < 0 || player >= 12)
		return 0;

	switch (index)
	{
	case 0: // tiles
		return m->ui.st.tiles.size();
	case 1: // unit count
		return util_functions(m->ui.st).count_units();
	case 2: // upgrade count
		return util_functions(m->ui.st).count_upgrades();
	case 3: // research count
		return util_functions(m->ui.st).count_research();
	case 4: // sprite count
		return util_functions(m->ui.st).sort_sprites().size();
	case 5: // image count
		return util_functions(m->ui.st).count_images();
	case 6: // sound count
		return (int)m->ui.played_sounds.size();
	case 7: // building queue count
		return util_functions(m->ui.st).count_building_queue();
	case 8:
		return m->ui.st.current_minerals.at(player);
	case 9:
		return m->ui.st.current_gas.at(player);
	case 10:
		return util_functions(m->ui.st).get_fow_size();
	case 12:
		return util_functions(m->ui.st).worker_supply(player);
	case 13:
		return util_functions(m->ui.st).army_supply(player);
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

		if (v > m->ui.fow[i])
		{
			m->ui.fow[i] = std::min(v, m->ui.fow[i] + 10);
		}
		else if (v < m->ui.fow[i])
		{
			m->ui.fow[i] = std::max(v, m->ui.fow[i] - 5);
		}
		i++;
	}
	return m->ui.fow.data();
}

bool any_replay_loaded = false;

extern "C" int next_frame()
{
	m->update();

#ifdef EMSCRIPTEN
	MAIN_THREAD_EM_ASM({ js_callbacks.js_post_main_loop(); });
#endif

	return m->ui.st.current_frame;
}

extern "C" void load_replay(const uint8_t *data, size_t len)
{
	m->reset();
	m->ui.load_replay_data(data, len);
	any_replay_loaded = true;
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
