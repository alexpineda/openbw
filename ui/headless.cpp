
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef TITAN_WRITEGIF
#include "GifEncoder.h"
#endif

//#include "ui.h"
#include "headless_state.h"
#include "common.h"
#include "bwgame.h"
#include "replay.h"
#include "util.h"

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
#ifdef EMSCRIPTEN
			const char *p = str.c_str();
			EM_ASM_({ js_callbacks.js_fatal_error($0); }, p);
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
		ui.played_sounds.clear();
		ui.deleted_images.clear();
		ui.deleted_sprites.clear();

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
		if (!ui.is_done()) {
			int w;
			int h;
			uint32_t* px;
			std::tie(w, h, px) = ui.get_rgba_buffer();
			int delay = 20;
			
			gifEncoder.push(GifEncoder::PIXEL_FORMAT_RGBA, (uint8_t*)px, w, h, delay);

			if (!screen_show_unit && screen_show_unit_cooldown == 0) {
				for (size_t i = 7; i != 0;) {
					--i;
					for (unit_t* u : ptr(ui.st.player_units[i])) {
						if (!ui.unit_visble_on_minimap(u)) continue;
						if (u->air_weapon_cooldown || u->ground_weapon_cooldown) {
							screen_show_unit = u;
							screen_show_unit_cooldown = 5 + std::rand() % 5;
						}
					}
				}
			}
			if (screen_show_unit) {
				unit_t* u = screen_show_unit;
				if (screen_show_unit_cooldown) {
					ui.screen_pos = xy(u->position.x - ui.view_width / 2,  u->position.y - ui.view_height / 2);
					ui.draw_ui_minimap = false;
				}
				else {
					ui.draw_ui_minimap = true;
					screen_show_unit = NULL;
					screen_show_unit_cooldown = 10 + std::rand() % 5;
				}
			}
			if (screen_show_unit_cooldown) {
				screen_show_unit_cooldown--;
			}
		}
		else {
			log("saving gif");
			if (!gifEncoder.close()) {
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
	EM_ASM_({ js_callbacks.js_fatal_error($0); }, p);
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
				return EM_ASM_INT({ return js_callbacks.js_file_index($0); }, filename.data());
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

				EM_ASM({ js_callbacks.js_read_data($0, $1, $2, $3); }, index, dst, file_pointer, n);
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
				return EM_ASM_INT({ return js_callbacks.js_file_size($0); }, index);
			}
		};

	} // namespace data_loading
} // namespace bwgame

main_t *m;

int current_width = -1;
int current_height = -1;

struct unit_dump_t
{
	int id;
	int typeId;
	int owner;
	int x;
	int y;
	double hp;
	double energy;
	double shields;
	uint32_t spriteIndex;
	int statusFlags;
	size_t direction;
	int resourceAmount;
	int remainingBuildtime;
	int remainingTraintime;
	int kills;
	int order;
	int subunit;
	int orderState;
	int groundWeaponCooldown;
	int airWeaponCooldown;
	int spellCooldown;
	size_t index;
	unsigned int unit_id_generation;
	int remainingTrainTime;
};

std::map<int, unit_dump_t> unit_dumps;

struct image_dump_t
{
	size_t index;
	uint32_t titanIndex;
	int typeId;
	int flags;
	int x; //pos
	int y; //pos
	int modifier;
	int modifierData1;
	int order;
	size_t frameIndex;
};

std::map<int, image_dump_t> image_dumps;

struct sprite_dump_t
{
	size_t index;
	uint32_t titanIndex;
	int owner;
	int typeId;
	int selection_index;
	int visibility_flags;
	int elevation_level;
	int flags;
	int selection_timer;
	int width;
	int height;
	int x; //pos
	int y; //pos
	int mainImageIndex;
};

std::map<int, sprite_dump_t> sprite_dumps;

void reset_dumps()
{
	unit_dumps.clear();
	sprite_dumps.clear();
	image_dumps.clear();
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
		reset_dumps();
		break;
	case 6:
		m->ui.replay_frame = (int)(m->ui.replay_st.end_frame * value);
		if (m->ui.replay_frame < 0)
			m->ui.replay_frame = 0;
		if (m->ui.replay_frame > m->ui.replay_st.end_frame)
			m->ui.replay_frame = m->ui.replay_st.end_frame;
		reset_dumps();
		break;
	}
}

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
#define DUMP_RAW(name, value)                        \
	if (is_new || !dirty_check || out.name != value) \
	{                                                \
		o.set(STR(name), val(value));                \
		out.name = value;                            \
		is_dirty = true;                             \
	}

