
// based on openbw/gfxtest.cpp

#include <emscripten.h>
#include "headless_state.h"
#include "common.h"
#include "bwgame.h"
#include "replay.h"

#include <chrono>
#include <thread>

using namespace bwgame;

using ui::log;

FILE *log_file = nullptr;

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
			const char *p = str.c_str();
			EM_ASM_({ js_fatal_error($0); }, p);
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
	const char *p = "out of memory :(";
	EM_ASM_({ js_fatal_error($0); }, p);
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

// EMSCRIPTEN
namespace bwgame
{
	namespace data_loading
	{

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
			void open(a_string filename)
			{
				if (filename == "StarDat.mpq")
					index = 0;
				else if (filename == "BrooDat.mpq")
					index = 1;
				else if (filename == "Patch_rt.mpq")
					index = 2;
				else
					ui::xcept("js_file_reader: unknown filename '%s'", filename);
				this->filename = std::move(filename);
			}

			void get_bytes(uint8_t *dst, size_t n)
			{
				EM_ASM_({ js_read_data($0, $1, $2, $3); }, index, dst, file_pointer, n);
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
				return EM_ASM_INT({ return js_file_size($0); }, index);
			}
		};

	} // namespace data_loading
} // namespace bwgame

main_t *m;

int current_width = -1;
int current_height = -1;

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

	auto get_all_incomplete_units()
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

	auto get_all_completed_units()
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

	auto get_all_units()
	{
		val r = val::array();
		size_t i = 0;
		for (unit_t *u : ptr(st.visible_units))
		{
			r.set(i++, u);
		}
		for (unit_t *u : ptr(st.hidden_units))
		{
			r.set(i++, u);
		}
		for (unit_t *u : ptr(st.map_revealer_units))
		{
			r.set(i++, u);
		}
		return r;
	}

	auto get_all_active_thingies()
	{
		val r = val::array();
		size_t i = 0;
		for (thingy_t *u : ptr(st.active_thingies))
		{
			r.set(i++, u);
		}
		return r;
	}

	auto get_all_sprites()
	{
		val r = val::array();
		size_t i = 0;
		for (sprite_t *u : ptr(st.sprites_container.free_list))
		{
			r.set(i++, u);
		}
		return r;
	}

	auto get_all_images()
	{
		val r = val::array();
		size_t i = 0;
		for (image_t *u : ptr(st.images_container.free_list))
		{
			r.set(i++, u);
		}
		return r;
	}

	auto get_all_active_bullets()
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

	auto get_all_tiles()
	{
		val r = val::array();
		size_t i = 0;
		for (tile_t *u : ptr(st.tiles))
		{
			r.set(i++, u);
		}
		return r;
	}
};

optional<util_functions> m_util_funcs;

double direction_t_to_double(direction_t &v)
{
	double as_double = v.raw_value;
	as_double /= (1 << v.fractional_bits);
	return as_double;
}

double fp8_to_double(fp8 &v)
{
	double as_double = v.raw_value;
	as_double /= (1 << v.fractional_bits);
	return as_double;
}

val dump_pos(xy &pos)
{
	val o = val::object();
	o.set("x", pos.x);
	o.set("y", pos.y);
	return o;
}

val dump_pos_fp8(xy_fp8 &pos)
{
	val o = val::object();
	o.set("x", fp8_to_double(pos.x));
	o.set("y", fp8_to_double(pos.y));
	return o;
}

class Dump
{
public:
#define STR(a) #a
#define DUMP_VAL(name) o.set(STR(name), to_emscripten(dumping->name))
#define DUMP_POS(field) o.set(STR(field), dump_pos(dumping->field))

	template <typename T>
	static val to_emscripten(T &v)
	{
		return val(v);
	}
	static val to_emscripten(fp8 &v)
	{
		double as_double = v.raw_value;
		as_double /= (1 << v.fractional_bits);
		return val(as_double);
	}
	template <typename T>
	static val to_emscripten(id_type_for<T> *v)
	{
		if (!v)
		{
			return val::null();
		}
		return val((int)v->id);
	}

