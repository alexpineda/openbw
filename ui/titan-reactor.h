
#include "common.h"
#include "drawing.h"
#include "../bwenums.h"
#include "../bwgame.h"
#include "../replay.h"

namespace bwgame
{

	struct played_sound_t
	{
		int32_t id;
		int32_t x;
		int32_t y;
		int32_t unit_type_id = -1;
	};

	struct player_data_t
	{
		int minerals;
		int gas;
		int supply;
		int supply_max;
		int worker_supply;
		int army_supply;
		int apm;
	};

	struct unit_in_production_t
	{
		int id;
		int count;
		int progress;
	};

	struct upgrade_in_production_t
	{
		int id;
		int level;
		int progress;

		upgrade_in_production_t(int id, const int level, const int progress) : id(id), level(level), progress(progress) {}
	};

	struct research_in_production_t
	{
		int id;
		int progress;

		research_in_production_t(int id, const int progress) : id(id), progress(progress) {}
	};
	struct production_data_t
	{
		std::vector<unit_in_production_t> units_in_production;
		std::vector<upgrade_in_production_t> upgrades_in_production;
		std::vector<research_in_production_t> research_in_production;
	};

	struct titan_replay_functions : ui_functions
	{
		game_player player;
		std::vector<played_sound_t> played_sounds;
		std::vector<int> deleted_images;
		std::vector<int> deleted_sprites;
		std::vector<int> killed_units;
		std::vector<int> destroyed_units;
		std::vector<int> deleted_bullets;
		std::array<player_data_t, 8> player_data;
		std::array<production_data_t, 8> production_data;
		std::vector<uint8_t> fow;
		std::vector<uint8_t> creep;
		std::vector<uint8_t> creep_edges;
		uint8_t player_visibility;

		titan_replay_functions(game_player player) : ui_functions(std::move(player))
		{
		}

		void init_session()
		{
			creep.resize(game_st.map_width * game_st.map_height);
			creep_edges.resize(game_st.map_width * game_st.map_height);
			fow.resize(game_st.map_width * game_st.map_height);
		}

		virtual void play_sound(int id, xy position, const unit_t *source_unit, bool add_race_index) override
		{
			played_sound_t ps;
			ps.id = add_race_index ? id + 1 : id;
			ps.x = position.x;
			ps.y = position.y;

			if (source_unit != nullptr)
			{
				const unit_type_t *unit_type = source_unit->unit_type;
				ps.unit_type_id = (int)unit_type->id;
			}
			played_sounds.push_back(ps);
		}

		virtual void on_action(int owner, int action) override
		{
			apm.at(owner).add_action(st.current_frame);
		}

		virtual void on_image_destroy(image_t *image) override
		{
			deleted_images.push_back(image->index);
		}

		virtual void on_bullet_destroy(bullet_t *bullet) override
		{
			deleted_bullets.push_back(bullet->index);
		}

		virtual void on_sprite_destroy(sprite_t *sprite) override
		{
			deleted_sprites.push_back(sprite->index);
		}

		virtual void on_kill_unit(unit_t *u) override
		{
			killed_units.push_back(get_unit_id(u).raw_value);
		}

		virtual void on_unit_destroy(unit_t *u) override
		{
			destroyed_units.push_back(get_unit_id(u).raw_value);
		}

		fp8 game_speed = fp8::integer(1);

		void generate_player_data()
		{
			for (int player = 0; player < 8; player++)
			{
				const int pos = player * 8;
				auto &p = player_data[player];

				double worker_supply = 0.0;
				for (const unit_t *u : ptr(st.player_units.at(player)))
				{
					if (!ut_worker(u))
						continue;
					if (!u_completed(u))
						continue;
					worker_supply += u->unit_type->supply_required.raw_value / 2.0;
				}

				double army_supply = 0.0;
				for (const unit_t *u : ptr(st.player_units.at(player)))
				{
					if (ut_worker(u))
						continue;
					if (!u_completed(u))
						continue;
					army_supply += u->unit_type->supply_required.raw_value / 2.0;
				}

				p.minerals = st.current_minerals.at(player);
				p.gas = st.current_gas.at(player);
				p.supply = 0;
				p.supply_max = 0;
				for (int r = 0; r < 3; r++)
				{
					p.supply += st.supply_used.at(player)[r].raw_value / 2.0;
					p.supply_max += std::min(st.supply_available.at(player)[r].raw_value / 2.0, 200.0);
				}
				p.worker_supply = worker_supply;
				p.army_supply = army_supply;
				p.apm = apm[player].current_apm;
			}
		}

		int get_unit_type_id(const unit_t *u)
		{
			if (u->order_type->id == Orders::ZergBirth ||
				u->order_type->id == Orders::ZergBuildingMorph ||
				u->order_type->id == Orders::ZergUnitMorph ||
				u->order_type->id == Orders::IncompleteMorphing)
			{
				return (int)u->build_queue[0]->id;
			}
			else
			{
				return (int)u->unit_type->id;
			}
		}

