#include "PlayMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"
#include "read_write_chunk.hpp"
#include "data_path.hpp"
#include "load_save_png.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <fstream>
#include <iostream>

float randf(float min, float max) {
	float range = max - min;
	return min + (range * (rand() / (float)RAND_MAX));
}

float magnitude(glm::vec2 vec) {
	return sqrtf((vec.x * vec.x) + (vec.y * vec.y));
}

PlayMode::PlayMode() {
	std::vector< char > to;
	std::filebuf fb; 
	
	// Load in the binary sprites table
	if (fb.open(data_path("..\\assets\\sprites.bin"), std::ios::in | std::ios::binary)) {
		std::istream from(&fb);

		read_chunk(from, "img0", &to);
		printf("Loaded\n");

		int offset = 0;
		for (int sprite_idx = 0; sprite_idx < 256; sprite_idx++) {
			for (int row = 0; row < 8; row++) {
				ppu.tile_table[sprite_idx].bit0[row] = to[offset++];
			}

			for (int row = 0; row < 8; row++) {
				ppu.tile_table[sprite_idx].bit1[row] = to[offset++];
			}
		}
	}


	// Load in the sprite palettes from a PNG
	glm::uvec2 size;
	std::vector< glm::u8vec4 > data;
	load_png(data_path("..\\assets\\Palettes.png"), &size, &data, UpperLeftOrigin);

	for (int i = 0; i < data.size(); i += 4) {
		for (int j = 0; j < 4; j++) {
			ppu.palette_table[i / 4][j] = data[i + j];
		}
	}

	ball_tiles = {
		{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7},
		{16, 17, 32, 33}, {18, 19, 34, 35}, {20, 21, 36, 37}, {22, 23, 38, 39}, 
		{48, 49, 64, 65}, {50, 51, 66, 67}, {52, 53, 68, 69}, {54, 55, 70, 71},
		{224, 225, 240, 241}, {208}, {209}, {210}, {242}
	};

	// Clear all background tiles
	for (uint32_t y = 0; y < PPU466::BackgroundHeight; ++y) {
		for (uint32_t x = 0; x < PPU466::BackgroundWidth; ++x) {
			//ppu.background[x + PPU466::BackgroundWidth * y] = ((x + y) % 8) | (1 << 8);
			ppu.background[x + PPU466::BackgroundWidth * y] = 255 | (1 << 8);
		}
	}

	// Copy in the nucleus image
	for (uint32_t y = 0; y < 8; ++y) {
		for (uint32_t x = 0; x < 8; ++x) {
			int index = (x + 32 - 4) + PPU466::BackgroundWidth * (30 + 4 - y);

			ppu.background[index]  = (8 + x) + (16 * y);
			ppu.background[index] |= 1 << 8;
		}
	}

	srand(15466);
	for (uint32_t i = 0; i < 1; i++) {
		float dir = randf(-(float)M_PI, (float)M_PI) / 2;
		float angle = randf(0.f, 2 * (float)M_PI);

		orbitals_dirs.emplace_back(glm::vec3(cosf(dir), sinf(dir), 0));
		orbitals_angles.emplace_back(angle);
	}

	for (uint32_t i = 0; i < ELECTRON_CNT; i++) {
		Particle particle;
		
		// Pick a position not within the shell radius
		glm::vec2 center_offset;
		do {
			particle.pos = glm::vec2(randf(0.f, WORLD_WIDTH - TILE_SIZE), 
															 randf(0.f, WORLD_HEIGHT - TILE_SIZE));

			center_offset = CENTER - particle.pos;
		} while (magnitude(center_offset) < SHELL_RADIUS * 1.25f);

		particle.vel = glm::vec2(0.f, 0.f);
		particle.lifetime = NEUTRON_LIFETIME;
		particle.is_electron = true;

		particles.emplace_back(particle);
	}

	neutron_timer = NEUTRON_COOLDOWN;
	player_at = glm::vec2(128.0f, 120.0f);

	ppu.background_position.x = -128;
	ppu.background_position.y = -120;
}

PlayMode::~PlayMode() {
}

glm::vec3 rotate_y(glm::vec3 point, float angle) {
	glm::vec3 out;

	out.x = (point.x * std::cosf(angle)) - (point.z * std::sinf(angle));
	out.y = point.y;
	out.z = (point.x * std::sinf(angle)) + (point.z * std::cosf(angle));;

	return out;
}

glm::ivec3 PlayMode::project_point(glm::vec3 point) {
	glm::ivec3 out;
	
	out.x = int32_t(PPU466::ScreenHeight * point.x / (point.z + 2.5f));
	out.y = int32_t(PPU466::ScreenHeight * point.y / (point.z + 2.5f));

	out.x += PPU466::ScreenWidth;
	out.y += PPU466::ScreenHeight;
	
	out.x += ((uint16_t)ppu.background_position.x % 512) - 512;
	out.y += ((uint16_t)ppu.background_position.y % 512) - 512;

	// Compute sprite radius and clamp at size 16
	out.z = int32_t((24.f / (point.z + 2.5f)) + .5f);
	out.z = (out.z > 16) ? 16 : out.z;
	// Make radii start at zero
	out.z--;

	return out;
}