#define DUMP_VAL(name)                                   \
	{                                                    \
		auto value = decode(dumping->name);              \
		if (is_new || !dirty_check || out.name != value) \
		{                                                \
			o.set(STR(name), val(value));                \
			out.name = value;                            \
			is_dirty = true;                             \
		}                                                \
	}

#define DUMP_VAL_AS(aka, name)                          \
	{                                                   \
		auto value = decode(dumping->name);             \
		if (is_new || !dirty_check || out.aka != value) \
		{                                               \
			o.set(STR(aka), val(value));                \
			out.aka = value;                            \
			is_dirty = true;                            \
		}                                               \
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

	std::pair<val, bool> dump_image(image_t *dumping, const bool dirty_check, int order)
	{
		val o = val::object();
		bool is_dirty = false;
		const auto is_new = std::get<1>(image_dumps.emplace(dumping->index, image_dump_t{}));
		const auto in = image_dumps.find(dumping->index);
		image_dump_t &out = in->second;

		o.set("index", val(dumping->index));
		DUMP_RAW(typeId, (int)dumping->image_type->id);
		DUMP_VAL(flags);

		DUMP_RAW(order, order);
		DUMP_RAW(x, decode(dumping->offset.x));
		DUMP_RAW(y, decode(dumping->offset.y));

		DUMP_VAL(modifier);
		DUMP_VAL_AS(modifierData1, modifier_data1);
		DUMP_VAL_AS(frameIndex, frame_index);
		return std::make_pair(o, is_dirty);
	}

	std::pair<val, bool> dump_sprite(sprite_t *dumping, const bool dirty_check)
	{

		val o = val::object();
		bool is_dirty = false;
		const auto is_new = std::get<1>(sprite_dumps.emplace(dumping->index, sprite_dump_t{}));
		const auto in = sprite_dumps.find(dumping->index);
		sprite_dump_t &out = in->second;

		o.set("index", val(dumping->index));

		// DUMP_VAL(index);
		DUMP_RAW(typeId, (int)dumping->sprite_type->id);
		DUMP_VAL(owner);
		DUMP_VAL(selection_index);
		DUMP_VAL(visibility_flags);
		DUMP_VAL(elevation_level);
		DUMP_VAL(flags);
		DUMP_VAL(selection_timer);
		DUMP_VAL(width);
		DUMP_VAL(height);
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
			if (dirty_check && !img.second)
			{
				continue;
			}
			r.set(i++, std::get<0>(img));
			is_dirty = true;
		}
		o.set("images", r);

		return std::make_pair(o, is_dirty);
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

	std::pair<val, bool> dump_unit(unit_t *dumping, const bool dirty_check)
	{
		val o = val::object();

		const int unit_id = decode(dumping);
		bool is_dirty = false;
		const auto is_new = std::get<1>(unit_dumps.emplace(unit_id, unit_dump_t{}));
		const auto in = unit_dumps.find(unit_id);
		unit_dump_t &out = in->second;

		//always set id
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

		{
			// int d = direction_index(dumping->heading);
			// d -= 64;
			// if (d < 0)
			// 	d += 256;
			// DUMP_RAW(angle, (double)d * 3.14159265358979323846 / 128.0);
		}

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

		//		prev
		// DUMP_VAL(main_order_timer);
		// DUMP_VAL(auto_target_unit);
		// DUMP_VAL(connected_unit);
		// DUMP_VAL(order_queue_count);
		// DUMP_VAL(order_process_timer);
		// DUMP_VAL(unknown_0x086);
		// DUMP_VAL(attack_notify_timer);
		// DUMP_VAL(last_event_timer);
		// DUMP_VAL(last_event_color);
		// DUMP_VAL(rank_increase);
		// DUMP_VAL(secondary_order_timer);
		// DUMP_VAL(user_action_flags);
		// DUMP_VAL(cloak_counter);
		// DUMP_VAL(previous_hp);

		val loaded = val::object();
		for (int i = 0; i < dumping->loaded_units.size(); ++i)
		{
			loaded.set(i, dumping->loaded_units[i].raw_value);
		}
		// o.set("loaded_units", loaded);
		// o.set("_flingy_t", dump_flingy(dumping));
		return std::make_pair(o, is_dirty);
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

	auto get_units(const bool dirty_check = true)
	{
		val r = val::array();
		size_t i = 0;
		for (int owner = 0; owner < 12; owner++)
		{
			for (unit_t *u : ptr(st.player_units[owner]))
			{
				auto o = dump_unit(u, dirty_check);
				if (std::get<1>(o))
				{
					r.set(i++, std::get<0>(o));
				}
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

	auto get_sprites(const bool dirty_check = true)
	{
		for (auto &i : m->ui.deleted_sprites)
		{
			sprite_dumps.erase(i);
		}

		for (auto &i : m->ui.deleted_images)
		{
			image_dumps.erase(i);
		}

		val r = val::array();
		int i = 0;

		auto sprites = sort_sprites();
		for (auto &sprite : sprites)
		{
			auto o = dump_sprite((sprite_t *)sprite.second, dirty_check);
			if (dirty_check && !o.second)
			{
				continue;
			}
			r.set(i++, o.first);
		}

		return r;
	}

	auto get_images()
	{
		val r = val::array();
		size_t i = 0;
		for (image_t *u : ptr(st.images_container.free_list))
		{
			r.set(i++, u);
		}
		return r;
	}

	auto get_bullets()
	{
		val r = val::array();
		size_t i = 0;
		for (bullet_t *u : ptr(st.active_bullets))
		{
			r.set(i++, u);
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
	// std::vector<unitFrameData> units;

	// auto get_units()
	// {
	// 	units.clear();
	// 	for (int owner = 0; owner < 12; owner++)
	// 	{
	// 		for (unit_t *u : ptr(st.player_units[owner]))
	// 		{
	// 			unitFrameData unitData;

	// 			unitData.id = get_unit_id(u).raw_value;
	// 			unitData.owner = u->owner;
	// 			unitData.typeId = (short int)u->unit_type->id;
	// 			unitData.hp = (short int)u->hp.integer_part();
	// 			unitData.energy = (short int)u->energy.integer_part();
	// 			unitData.shields = (short int)u->shield_points.integer_part();
	// 			unitData.sprite_index = u->sprite->index;
	// 			unitData.status_flags = u->status_flags;
	// 			unitData.x = u->position.x;
	// 			unitData.y = u->position.y;
	// 			unitData.direction = direction_index(u->heading);
	// 			unitData.remainingBuildTime = u->remaining_build_time;

	// 			if (u_completed(u) && ut_resource(u->unit_type))
	// 			{
	// 				unitData.remainingBuildTime = u->building.resource.resource_count;
	// 			}

	// 			unitData.order = (unsigned char)u->order_type->id;
	// 			if (u->current_build_unit)
	// 			{
	// 				unitData.remainingTrainTime = ((float)u->current_build_unit->remaining_build_time / (float)u->current_build_unit->unit_type->build_time) * 255;
	// 			}
	// 			unitData.kills = u->kill_count;

	// 			units.push_back(unitData);
	// 		}
	// 	}
	// 	return units;
	// }

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
};

optional<util_functions> m_util_funcs;

util_functions &get_util_funcs()
{
	m_util_funcs.emplace(m->ui.st);
	return *m_util_funcs;
}

const unit_type_t *unit_t_unit_type(const unit_t *u)
{
	return u->unit_type;
}
const unit_type_t *unit_t_build_type(const unit_t *u)
{
	if (u->build_queue.empty())
		return nullptr;
	return u->build_queue.front();
}

const sprite_t *thingy_t_sprite(const thingy_t *t)
{
	return t->sprite;
}

const int_fast32_t thingy_t_hp(const thingy_t *t)
{
	return t->hp.integer_part();
}

const image_t *sprite_t_main_image(const sprite_t *s)
{
	return s->main_image;
}

const int image_t_image_type(const image_t *i)
{
	return (int)i->image_type->id;
}

const iscript_state_t image_t_iscript_state(const image_t *i)
{
	return i->iscript_state;
}

const auto sprite_t_images(const sprite_t *s)
{
	val r = val::array();
	size_t i = 0;
	for (const image_t *u : ptr(s->images))
	{
		r.set(i++, u);
	}
	return r;
}

int unit_type_t_id(const unit_type_t &ut)
{
	return (int)ut.id;
}

int iscript_state_t_current_script(const iscript_state_t *i)
{
	return (int)i->current_script->id;
}

std::string getExceptionMessage(int exceptionPtr)
{
	return std::string(reinterpret_cast<std::exception *>(exceptionPtr)->what());
}

EMSCRIPTEN_BINDINGS(openbw)
{
	register_vector<js_unit>("vector_js_unit");

	function("getExceptionMessage", &getExceptionMessage);

	// function("lookup_unit_extended", &lookup_unit_extended);
	// function("get_units", &util_functions::get_units);

	class_<util_functions>("util_functions")
		.function("worker_supply", &util_functions::worker_supply)
		.function("army_supply", &util_functions::army_supply)
		.function("get_incomplete_units", &util_functions::get_incomplete_units, allow_raw_pointers())
		.function("get_completed_units", &util_functions::get_completed_units, allow_raw_pointers())
		.function("get_units", &util_functions::get_units, allow_raw_pointers())
		.function("get_completed_upgrades", &util_functions::get_completed_upgrades)
		.function("get_completed_research", &util_functions::get_completed_research)
		.function("get_incomplete_upgrades", &util_functions::get_incomplete_upgrades)
		.function("get_incomplete_research", &util_functions::get_incomplete_research)
		.function("get_sprites", &util_functions::get_sprites)
		.function("get_images", &util_functions::get_images)
		.function("get_bullets", &util_functions::get_bullets, allow_raw_pointers())
		.function("get_sounds", &util_functions::get_sounds)
		.function("get_deleted_sprites", &util_functions::get_deleted_sprites)
		.function("get_deleted_images", &util_functions::get_deleted_images);

	function("get_util_funcs", &get_util_funcs);
}

// return m->ui.st.players.at(player).controller == player_t::controller_occupied ? 1 : 0;
// 	case 14:
// 		return (double)m->ui.apm.at(player).current_apm;

extern "C" char *get_buffer(int index)
{
	switch (index)
	{
	case 0: //tiles + creep (t->flags & tile_t::flag_has_creep);
		return reinterpret_cast<char *>(&m->ui.st.tiles.data()[0]);
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
	case 0: //tiles
		return m->ui.st.tiles.size();
	case 1: //unit count
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
		return 0; // @todo implement
	// case 10:
	// 	return m->ui.st.supply_used.at(player)[0].raw_value / 2.0;
	// case 10:
	// 	return m->ui.st.supply_used.at(player)[1].raw_value / 2.0;
	// case 10:
	// 	return m->ui.st.supply_used.at(player)[2].raw_value / 2.0;
	case 11:
		return 0; // @todo implement
				  // case 11:
				  // 	return std::min(m->ui.st.supply_available.at(player)[0].raw_value / 2.0, 200.0);
				  // case 11:
				  // 	return std::min(m->ui.st.supply_available.at(player)[1].raw_value / 2.0, 200.0);
				  // case 11:
				  // 	return std::min(m->ui.st.supply_available.at(player)[2].raw_value / 2.0, 200.0);

	case 12:
		return util_functions(m->ui.st).worker_supply(player);
	case 13:
		return util_functions(m->ui.st).army_supply(player);
	default:
		return 0;
	}
}

bool any_replay_loaded = false;

extern "C" int next_frame_exact()
{
	//@todo clear states
	m->ui.next_frame();

#ifdef EMSCRIPTEN
	EM_ASM({ js_callbacks.js_post_main_loop(); });
#endif

	return m->ui.st.current_frame;
}

extern "C" int next_frame()
{
	m->update();

#ifdef EMSCRIPTEN
	EM_ASM({ js_callbacks.js_post_main_loop(); });
#endif

	return m->ui.st.current_frame;
}

extern "C" void load_replay(const uint8_t *data, size_t len)
{
	m->reset();
	reset_dumps();
	m->ui.load_replay_data(data, len);
	any_replay_loaded = true;
}

int main()
{
	using namespace bwgame;

	log("openbw-build: 30\n");

	std::chrono::high_resolution_clock clock;
	auto start = clock.now();

#ifdef EMSCRIPTEN
	auto load_data_file = data_loading::simple_reader<data_loading::js_file_reader<>>();
#else
	auto load_data_file = data_loading::data_files_directory("D:\\dev\\openbw\\openbw-original\\openbw-original\\Debug\\");
#endif
	log("files loaded\n");

	game_player player(load_data_file);

	main_t m(std::move(player));

	// m.ui.load_all_image_data(load_data_file);

	// ui.load_data_file = [&](a_vector<uint8_t> &data, a_string filename) {
	// 	load_data_file(data, std::move(filename));
	// };

	// ui.init();

	log("loaded in %dms\n", std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start).count());

#ifdef EMSCRIPTEN
	::m = &m;
	::g_m = &m;
	MAIN_THREAD_EM_ASM({ js_callbacks.js_load_done(); });
	emscripten_exit_with_live_runtime();
#endif

	::g_m = nullptr;
	while (m.update())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return 0;
}