	static val to_emscripten(unit_t *unit)
	{
		util_functions f(m->ui.st);
		if (!unit)
		{
			return val::null();
		}
		return val(f.get_unit_id(unit).raw_value);
	}

	template <typename T>
	static val dump_pos(T &pos)
	{
		val o = val::object();
		o.set("x", to_emscripten(pos.x));
		o.set("y", to_emscripten(pos.y));
		return o;
	}

	static val dump_sprite(sprite_t *dumping)
	{
		val o = val::object();
		DUMP_VAL(index);
		DUMP_VAL(owner);
		DUMP_VAL(selection_index);
		DUMP_VAL(visibility_flags);
		DUMP_VAL(elevation_level);
		DUMP_VAL(flags);
		DUMP_VAL(selection_timer);
		DUMP_VAL(width);
		DUMP_VAL(height);
		DUMP_POS(position);
		return o;
	}

	static val dump_thingy(thingy_t *dumping)
	{
		val o = val::object();
		DUMP_VAL(hp);
		o.set("sprite", dump_sprite(dumping->sprite));
		return o;
	}

	static val dump_target(target_t *dumping)
	{
		val o = val::object();
		DUMP_POS(pos);
		DUMP_VAL(unit);
		return o;
	}

	static val dump_flingy(flingy_t *dumping)
	{
		val o = val::object();
		DUMP_VAL(index);
		o.set("move_target", dump_target(&dumping->move_target));
		DUMP_POS(next_movement_waypoint);
		DUMP_POS(next_target_waypoint);
		DUMP_VAL(movement_flags);
		DUMP_POS(position);
		DUMP_POS(exact_position);
		DUMP_VAL(flingy_top_speed);
		DUMP_VAL(current_speed);
		DUMP_VAL(next_speed);
		DUMP_POS(velocity);
		DUMP_VAL(flingy_acceleration);
		o.set("sprite", dump_sprite(dumping->sprite));
		o.set("_thingy_t", dump_thingy(dumping));
		return o;
	}

	static val dump_unit(unit_t *dumping)
	{
		val o = val::object();
		DUMP_VAL(owner);
		DUMP_VAL(order_state);
		DUMP_VAL(main_order_timer);
		DUMP_VAL(ground_weapon_cooldown);
		DUMP_VAL(air_weapon_cooldown);
		DUMP_VAL(spell_cooldown);
		o.set("order_target", dump_target(&dumping->order_target));

		DUMP_VAL(shield_points);
		DUMP_VAL(unit_type);

		DUMP_VAL(subunit);
		DUMP_VAL(auto_target_unit);
		DUMP_VAL(connected_unit);
		DUMP_VAL(order_queue_count);
		DUMP_VAL(order_process_timer);
		DUMP_VAL(unknown_0x086);
		DUMP_VAL(attack_notify_timer);
		DUMP_VAL(previous_unit_type);
		DUMP_VAL(last_event_timer);
		DUMP_VAL(last_event_color);
		DUMP_VAL(rank_increase);
		DUMP_VAL(kill_count);
		DUMP_VAL(last_attacking_player);
		DUMP_VAL(secondary_order_timer);
		DUMP_VAL(user_action_flags);
		DUMP_VAL(cloak_counter);
		DUMP_VAL(movement_state);
		DUMP_VAL(energy);
		DUMP_VAL(unit_id_generation);
		DUMP_VAL(damage_overlay_state);
		DUMP_VAL(hp_construction_rate);
		DUMP_VAL(shield_construction_rate);
		DUMP_VAL(remaining_build_time);
		DUMP_VAL(previous_hp);

		val loaded = val::object();
		for (int i = 0; i < dumping->loaded_units.size(); ++i)
		{
			loaded.set(i, dumping->loaded_units[i].raw_value);
		}
		o.set("loaded_units", loaded);
		o.set("_flingy_t", dump_flingy(dumping));
		return o;
	}
};

