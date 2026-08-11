#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include <polymer/asset/asset_system.h>
#include <polymer/camera.h>
#include <polymer/connection.h>
#include <polymer/input.h>
#include <polymer/render/chunk_renderer.h>
#include <polymer/render/font_renderer.h>
#include <polymer/types.h>
#include <polymer/ui/chat_window.h>
#include <polymer/world/block.h>
#include <polymer/world/dimension.h>
#include <polymer/world/world.h>
#include <cmath>
#include <vulkan/vulkan_core.h>
#include "render/render.h"
#include "render/render_pass.h"
#include "timer.h"

namespace polymer {

struct MemoryArena;

struct Player {
  char name[17];
  char uuid[16];

  Vector3f previous_position{};
  Vector2f previous_look{};
  Vector3f position{};
  Vector2f look{};

  Vector3f velocity{};

  bool flying = false;
  bool sprinting = false;
  bool on_ground = false;
  float fall_time = 0.0f;
  u64 pressed_jump_at_ms = 0;

  float width = 0.6f;
  float height = 1.8f;
  float eye_height = 1.62f;

  u8 ping;
  u8 gamemode;
  bool listed;

  inline BoundingBox GetBoundingBox() const {
    float halfWidth = width / 2.0f;
    float halfHeight = height / 2.0f;

    Vector3f halfBox{halfWidth, halfHeight, halfWidth};

    return {position - halfBox, position + halfBox};
  }

  inline bool CanFly() const {
    // TODO: check actual player's server-sent capabilities
    return gamemode == 1 || gamemode == 3;
  }

  inline bool NoClip() const {
    return gamemode == 3;
  }
};

struct PlayerManager {
  Player players[256];
  size_t player_count = 0;

  Player* client_player = nullptr;
  char client_name[17];

  void SetClientPlayer(Player* player) {
    client_player = player;
  }

  void AddPlayer(const String& name, const String& uuid, u8 ping, u8 gamemode);
  void RemovePlayer(const String& uuid);
  Player* GetPlayerByUuid(const String& uuid);

  void RenderPlayerList(render::FontRenderer& font_renderer);
};

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;

  render::VulkanRenderer* renderer;
  render::FontRenderer font_renderer;
  render::ChunkRenderer chunk_renderer;

  render::RenderPass render_pass;
  // TODO: Clean this up
  VkCommandBuffer command_buffers[2];

  asset::AssetSystem assets;
  world::DimensionCodec dimension_codec;
  world::DimensionType dimension;

  Connection connection;
  Camera camera;
  world::World world;

  PlayerManager player_manager;
  ui::ChatWindow chat_window;

  float animation_accumulator;

  world::BlockRegistry block_registry;

  GameState(render::VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena);

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);
  void OnPlayerPositionAndLook(const Vector3f& position, const Vector3f& velocity, float yaw, float pitch, u32 flags);
  void OnDimensionChange();

  void OnWindowMouseMove(s32 dx, s32 dy);
  
  void Render(const Timer& timer, InputState* input);

  void Update(const Timer& timer, InputState* input);
  void ProcessMovement(float delta_tick, InputState* input);
  void UpdateFov();
  void MoveAndCollideWithStepping(Vector3f& movement);

  void SubmitFrame();

private:
  void ResolvePenetration();
  void UpdateCamera(const Timer& timer);
  bool IsPlayerGrounded();
  void MoveAndCollide(Vector3f& movement);
};

} // namespace polymer

#endif
