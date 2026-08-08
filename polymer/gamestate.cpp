#include <polymer/gamestate.h>



#include <string.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <volk.h>
#include <vulkan/vulkan_core.h>
#include "input.h"
#include "memory.h"
#include "protocol.h"
#include "render/chunk_renderer.h"
#include "render/font_renderer.h"
#include "render/render.h"
#include "render/swapchain.h"
#include "types.h"
#include "world/chunk.h"
#include "world/block.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>
using namespace polymer::world;

using polymer::render::kRenderLayerCount;
using polymer::render::RenderLayer;
using polymer::world::ChunkMesh;
using polymer::world::ChunkSection;
using polymer::world::ChunkSectionInfo;
using polymer::world::kChunkCacheSize;
using polymer::world::kChunkColumnCount;
using polymer::world::BlockState;

namespace polymer {

void OnSwapchainCreate(render::Swapchain& swapchain, void* user_data) {
  GameState* gamestate = (GameState*)user_data;

  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandPool = gamestate->renderer->command_pool;
  alloc_info.commandBufferCount = 2;

  vkAllocateCommandBuffers(swapchain.device, &alloc_info, gamestate->command_buffers);

  // Create main render pass
  VkAttachmentDescription color_attachment = {};

  color_attachment.format = swapchain.format;
  color_attachment.samples = swapchain.multisample.samples;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  if (swapchain.multisample.samples & VK_SAMPLE_COUNT_1_BIT) {
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }

  VkAttachmentDescription depth_attachment = {};
  depth_attachment.format = VK_FORMAT_D32_SFLOAT;
  depth_attachment.samples = swapchain.multisample.samples;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription color_attachment_resolve = {};

  color_attachment_resolve.format = swapchain.format;
  color_attachment_resolve.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment_resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment_resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment_resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment_resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment_resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment_resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  gamestate->render_pass.CreateSimple(swapchain, color_attachment, depth_attachment, color_attachment_resolve);

  gamestate->font_renderer.render_pass = &gamestate->render_pass;
  gamestate->chunk_renderer.render_pass = &gamestate->render_pass;

  gamestate->font_renderer.OnSwapchainCreate(*gamestate->trans_arena, swapchain, gamestate->renderer->descriptor_pool);
  gamestate->chunk_renderer.OnSwapchainCreate(*gamestate->trans_arena, swapchain, gamestate->renderer->descriptor_pool);
}

void OnSwapchainCleanup(render::Swapchain& swapchain, void* user_data) {
  GameState* gamestate = (GameState*)user_data;

  gamestate->render_pass.Destroy(swapchain);

  gamestate->font_renderer.OnSwapchainDestroy(swapchain.device);
  gamestate->chunk_renderer.OnSwapchainDestroy(swapchain.device);
}

GameState::GameState(render::VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena)
    : perm_arena(perm_arena), trans_arena(trans_arena), connection(*perm_arena), renderer(renderer),
      block_registry(*perm_arena), assets(), world(*trans_arena, *renderer, assets, block_registry),
      chat_window(*trans_arena) {
  camera.near = 0.07f;
  camera.far = 1024.0f;
  camera.fov = Radians(80.0f);

  position_sync_timer = 0.0f;
  animation_accumulator = 0.0f;
  time_accumulator = 0.0f;

  renderer->swapchain.RegisterCreateCallback(this, OnSwapchainCreate);
  renderer->swapchain.RegisterCleanupCallback(this, OnSwapchainCleanup);
}

void GameState::Update(float dt, InputState* input) {
  ProcessMovement(dt, input);

  float sunlight = world.GetSunlight();

  VkClearValue clears[] = {{0.71f * sunlight, 0.816f * sunlight, 1.0f * sunlight, 1.0f}, {1.0f, 0}};

  VkCommandBufferBeginInfo begin_info = {};

  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;

  VkCommandBuffer command_buffer = command_buffers[renderer->current_frame];

  vkBeginCommandBuffer(command_buffer, &begin_info);

  render_pass.BeginPass(command_buffer, renderer->GetExtent(), renderer->current_image, clears,
                        polymer_array_count(clears));

  if (input->display_players) {
    player_manager.RenderPlayerList(font_renderer);
  }

  chat_window.Update(font_renderer);

  animation_accumulator += dt;
  time_accumulator += dt;

  constexpr float kMaxFrame = 256.0f;
  if (animation_accumulator >= kMaxFrame) {
    animation_accumulator -= kMaxFrame;
  }

  if (time_accumulator >= 1.0f / 20.0f) {
    time_accumulator -= 1.0f / 20.0f;

    if (world.world_tick++ >= 24000) {
      world.world_tick = 0;
    }
  }

  world.Update(dt);

  chunk_renderer.Draw(command_buffer, renderer->current_frame, world, camera, animation_accumulator, sunlight);
}

void GameState::SubmitFrame() {
  VkCommandBuffer command_buffer = command_buffers[renderer->current_frame];

  render_pass.EndPass(command_buffer);

  vkEndCommandBuffer(command_buffer);

  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  VkSemaphore wait_semaphore = renderer->image_available_semaphores[renderer->current_frame];
  VkSemaphore signal_semaphore = renderer->render_complete_semaphores[renderer->current_frame];

  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &wait_semaphore;
  submit_info.pWaitDstStageMask = waitStages;

  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &signal_semaphore;

  VkFence fence = renderer->frame_fences[renderer->current_frame];

  vkResetFences(renderer->device, 1, &fence);
  if (vkQueueSubmit(renderer->graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
    fprintf(stderr, "Failed to submit draw command buffer.\n");
  }
}

void GameState::MoveAndCollide(Vector3f& movement) {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  Vector3f remaining = movement;

  for (int i = 0; i < 3; i++) {
    auto playerBoundingBox = player->getBoundingBox() + Vector3f{0, player->height / 2.0f, 0};
    auto nextPlayerBoundingBox = playerBoundingBox + remaining;
    auto sweptBoundingBox = playerBoundingBox.Combine(nextPlayerBoundingBox);

    auto minX = (int)floor(sweptBoundingBox.min.x);
    auto maxX = (int)floor(sweptBoundingBox.max.x);
    auto minY = (int)floor(sweptBoundingBox.min.y);
    auto maxY = (int)floor(sweptBoundingBox.max.y);
    auto minZ = (int)floor(sweptBoundingBox.min.z);
    auto maxZ = (int)floor(sweptBoundingBox.max.z);
  
    Vector3f movementMask{1, 1, 1};
    float hitFraction = -1;

    for (int x = minX; x <= maxX; x++) {
      for (int z = minZ; z <= maxZ; z++) {
        for (int y = minY; y <= maxY; y++) {
          auto block = world.GetBlockAt({x, y, z});
          if (!block) {
            continue;
          }

          Vector3f pos{(float)x, (float)y, (float)z};
          for (int e = 0; e < block->model.element_count; e++) {
            BlockElement& element = block->model.elements[e];
            auto elementBoundingBox = element.GetBoundingBox(block->model.rotation, pos);

            auto minkowski = elementBoundingBox.MinkowskiDifference(playerBoundingBox);

            auto raycast = minkowski.Raycast(remaining);

            if (raycast.hit && (raycast.fraction < hitFraction || hitFraction < 0)) {
              hitFraction = raycast.fraction;
              movementMask = raycast.movementMask;
            }
          }
        }
      }
    }

    if (hitFraction < 0) {
      player->position += remaining;
      break;
    }

    if (movementMask.y == 0) {
      player->velocity.y = 0;
    }

    player->position += remaining * hitFraction;

    remaining *= (1 - hitFraction);
    remaining *= movementMask;

    if (abs(remaining.LengthSq()) < kEpsilon * kEpsilon) {
      break;
    }
  }
}

void GameState::ResolvePenetration() {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  for (int i = 0; i < 3; i++) {
    auto playerBoundingBox = player->getBoundingBox() + Vector3f{0, player->height / 2.0f, 0};

    auto minX = (int)floor(playerBoundingBox.min.x);
    auto maxX = (int)floor(playerBoundingBox.max.x);
    auto minY = (int)floor(playerBoundingBox.min.y);
    auto maxY = (int)floor(playerBoundingBox.max.y);
    auto minZ = (int)floor(playerBoundingBox.min.z);
    auto maxZ = (int)floor(playerBoundingBox.max.z);

    Vector3f correction{0, 0, 0};

    for (int x = minX; x <= maxX; x++) {
      for (int z = minZ; z <= maxZ; z++) {
        for (int y = minY; y <= maxY; y++) {
          auto block = world.GetBlockAt({x, y, z});
          if (!block) {
            continue;
          }

          Vector3f pos{(float)x, (float)y, (float)z};
          for (int e = 0; e < block->model.element_count; e++) {
            BlockElement& element = block->model.elements[e];
            auto elementBoundingBox = element.GetBoundingBox(block->model.rotation, pos);

            auto minkowski = elementBoundingBox.MinkowskiDifference(playerBoundingBox);

            if (minkowski.min.x < 0 && minkowski.max.x > 0 && minkowski.min.y < 0 && minkowski.max.y > 0 &&
                minkowski.min.z < 0 && minkowski.max.z > 0) {
              auto vec = minkowski.ClosestPointOnBoundsToPoint(Vector3f{0, 0, 0});

              if (vec.LengthSq() > correction.LengthSq()) {
                correction = vec;
              }
            }
          }
        }
      }
    }

    if (correction.LengthSq() == 0) {
      break;
    }

    player->position += correction;
  }
}

void GameState::MoveAndCollideWithStepping(Vector3f& movement) {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  Vector3f initialPos = player->position;

  MoveAndCollide(movement);

  Vector3f normalPos = player->position;
  auto normalDiff = normalPos - initialPos;

  if (!player->on_ground || movement.LengthSqXZ() <= kEpsilon || normalDiff.LengthSqXZ() >= movement.LengthSqXZ()) {
    return;
  }

  player->position = initialPos;

  constexpr float stepHeight = 0.5f;

  MoveAndCollide(Vector3f{0, stepHeight, 0});
  MoveAndCollide(Vector3f{movement.x, 0, movement.z});
  MoveAndCollide(Vector3f{0, -stepHeight, 0});

  Vector3f stepPos = player->position;
  auto stepDiff = stepPos - initialPos;

  if (stepDiff.LengthSqXZ() > normalDiff.LengthSqXZ()) {
    return;
  }

  player->position = normalPos;
}

bool GameState::IsPlayerGrounded() {
  auto player = player_manager.client_player;
  if (!player) {
    return false;
  }

  constexpr float groundProbe = 0.001f;
  constexpr float horizontalInset = 0.001f;

  auto playerBox = player->getBoundingBox() + Vector3f{0, player->height / 2.0f, 0};

  BoundingBox box{{playerBox.min.x + horizontalInset, playerBox.min.y, playerBox.min.z + horizontalInset},
      {playerBox.max.x - horizontalInset, playerBox.min.y + groundProbe, playerBox.max.z - horizontalInset}};

  auto minX = (int)floor(box.min.x);
  auto maxX = (int)floor(box.max.x);
  auto minY = (int)floor(box.min.y) - 1;
  auto maxY = (int)floor(box.max.y);
  auto minZ = (int)floor(box.min.z);
  auto maxZ = (int)floor(box.max.z);

  for (int x = minX; x <= maxX; x++) {
    for (int z = minZ; z <= maxZ; z++) {
      for (int y = minY; y <= maxY; y++) {
        auto block = world.GetBlockAt({x, y, z});
        if (!block) {
          continue;
        }

        Vector3f pos{(float)x, (float)y, (float)z};
        for (int e = 0; e < block->model.element_count; e++) {
          BlockElement& element = block->model.elements[e];
          auto elementBoundingBox = element.GetBoundingBox(block->model.rotation, pos);
          
          if (box.Intersects(elementBoundingBox)) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

void GameState::ProcessMovement(float dt, InputState* input) {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  bool chunkLoaded = world.ChunkLoadedAt({(int)floor(player->position.x), (int)floor(player->position.y), (int)floor(player->position.z)});

  ResolvePenetration();

  bool on_ground = player->velocity.y <= 0 && IsPlayerGrounded();
  if (on_ground && !player->on_ground) {
    player->fall_time = 0;
    player->velocity.y = 0;
  }
  player->on_ground = on_ground;

  constexpr float kMoveSpeed = 5.0f;
  constexpr float kSprintModifier = 1.5f;
  constexpr float gravity = 31.0f;
  constexpr float terminalVelocity = 60.0f;
  constexpr float jumpHeight = 1.25f;
  const float jumpVelocity = std::sqrt(2.0f * gravity * jumpHeight);

  Vector3f movement;

  if (chunkLoaded && !player->on_ground) {
    player->fall_time += dt;

    player->velocity -= Vector3f{0, gravity * dt, 0};
    player->velocity.y = std::max(player->velocity.y, -terminalVelocity);
  }

  if (input->forward) {
    movement += camera.GetForwardXZ();
  }

  if (input->backward) {
    movement -= camera.GetForwardXZ();
  }

  if (input->left) {
    Vector3f forward = camera.GetForwardXZ();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement -= right;
  }

  if (input->right) {
    Vector3f forward = camera.GetForwardXZ();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement += right;
  }

if (input->jumping) {
    if (player->on_ground) {
        player->velocity.y = jumpVelocity;
    }
    input->jumping = false;
  }

  if (input->fall) {
    movement -= Vector3f(0, 1, 0);
  }

  if (movement.LengthSqXZ() > 1) {
    movement = NormalizeXZ(movement);
  }

  movement += player->velocity;

  if (movement.LengthSq() > 0) {

    float modifier = kMoveSpeed;
    if (input->sprint) {
      modifier *= kSprintModifier;
    }

    movement.x *= (dt * modifier);
    movement.y *= dt;
    movement.z *= (dt * modifier);

    MoveAndCollideWithStepping(movement);
  }

  position_sync_timer += dt;

  // Send position packets when in spectator for testing chunk loading.
  // TODO: Implement for real
  if (player && player->gamemode == 3) {
    if (position_sync_timer >= (50.0f / 1000.0f)) {
      float yaw = Degrees(player->look.x) - 90.0f;
      float pitch = -Degrees(player->look.y);

      PlayerMoveFlags move_flags = PlayerMoveFlag_Position | PlayerMoveFlag_Look;

      outbound::play::SendPlayerPositionAndRotation(connection, player->position, yaw, pitch,
                                                    move_flags);
      position_sync_timer = 0.0f;
    }
  }

  UpdateCamera();
}

void GameState::UpdateCamera() {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  camera.position = player->position + Vector3f{0, player->eye_height, 0};
  camera.yaw = player->look.x;
  camera.pitch = player->look.y;
}

void GameState::OnWindowMouseMove(s32 dx, s32 dy) {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  const float kSensitivity = 0.005f;
  constexpr float kMaxPitch = Radians(89.0f);

  player->look.x += dx * kSensitivity;
  player->look.y -= dy * kSensitivity;

  if (player->look.y > kMaxPitch) {
    player->look.y = kMaxPitch;
  } else if (player->look.y < -kMaxPitch) {
    player->look.y = -kMaxPitch;
  }

  UpdateCamera();
}

void GameState::OnPlayerPositionAndLook(const Vector3f& position, const Vector3f& velocity, float yaw, float pitch,
                                        u32 flags) {
  auto player = player_manager.client_player;
  if (!player) {
    return;
  }

  if (flags & TeleportFlag_RelativeX) {
    player->position.x += position.x;
  } else {
    player->position.x = position.x;
  }

  if (flags & TeleportFlag_RelativeY) {
    player->position.y += position.y;
  } else {
    player->position.y = position.y + 1.62f;
  }

  if (flags & TeleportFlag_RelativeZ) {
    player->position.z += position.z;
  } else {
    player->position.z = position.z;
  }

  if (flags & TeleportFlag_RelativeYaw) {
    player->look.x += Radians(yaw);
  } else {
    player->look.x = Radians(yaw + 90.0f);
  }

  if (flags & TeleportFlag_RelativePitch) {
    player->look.y += Radians(pitch);
  } else {
    player->look.y = -Radians(pitch);
  }

  // TODO: Velocity
  // TODO: RotateDelta

  UpdateCamera();
}

void GameState::OnDimensionChange() {
  world.OnDimensionChange();
}

void GameState::OnChunkLoad(s32 chunk_x, s32 chunk_z) {
  world.OnChunkLoad(chunk_x, chunk_z);
}

void GameState::OnChunkUnload(s32 chunk_x, s32 chunk_z) {
  world.OnChunkUnload(chunk_x, chunk_z);
}

void GameState::OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid) {
  world.OnBlockChange(x, y, z, new_bid);
}

void PlayerManager::AddPlayer(const String& name, const String& uuid, u8 ping, u8 gamemode) {
  Player* new_player = nullptr;

  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      new_player = player;
      break;
    }
  }

  if (!new_player) {
    if (player_count >= polymer_array_count(players)) return;

    new_player = players + player_count++;
  }

  assert(name.size < polymer_array_count(new_player->name));

  memcpy(new_player->name, name.data, name.size);
  new_player->name[name.size] = 0;

  assert(uuid.size <= polymer_array_count(new_player->uuid));
  memcpy(new_player->uuid, uuid.data, uuid.size);

  new_player->ping = ping;
  new_player->gamemode = gamemode;
  new_player->listed = true;

  if (poly_strcmp(name, String(client_name)) == 0) {
    client_player = new_player;
  }
}

void PlayerManager::RemovePlayer(const String& uuid) {
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      Player* swap_player = players + --player_count;

      // Swap the pointer to the new slot if the removed player is the client player
      if (poly_strcmp(swap_player->name, String(client_name)) == 0) {
        client_player = players + i;
      }

      players[i] = *swap_player;
      return;
    }
  }
}

Player* PlayerManager::GetPlayerByUuid(const String& uuid) {
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      return player;
    }
  }

  return nullptr;
}

void PlayerManager::RenderPlayerList(render::FontRenderer& font_renderer) {
  float center_x = font_renderer.renderer->GetExtent().width / 2.0f;

  render::FontStyleFlags style = render::FontStyle_DropShadow;

  float max_width = 0;

  // Find the width of the longest player name
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_name(player->name);

    float text_width = (float)font_renderer.GetTextWidth(player_name);

    if (text_width > max_width) {
      max_width = text_width;
    }
  }

  Vector3f position(center_x - max_width / 2.0f, 16, 0);

  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_name(player->name);

    // Add extra pixels to the last displayed name because the glyph is not vertically centered
    float height = (i == player_count - 1) ? 18.0f : 16.0f;

    // Render the background with the size determined by the longest name. Add some horizontal padding.
    font_renderer.RenderBackground(position + Vector3f(-4, 0, 0), Vector2f(max_width + 8, height));
    font_renderer.RenderText(position, player_name, style);

    position.y += 16;
  }
}

} // namespace polymer