void PlayMode::draw_sprite(int x, int y, int size, bool background, int palette) {
	// Ignore sprites beyond the screen range
	if (x < 0 || x > PPU466::ScreenWidth) return;
	if (y < 0 || y > PPU466::ScreenHeight) return;

	std::vector<int> x_off = { 0, TILE_SIZE, 0, TILE_SIZE };
	std::vector<int> y_off = { 0, 0, TILE_SIZE, TILE_SIZE };

	// Draws 1x1 or 2x2 sprites
	int i = 0;
	for (int tile : ball_tiles[size]) {
		if (x + x_off[i] < 0 || x + x_off[i] >= PPU466::ScreenWidth) continue;
		if (y - y_off[i] < 0 || y - y_off[i] >= PPU466::ScreenHeight) continue;
		// If we have more than the allowed number of sprites, just ignore to precent crashing.
		if (sprite_cnt == 64) continue;

		ppu.sprites[sprite_cnt].x = x + x_off[i];
		ppu.sprites[sprite_cnt].y = y - y_off[i];
		ppu.sprites[sprite_cnt].index = tile;
		ppu.sprites[sprite_cnt].attributes = background ? 1 << 7 : 0;
		ppu.sprites[sprite_cnt].attributes |= palette;
		sprite_cnt++;
		i++;
	}	
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.downs += 1;
			down.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.downs += 1;
			space.pressed = true;
			return true;
		}

	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	game_time += elapsed;

	hit_cooldown -= elapsed;
	hit_cooldown = fmaxf(hit_cooldown, 0.f);

	// Update player position
	constexpr float PlayerSpeed = 100.0f;
	glm::vec2 player_vel(0.f);
	if (left.pressed)  player_vel.x = -1;
	if (right.pressed) player_vel.x =  1;
	if (down.pressed)  player_vel.y = -1;
	if (up.pressed)    player_vel.y =  1;

	player_vel /= fmaxf(magnitude(player_vel), 1.f);
	player_vel *= PlayerSpeed;
	player_at += player_vel * elapsed;

	// Bound player to screen
	player_at.x = fmaxf(fminf(player_at.x, WORLD_WIDTH  - PLAYER_SIZE), 1);;
	player_at.y = fmaxf(fminf(player_at.y, WORLD_HEIGHT - PLAYER_SIZE), 1);;

	// Computer player's sprite position
	player_pos.x = (int)player_at.x;
	player_pos.y = (int)player_at.y;

	glm::ivec2 player_screen_pos = player_pos + ppu.background_position;


	// Scroll screen to player movement
	if (player_screen_pos.x + PLAYER_SIZE > PPU466::ScreenWidth) {
		ppu.background_position.x -= player_screen_pos.x + PLAYER_SIZE - PPU466::ScreenWidth;
	}
	if (player_screen_pos.x < 0) {
		ppu.background_position.x -= player_screen_pos.x;
	}
	if (player_screen_pos.y + PLAYER_SIZE > PPU466::ScreenHeight) {
		ppu.background_position.y -= player_screen_pos.y + PLAYER_SIZE - PPU466::ScreenHeight;
	}
	if (player_screen_pos.y < 0) {
		ppu.background_position.y -= player_screen_pos.y;
	}

	// Update free electron positions
	for (int32_t i = 0; i < particles.size(); ++i) {
		particles[i].pos += particles[i].vel * elapsed;
		if (particles[i].is_electron) {
			particles[i].vel *= powf(0.5f, elapsed);
		}

		glm::vec2 center_offset = CENTER - particles[i].pos;
		float dist = fmaxf(magnitude(center_offset), 1.f);

		// If an electron gets close enough to the atom, count it as an orbital
		if (dist < SHELL_RADIUS && particles[i].is_electron) {
			particles.erase(particles.begin() + i);
			if (i == grabbing) grabbing = -1;
			if (i < grabbing) grabbing--;
			i--;

			glm::vec3 dir(-center_offset.x / dist, -center_offset.y / dist, 0.f);
			orbitals_dirs.emplace_back(dir);
			orbitals_angles.emplace_back(ORBITAL_SPEED * game_time);
			continue;
		}

		// If a neutron hits the nucleus, things get unstable!
		if (dist < NUCLEUS_RADIUS && !particles[i].is_electron) {
			particles.erase(particles.begin() + i);
			if (i == grabbing) grabbing = -1;
			if (i < grabbing) grabbing--;
			i--;
			hit_cooldown = HIT_TIME;
			hits++;
			continue;
		}

		// Delete neutrons occasionally
		if (!particles[i].is_electron && grabbing != i) {
			// If neutrons are still, wait a few seconds, then die
			if (magnitude(particles[i].vel) < 1.0f) {
				particles[i].lifetime -= elapsed;

				if (particles[i].lifetime < 0.f) {
					particles.erase(particles.begin() + i);
					if (i == grabbing) grabbing = -1;
					if (i < grabbing) grabbing--;
					i--;
					continue;
				}
			}

			// If neutrons leave the screen, die
			if (particles[i].pos.x < 0.f || particles[i].pos.x > 512.f || 
					particles[i].pos.y < 0.f || particles[i].pos.y > 480.f) {
				particles.erase(particles.begin() + i);
				if (i == grabbing) grabbing = -1;
				if (i < grabbing) grabbing--;
				i--;
				continue;
			}
		}
	}
	
	if (grabbing >= 0) {
		particles[grabbing].pos = player_at;
	}

	// On grab
	if (space.pressed && grabbing < 0) {
		for (uint32_t i = 0; i < particles.size(); ++i) {
			glm::vec2 offset = player_at - particles[i].pos;
			if (magnitude(offset) < GRAB_RADIUS) {
				grabbing = i;
				break;
			}
		}
	}

	// On release of ball
	if (!space.pressed && grabbing >= 0) {
		particles[grabbing].vel = player_vel;
		grabbing = -1;
	}

	meter_pos = (.9f * meter_pos) + (0.1f * (TILE_SIZE * hits));
	
	// Generate a new neutron every couple seconds
	neutron_timer -= elapsed;
	if (neutron_timer <= 0.f) {
		Particle particle;
		particle.is_electron = false;
		particle.lifetime = NEUTRON_LIFETIME;
		
		switch (rand() % 4) {
		case 0:
			particle.pos.x = 1.f;
			particle.pos.y = randf(1.f, 479.f);
			break;
		case 1:
			particle.pos.x = 511.f;
			particle.pos.y = randf(1.f, 479.f);
			break;
		case 2:
			particle.pos.x = randf(1.f, 511.f);
			particle.pos.y = 1.f;
			break;
		case 3:
			particle.pos.x = randf(1.f, 511.f);
			particle.pos.y = 479.f;
			break;
		}

		particle.vel = CENTER - particle.pos;
		particle.vel /= magnitude(particle.vel);
		particle.vel *= NEUTRON_SPEED;

		particles.emplace_back(particle);

		neutron_timer = NEUTRON_COOLDOWN;
	}

	// If the nucleus gets hit too many times, end the game
	if (hits >= MAX_HITS) {
		printf("You Lose!");
		Mode::set_current(nullptr);
		return;
	}

	// Check if any electrons remain
	bool won = true;
	for (int32_t i = 0; i < particles.size(); ++i) {
		if (particles[i].is_electron) {
			won = false;
			break;
		}
	}


	// On win state, end game
	if (won) {
		printf("You Win!");
		Mode::set_current(nullptr);
		return;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//--- set ppu state based on game state ---

	sprite_cnt = 0;

	// Set background color to blue
	ppu.background_color = glm::u8vec4(0x40, 0x40, 0x80, 0xff);

	// Draw all electrons orbiting the nucleus
	for (uint32_t i = 0; i < orbitals_dirs.size(); ++i) {
		float angle = (ORBITAL_SPEED * -game_time) + orbitals_angles[i];
		glm::vec3 point = cosf(angle) * orbitals_dirs[i];
		point.z = -sinf(angle);
		
		glm::ivec3 proj_point = project_point(point);

		int offset = proj_point.z / 2;

		draw_sprite(proj_point.x - offset, 
								proj_point.y + offset, 
								proj_point.z, 
								point.z > 0,
								0);
	}

	// Draw the free particles
	for (uint32_t i = 0; i < particles.size(); ++i) {
		draw_sprite((int)particles[i].pos.x + ppu.background_position.x,
								(int)particles[i].pos.y + ppu.background_position.y,
								8, false, particles[i].is_electron ? 0 : 2);
	}

	// Draw the player sprite
	draw_sprite(player_pos.x + ppu.background_position.x, 
							player_pos.y + ppu.background_position.y + 8, 
							16, false, 3);

	// Draw the DANGER THERMOMETER
	int scorebar_y = PPU466::ScreenHeight - TILE_SIZE - 1;
	for (int tile = 0; tile < MAX_HITS; tile++) {
		int sprite_idx = 18;
		if (tile == 0) sprite_idx = 17;
		if (tile == MAX_HITS - 1) sprite_idx = 19;

		int color = 4 + (tile * 3 / MAX_HITS);
		
		draw_sprite(1 + (TILE_SIZE * tile), scorebar_y, sprite_idx, false, color);
	}

	draw_sprite((int)meter_pos + (TILE_SIZE / 2), 
							PPU466::ScreenHeight - 4, 20, false, 4);
	

	// Put all the other sprites off the edge of the screen
	for (int i = sprite_cnt; i < 64; i++) {
		ppu.sprites[i].y = 255;
	}

	// Adjust the scroll a little bit if we need to shake the background.
	int scroll_x = ppu.background_position.x;

	float t = hit_cooldown * 4.0f * 3.14159f / HIT_TIME;
	ppu.background_position.x += (int)(10 * sinf(t));

	//--- actually draw ---
	ppu.draw(drawable_size);

	ppu.background_position.x = scroll_x;
}
