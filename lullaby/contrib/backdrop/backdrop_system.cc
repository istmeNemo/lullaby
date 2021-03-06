/*
Copyright 2017 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "lullaby/contrib/backdrop/backdrop_system.h"

#include "lullaby/contrib/backdrop/backdrop_channels.h"
#include "lullaby/modules/ecs/entity_factory.h"
#include "lullaby/modules/flatbuffers/mathfu_fb_conversions.h"
#include "lullaby/modules/script/function_binder.h"
#include "lullaby/systems/animation/animation_system.h"
#include "lullaby/systems/layout/layout_box_system.h"
#include "lullaby/systems/nine_patch/nine_patch_system.h"
#include "lullaby/systems/transform/transform_system.h"
#include "lullaby/util/logging.h"

namespace lull {
namespace {
const size_t kBackdropPoolPageSize = 4;
const HashValue kBackdropDefHash = Hash("BackdropDef");
}  // namespace

BackdropSystem::BackdropSystem(Registry* registry)
    : System(registry), backdrops_(kBackdropPoolPageSize) {
  RegisterDef(this, kBackdropDefHash);

  RegisterDependency<RenderSystem>(this);
  RegisterDependency<TransformSystem>(this);

  auto* dispatcher = registry_->Get<Dispatcher>();
  dispatcher->Connect(this, [this](const ParentChangedEvent& event) {
    OnParentChanged(event);
  });
  dispatcher->Connect(this, [this](const AabbChangedEvent& event) {
    OnEntityChanged(event.target);
  });
  dispatcher->Connect(this, [this](const OnDisabledEvent& event) {
    OnEntityChanged(event.target);
  });
  dispatcher->Connect(this, [this](const OnEnabledEvent& event) {
    OnEntityChanged(event.target);
  });
  dispatcher->Connect(this, [this](const DesiredSizeChangedEvent& event) {
    OnDesiredSizeChanged(event);
  });

  FunctionBinder* binder = registry->Get<FunctionBinder>();
  if (binder) {
    binder->RegisterMethod("lull.Backdrop.HasBackdrop",
                           &lull::BackdropSystem::HasBackdrop);
    binder->RegisterMethod("lull.Backdrop.GetBackdropRenderableEntity",
                           &lull::BackdropSystem::GetBackdropRenderableEntity);
    binder->RegisterMethod(
        "lull.Backdrop.GetBackdropAabbAnimationDuration",
        &lull::BackdropSystem::GetBackdropAabbAnimationDuration);
    binder->RegisterMethod(
        "lull.Backdrop.SetBackdropAabbAnimationDuration",
        &lull::BackdropSystem::SetBackdropAabbAnimationDuration);
  }
}

BackdropSystem::~BackdropSystem() {
  registry_->Get<Dispatcher>()->DisconnectAll(this);

  FunctionBinder* binder = registry_->Get<FunctionBinder>();
  if (binder) {
    binder->UnregisterFunction("lull.Backdrop.HasBackdrop");
    binder->UnregisterFunction("lull.Backdrop.GetBackdropRenderableEntity");
    binder->UnregisterFunction(
        "lull.Backdrop.GetBackdropAabbAnimationDuration");
    binder->UnregisterFunction(
        "lull.Backdrop.SetBackdropAabbAnimationDuration");
  }
}

void BackdropSystem::Initialize() {
  if (registry_->Get<AnimationSystem>()) {
    BackdropAabbChannel::Setup(registry_, 8);
  }
}

void BackdropSystem::Create(Entity e, HashValue type, const Def* def) {
  if (type != kBackdropDefHash) {
    LOG(DFATAL) << "Invalid type passed to Create. Expecting BackdropDef!";
    return;
  }
  const auto& data = *ConvertDef<BackdropDef>(def);
  Backdrop* backdrop = backdrops_.Emplace(e);
  if (!backdrop) {
    LOG(DFATAL) << "Backdrop already created for entity: " << e;
    return;
  }

  backdrop->offset = data.offset();
  MathfuVec2FromFbVec2(data.margin(), &backdrop->bottom_left_margin);
  if (data.top_right_margin()) {
    MathfuVec2FromFbVec2(data.top_right_margin(), &backdrop->top_right_margin);
  } else {
    backdrop->top_right_margin = backdrop->bottom_left_margin;
  }
  backdrop->aabb_behavior = data.aabb_behavior();
  backdrop->animate_aabb_duration =
      std::chrono::milliseconds(data.animate_aabb_duration_ms());

  backdrop->quad.size.x = 0.0f;
  backdrop->quad.size.y = 0.0f;

  if (data.quad()) {
    backdrop->quad.verts.x = data.quad()->verts_x();
    backdrop->quad.verts.y = data.quad()->verts_y();
    backdrop->quad.corner_radius = data.quad()->corner_radius();
    backdrop->quad.corner_verts = data.quad()->corner_verts();
    backdrop->quad.has_uv = data.quad()->has_uv();
    backdrop->renderable_type = RenderableType::kQuad;
  } else {
    backdrop->renderable_type = RenderableType::kNinePatch;
    if (!registry_->Get<NinePatchSystem>()) {
      LOG(DFATAL) << "Backdrop missing NinePatchSystem for entity: " << e;
      return;
    }
  }

  // When calling transform system's CreateChildWithEntity() or AddChild(), it
  // triggers ParentChangedEvent, and BackdropSystem will respond with
  // UpdateBackdrop() before that transform system function returns. So, create
  // and save the entity to the component first. This should only be an issue
  // with non-queued Dispatcher, such as in tests.
  backdrop->renderable = registry_->Get<EntityFactory>()->Create();
  // TODO(b/30098068): Ideally we would fail if the def did not contain a
  // valid child blueprint, but we cannot do that because then we would be
  // unable to test this call.
  if (data.blueprint() && data.blueprint()->size() > 0) {
    registry_->Get<TransformSystem>()->CreateChildWithEntity(
        backdrop->GetEntity(), backdrop->renderable, data.blueprint()->str());
  } else {
    LOG(WARNING) << "BackdropDef missing required field blueprint";

    auto& transform_system = *registry_->Get<TransformSystem>();
    transform_system.Create(backdrop->renderable, Sqt());
    transform_system.AddChild(backdrop->GetEntity(), backdrop->renderable);
  }
}

void BackdropSystem::Destroy(Entity e) { backdrops_.Destroy(e); }

bool BackdropSystem::HasBackdrop(const Entity e) const {
  return backdrops_.Get(e) != nullptr;
}

Entity BackdropSystem::GetBackdropRenderableEntity(Entity e) const {
  const Backdrop* backdrop = backdrops_.Get(e);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem";
    return lull::kNullEntity;
  }
  return backdrop->renderable;
}

void BackdropSystem::SetBackdropQuad(Entity e, const RenderSystem::Quad& quad) {
  Backdrop* backdrop = backdrops_.Get(e);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem";
    return;
  }
  if (backdrop->renderable_type != RenderableType::kQuad) {
    LOG(WARNING) << "Backdrop is not a quad, setting backdrop quad does "
                 << "nothing";
    return;
  }
  backdrop->quad = quad;
  UpdateBackdrop(backdrop);
}

const Aabb* BackdropSystem::GetBackdropAabb(Entity entity) const {
  const Backdrop* backdrop = backdrops_.Get(entity);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem: "
                 << entity;
    return nullptr;
  }

  if (!backdrop->aabb) {
    return nullptr;
  }

  return backdrop->aabb.get();
}

void BackdropSystem::SetBackdropAabb(Entity entity, const Aabb& aabb) {
  Backdrop* backdrop = backdrops_.Get(entity);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem: "
                 << entity;
    return;
  }

  auto* transform_system = registry_->Get<TransformSystem>();
  auto* render_system = registry_->Get<RenderSystem>();
  auto* nine_patch_system = registry_->Get<NinePatchSystem>();

  Aabb renderable_aabb = aabb;
  // If it is empty and Behavior_Backdrop, we already applied the margin and
  // offset, so don't change that case.
  if (!(backdrop->is_empty &&
        backdrop->aabb_behavior == BackdropAabbBehavior_Backdrop)) {
    CreateRenderableAabb(backdrop, &renderable_aabb);
  }

  Sqt sqt;
  sqt.translation = renderable_aabb.Center();
  sqt.translation.z = renderable_aabb.min.z;
  backdrop->quad.size = renderable_aabb.Size().xy();
  transform_system->SetSqt(backdrop->renderable, sqt);
  switch (backdrop->renderable_type) {
    case RenderableType::kQuad:
      render_system->SetQuad(backdrop->renderable, backdrop->quad);
      break;
    case RenderableType::kNinePatch:
      nine_patch_system->SetSize(backdrop->renderable, backdrop->quad.size);
      break;
  }

  // Update the bound box of this entity manually from what we know about the
  // quad. This avoids us having to do it in the AabbChangedEvent handler, which
  // would require the above AABB calculation of other children again.
  switch (backdrop->aabb_behavior) {
    case BackdropAabbBehavior_None: {
      break;
    }
    case BackdropAabbBehavior_Content: {
      transform_system->SetAabb(backdrop->GetEntity(), aabb);
      break;
    }
    case BackdropAabbBehavior_Backdrop: {
      const Aabb total_aabb = MergeAabbs(aabb, renderable_aabb);
      transform_system->SetAabb(backdrop->GetEntity(), total_aabb);
      break;
    }
  }

  backdrop->aabb = aabb;
}

Clock::duration BackdropSystem::GetBackdropAabbAnimationDuration(
    Entity entity) {
  Backdrop* backdrop = backdrops_.Get(entity);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem: "
                 << entity;
    return Clock::duration::zero();
  }
  return backdrop->animate_aabb_duration;
}

void BackdropSystem::SetBackdropAabbAnimationDuration(
    Entity entity, Clock::duration duration) {
  Backdrop* backdrop = backdrops_.Get(entity);
  if (!backdrop) {
    LOG(WARNING) << "Entity is not registered with the BackdropSystem: "
                 << entity;
    return;
  }
  backdrop->animate_aabb_duration = duration;
}

void BackdropSystem::CreateRenderableAabb(const Backdrop* backdrop,
                                          Aabb* aabb) {
  aabb->min += mathfu::vec3(-backdrop->bottom_left_margin, backdrop->offset);
  aabb->max += mathfu::vec3(backdrop->top_right_margin, 0.f);
  // The renderable is flat.
  aabb->max.z = aabb->min.z;
}

void BackdropSystem::UpdateBackdrop(Backdrop* backdrop) {
  auto& transform_system = *registry_->Get<TransformSystem>();
  const std::vector<Entity>* children =
      transform_system.GetChildren(backdrop->GetEntity());
  if (!children || children->empty()) {
    LOG(DFATAL) << "Entity with BackdropDef missing required child: "
                << backdrop->renderable;
    return;
  }

  Aabb final_aabb;
  bool aabb_uninitialized = true;
  auto merge_aabb = [&final_aabb, &aabb_uninitialized](const Aabb& other_aabb) {
    if (aabb_uninitialized) {
      final_aabb = other_aabb;
      aabb_uninitialized = false;
    } else {
      final_aabb.min = mathfu::vec3::Min(final_aabb.min, other_aabb.min);
      final_aabb.max = mathfu::vec3::Max(final_aabb.max, other_aabb.max);
    }
  };

  for (Entity child : *children) {
    if (child == backdrop->renderable) {
      continue;
    }

    const Sqt* sqt = transform_system.GetSqt(child);
    const Aabb* aabb_child_space = transform_system.GetAabb(child);
    const bool child_enabled = transform_system.IsEnabled(child);
    if (!sqt || !aabb_child_space || !child_enabled) {
      continue;
    }

    mathfu::vec3 corners[8];
    GetTransformedBoxCorners(*aabb_child_space, *sqt, corners);
    merge_aabb(GetBoundingBox(corners, 8));
  }

  // For animation purposes, normally we store just the exact aabb of the
  // children.  But, if there are no children and the aabb_behavior is Backdrop,
  // store the renderable quad instead because we need to animate to it.
  if (aabb_uninitialized) {
    if (backdrop->aabb_behavior == BackdropAabbBehavior_Backdrop) {
      CreateRenderableAabb(backdrop, &final_aabb);
      // We're transitioning from not empty, so turn the current aabb from
      // the exact aabb of children into the corresponding renderable quad.
      if (!backdrop->is_empty && backdrop->aabb) {
        CreateRenderableAabb(backdrop, backdrop->aabb.get());
      }
    }
    backdrop->is_empty = true;
  } else if (backdrop->is_empty) {
    // We now have children, but used to be empty. The current aabb was set to
    // be the renderable quad, turn it into an empty aabb instead so we animate
    // from no children.
    if (backdrop->aabb_behavior == BackdropAabbBehavior_Backdrop) {
      backdrop->aabb = Aabb();
    }
    backdrop->is_empty = false;
  }

  // Don't animate the very first aabb, just set it directly.
  if (backdrop->animate_aabb_duration <= Clock::duration::zero() ||
      !backdrop->aabb) {
    SetBackdropAabb(backdrop->GetEntity(), final_aabb);
  } else {
    auto* animation_system = registry_->Get<AnimationSystem>();
    if (!animation_system) {
      LOG(DFATAL) << "Missing AnimationSystem!";
      SetBackdropAabb(backdrop->GetEntity(), final_aabb);
      return;
    }

    float target[6];
    final_aabb.ToArray(target);
    animation_system->SetTarget(backdrop->GetEntity(),
                                BackdropAabbChannel::kChannelName,
                                target, 6, backdrop->animate_aabb_duration);
  }
}

void BackdropSystem::OnParentChanged(const ParentChangedEvent& event) {
  Backdrop* old_parent_backdrop = backdrops_.Get(event.old_parent);
  if (old_parent_backdrop) {
    UpdateBackdrop(old_parent_backdrop);
  }
  Backdrop* new_parent_backdrop = backdrops_.Get(event.new_parent);
  if (new_parent_backdrop) {
    UpdateBackdrop(new_parent_backdrop);
  }
}

void BackdropSystem::OnEntityChanged(Entity entity) {
  Entity parent = registry_->Get<TransformSystem>()->GetParent(entity);
  Backdrop* parent_backdrop = backdrops_.Get(parent);
  if (parent_backdrop && parent_backdrop->renderable != entity) {
    UpdateBackdrop(parent_backdrop);
  }

  // TODO(b/70281412): Remove the following once sort order is properly
  // refreshed.
  Backdrop* backdrop = backdrops_.Get(entity);
  if (backdrop && backdrop->renderable) {
    auto* render_system = registry_->Get<RenderSystem>();
    render_system->SetSortOrderOffset(
        backdrop->renderable,
        render_system->GetSortOrderOffset(backdrop->renderable));
  }
}

void BackdropSystem::OnDesiredSizeChanged(
    const DesiredSizeChangedEvent& event) {
  if (backdrops_.Get(event.target)) {
    const auto* transform_system = registry_->Get<TransformSystem>();
    auto* layout_box_system = registry_->Get<LayoutBoxSystem>();
    for (const Entity& child : *transform_system->GetChildren(event.target)) {
      layout_box_system->SetDesiredSize(child, event.source, event.x, event.y,
                                        event.z);
    }
  }
}

}  // namespace lull