val lookup_unit_extended(int32_t index)
{
	util_functions f(m->ui.st);
	unit_t *u = f.get_unit(unit_id(index));
	if (!u)
	{
		return val::null();
	}
	return Dump::dump_unit(u);
}

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

	class_<util_functions>("util_functions")
		.function("worker_supply", &util_functions::worker_supply)
		.function("army_supply", &util_functions::army_supply)
		.function("get_all_incomplete_units", &util_functions::get_all_incomplete_units, allow_raw_pointers())
		.function("get_all_completed_units", &util_functions::get_all_completed_units, allow_raw_pointers())
		.function("get_all_units", &util_functions::get_all_units, allow_raw_pointers())
		.function("get_completed_upgrades", &util_functions::get_completed_upgrades)
		.function("get_completed_research", &util_functions::get_completed_research)
		.function("get_incomplete_upgrades", &util_functions::get_incomplete_upgrades)
		.function("get_incomplete_research", &util_functions::get_incomplete_research)
		.function("get_all_active_thingies", &util_functions::get_all_active_thingies, allow_raw_pointers())
		.function("get_all_sprites", &util_functions::get_all_sprites)
		.function("get_all_images", &util_functions::get_all_images)
		.function("get_all_active_bullets", &util_functions::get_all_active_bullets, allow_raw_pointers());

	function("get_util_funcs", &get_util_funcs);

	class_<tile_t>("tile_t")
		.property("visible", &tile_t::visible)
		.property("explored", &tile_t::explored)
		.property("flags", &tile_t::flags);

	class_<iscript_state_t>("iscript_state_t")
		.property("program_counter", &iscript_state_t::program_counter)
		.property("return_address", &iscript_state_t::return_address)
		.property("animation", &iscript_state_t::animation)
		.property("wait", &iscript_state_t::wait)
		.function("current_script", &iscript_state_t_current_script, allow_raw_pointers());

	class_<unit_type_t>("unit_type_t")
		.property("id", &unit_type_t_id)
		.property("build_time", &unit_type_t::build_time);

	class_<link_base>("link_base");

	class_<image_t, base<link_base>>("image_t")
		.property("index", &image_t::index)
		.property("modifier", &image_t::modifier)
		.property("frame_index_offset", &image_t::frame_index_offset)
		.property("flags", &image_t::flags)
		.property("frame_index_base", &image_t::frame_index_base)
		.property("frame_index", &image_t::frame_index)
		.property("modifier_data1", &image_t::modifier_data1)
		.property("modifier_data2", &image_t::modifier_data2)
		.property("frozen_y_value", &image_t::frozen_y_value)
		.function("image_type", &image_t_image_type, allow_raw_pointers())
		.function("iscript_state", &image_t_iscript_state, allow_raw_pointers());

	class_<sprite_t, base<link_base>>("sprite_t")
		.property("index", &sprite_t::index)
		.property("owner", &sprite_t::owner)
		.property("selection_index", &sprite_t::selection_index)
		.property("visibility_flags", &sprite_t::visibility_flags)
		.property("elevation_level", &sprite_t::elevation_level)
		.property("flags", &sprite_t::flags)
		.property("selection_timer", &sprite_t::selection_timer)
		.property("width", &sprite_t::width)
		.property("height", &sprite_t::height)
		.function("main_image", &sprite_t_main_image, allow_raw_pointers())
		.function("images", &sprite_t_images, allow_raw_pointers());

	class_<thingy_t, base<link_base>>("thingy_t")
		.property("hp", &flingy_t::hp)
		.function("sprite", &thingy_t_sprite, allow_raw_pointers());

	class_<flingy_t, base<thingy_t>>("flingy_t")
		.property("index", &flingy_t::index)
		.property("direction", &flingy_t::heading);

	class_<bullet_t, base<flingy_t>>("bullet_t");

	class_<unit_t, base<flingy_t>>("unit_t")
		.property("owner", &unit_t::owner)
		.property("order_state", &unit_t::order_state)
		.property("ground_weapon_cooldown", &unit_t::ground_weapon_cooldown)
		.property("air_weapon_cooldown", &unit_t::air_weapon_cooldown)
		.property("spell_cooldown", &unit_t::spell_cooldown)
		.property("rank_increase", &unit_t::rank_increase)
		.property("kill_count", &unit_t::kill_count)
		.property("cloak_counter", &unit_t::cloak_counter)
		.property("damage_overlay_state", &unit_t::damage_overlay_state)
		.property("status_flags", &unit_t::status_flags)
		.property("remaining_build_time", &unit_t::remaining_build_time)
		.property("carrying_flags", &unit_t::carrying_flags)
		.property("wireframe_randomizer", &unit_t::wireframe_randomizer)
		.property("secondary_order_state", &unit_t::secondary_order_state)
		.function("unit_type", &unit_t_unit_type, allow_raw_pointers())
		.function("build_type", &unit_t_build_type, allow_raw_pointers());

	//status_flags_t
	// order_type, order_unit_type, secondary_order_type
	// order_target, shield_points
	// subunit, auto_target_unit, connected_unit
	// previous_unit_type
	// build_queue, energy
	// loaded_units
}