		void generate_production_data()
		{

			for (int player = 0; player < 8; player++)
			{
				const int pos = player * 8;
				auto &p = production_data[player];

				p.units_in_production.clear();
				p.upgrades_in_production.clear();
				p.research_in_production.clear();

				for (const unit_t *u : ptr(st.player_units.at(player)))
				{

					// incomplete units + count
					if (!u_completed(u))
					{
						bool found = false;
						for (auto &o : p.units_in_production)
						{
							if (o.id == get_unit_type_id(u))
							{
								o.count++;
								if (u->remaining_build_time < o.progress)
								{
									o.progress = u->remaining_build_time;
								}
								found = true;
								break;
							}
						}

						if (found == false)
						{
							unit_in_production_t ip;
							ip.id = get_unit_type_id(u);
							ip.count = 1;
							ip.progress = u->remaining_build_time;
							p.units_in_production.push_back(ip);
						}
					}
					else
					{
						// incomplete upgrades
						if (u->order_type->id == Orders::Upgrade && u->building.upgrading_type)
						{
							upgrade_in_production_t uip{(int)u->building.upgrading_type->id, u->building.upgrading_level, u->building.upgrade_research_time};

							p.upgrades_in_production.push_back(uip);
						}

						// incomplete research
						if (u->order_type->id == Orders::ResearchTech && u->building.researching_type)
						{
							research_in_production_t uip{(int)u->building.researching_type->id, u->building.upgrade_research_time};
							p.research_in_production.push_back(uip);
						}
					}
				}

				// completed upgrades
				for (size_t i = 0; i != 61; ++i)
				{
					int level = player_upgrade_level(player, (UpgradeTypes)i);
					if (level == 0)
						continue;
					upgrade_in_production_t uip{(int)i, level, 0};
					p.upgrades_in_production.push_back(uip);
				}

				// completed research
				for (size_t i = 0; i != 44; ++i)
				{
					if (!player_has_researched(player, (TechTypes)i))
						continue;
					research_in_production_t uip{(int)i, 0};
					p.research_in_production.push_back(uip);
				}
			}
		}

		void clear_frame()
		{

			deleted_images.clear();
			deleted_sprites.clear();
			destroyed_units.clear();
			killed_units.clear();
			deleted_bullets.clear();
			played_sounds.clear();
		}

		void reset()
		{

			clear_frame();
			player_data.fill({});
			production_data.fill({});
			fow.clear();
			creep.clear();
			creep_edges.clear();

			apm = {};
			auto &game = *st.game;
			st = state();
			game = game_state();
			replay_st = replay_state();
			action_st = action_state();

			replay_frame = 0;
			st.global = &global_st;
			st.game = &game;

			// default to 1.16 limits
			st.images_container = 5000;
			st.units_container = 1700;
			st.sprites_container = 2500;
			st.bullets_container = 100;
			st.orders_container = 2000;

			unit_id::unit_generation_size = st.units_container.max_size == 1700 ? 5 : 3;
		}

		void next_no_replay()
		{
			state_functions::next_frame();
		}

		//	extracted from drawing.h -> draw_tiles
		void generate_creep()
		{
			if (st.creep_life.recede_timer > 0) {
				return;
			}

			xy dirs[9] = {{1, 1}, {0, 1}, {-1, 1}, {1, 0}, {-1, 0}, {1, -1}, {0, -1}, {-1, -1}, {0, 0}};

			for (int tile_y = 0; tile_y < game_st.map_height; ++tile_y)
			{
				for (int tile_x = 0; tile_x < game_st.map_width; ++tile_x)
				{
					size_t tile_index = tile_y * game_st.map_tile_width + tile_x;
					auto *megatile_index = &st.tiles_mega_tile_index[tile_index];
					auto *tile = &st.tiles[tile_index];

					creep[tile_index] = 0;
					creep_edges[tile_index] = 0;

					if (tile->flags & tile_t::flag_has_creep)
					{
						creep[tile_index] = game_st.cv5.at(1).mega_tile_index[creep_random_tile_indices[tile_x + tile_y * game_st.map_tile_width]];
					}

					if (~tile->flags & tile_t::flag_has_creep)
					{
						size_t creep_index = 0;
						for (size_t i = 0; i != 9; ++i)
						{
							int add_x = dirs[i].x;
							int add_y = dirs[i].y;
							if (tile_x + add_x >= game_st.map_tile_width)
								continue;
							if (tile_y + add_y >= game_st.map_tile_height)
								continue;
							if (st.tiles[tile_x + add_x + (tile_y + add_y) * game_st.map_tile_width].flags & tile_t::flag_has_creep)
								creep_index |= 1 << i;
						}
						size_t creep_frame = img.creep_edge_frame_index[creep_index];

						if (creep_frame)
						{

							creep_edges[tile_index] = creep_frame;
						}
					}
				}
			}
		}

		void generate_fow(bool instant)
		{
			int i = 0;
			for (auto &tile : st.tiles)
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
					fow[i] = v;
				}
				else
				{
					if (v > fow[i])
					{
						fow[i] = std::min(v, fow[i] + 10);
					}
					else if (v < fow[i])
					{
						fow[i] = std::max(v, fow[i] - 5);
					}
				}

				i++;
			}
		};

		//TODO: optimize when this is called as this is slow
		void generate_frame() 
		{
			// generate_creep();
			generate_fow(false);
		}
	};

}
