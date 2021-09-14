#include "PPU466.hpp"
#include "Mode.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

#define PLAYER_SIZE 16
#define ELECTRON_CNT 12

#define ORBITAL_SPEED 3.14159265f
#define GRAB_RADIUS 16.f
#define SHELL_RADIUS (PPU466::ScreenHeight / 2.5f)
#define NUCLEUS_RADIUS 36.f

#define HIT_TIME 0.2f
#define MAX_HITS 6

#define NEUTRON_COOLDOWN 6.f
#define NEUTRON_LIFETIME 5.f

#define NEUTRON_SPEED 16.f

#define TILE_SIZE 8
#define WORLD_WIDTH  (PPU466::ScreenWidth * 2)
#define WORLD_HEIGHT (PPU466::ScreenHeight * 2)

#define CENTER (glm::vec2(WORLD_WIDTH / 2, WORLD_HEIGHT / 2))

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	glm::ivec3 project_point(glm::vec3 point);
	void draw_sprite(int x, int y, int size, bool background, int palette);

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----
	typedef struct Particle {
		glm::vec2 pos;
		glm::vec2 vel;
		bool is_electron;
		float lifetime;
	} Particle;

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up, space;

	//some weird background animation:
	float game_time = 0.0f;

	//player position:
	glm::vec2 player_at = glm::vec2(0.0f);
	glm::ivec2 player_pos = glm::ivec2(0);

	// All 3d points to render
	std::vector<glm::vec3> orbitals_dirs;
	std::vector<float>     orbitals_angles;

	int grabbing = -1;
	std::vector<Particle> particles;

	std::vector<std::vector<int>> ball_tiles;

	int sprite_cnt;
	
	float hit_cooldown;
	int hits = 0;

	float neutron_timer = 0.f;

	float meter_pos = 0;


	//----- drawing handled by PPU466 -----

	PPU466 ppu;
};