extern "C" double player_get_value(int player, int index)
{
	if (player < 0 || player >= 12)
		return 0;
	switch (index)
	{
	case 0:
		return m->ui.st.players.at(player).controller == player_t::controller_occupied ? 1 : 0;
	case 1:
		return (double)m->ui.st.players.at(player).color;
	case 2:
		return (double)(uintptr_t)m->ui.replay_st.player_name.at(player).data();
	case 3:
		return m->ui.st.supply_used.at(player)[0].raw_value / 2.0;
	case 4:
		return m->ui.st.supply_used.at(player)[1].raw_value / 2.0;
	case 5:
		return m->ui.st.supply_used.at(player)[2].raw_value / 2.0;
	case 6:
		return std::min(m->ui.st.supply_available.at(player)[0].raw_value / 2.0, 200.0);
	case 7:
		return std::min(m->ui.st.supply_available.at(player)[1].raw_value / 2.0, 200.0);
	case 8:
		return std::min(m->ui.st.supply_available.at(player)[2].raw_value / 2.0, 200.0);
	case 9:
		return (double)m->ui.st.current_minerals.at(player);
	case 10:
		return (double)m->ui.st.current_gas.at(player);
	case 11:
		return util_functions(m->ui.st).worker_supply(player);
	case 12:
		return util_functions(m->ui.st).army_supply(player);
	case 13:
		return (double)(int)m->ui.st.players.at(player).race;
	case 14:
		return (double)m->ui.apm.at(player).current_apm;
	default:
		return 0;
	}
}

bool any_replay_loaded = false;

extern "C" void next_frame()
{
	m->update();
}

extern "C" void load_replay(const uint8_t *data, size_t len)
{
	m->reset();
	m->ui.load_replay_data(data, len);
	any_replay_loaded = true;
}

int main()
{
	using namespace bwgame;

	log("titan v1\n");

	std::chrono::high_resolution_clock clock;
	auto start = clock.now();

	auto load_data_file = data_loading::data_files_directory<data_loading::data_files_loader<data_loading::mpq_file<data_loading::js_file_reader<>>>>("");
	log("mpqs loaded\n");

	game_player player(load_data_file);

	main_t m(std::move(player));

	log("loaded in %dms\n", std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start).count());

	::m = &m;
	::g_m = &m;
	EM_ASM({js_load_done();});
	emscripten_set_main_loop_arg([](void *ptr)
								 {
									 if (!any_replay_loaded)
										 return;
									 EM_ASM({ js_pre_main_loop(); });
									 ((main_t *)ptr)->update();
									 EM_ASM({ js_post_main_loop(); });
								 },
								 &m, 0, 1);

	::g_m = nullptr;

	return 0;
}
