#include "pch.h"

#include "space.h"          // local_object, MAX_LOCAL_OBJECTS, LocalObjectArray
#include "GameUniverse.h"   // GameUniverse()

#include <array>

// The 20 permanent slot entities backing local_objects[]. Each carries one
// `local_object` component. Because the slots are created once and never
// added/removed (an empty slot is just type == 0), the ECS component pool never
// reallocates after creation, so the references this proxy hands out - and
// pointers like &local_objects[un] - stay valid for the whole frame, matching
// the old raw-array behaviour.
namespace
{
  std::array<Neuron::ECS::EntityId, MAX_LOCAL_OBJECTS> g_slots;
}

LocalObjectArray local_objects;

void create_local_object_slots (void)
{
  for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
  {
    const Neuron::ECS::EntityId e = GameUniverse().Reg().Create();
    GameUniverse().Reg().Add<local_object>(e, local_object{});
    g_slots[i] = e;
  }
}

struct local_object& LocalObjectArray::operator[] (int slot)
{
  return GameUniverse().Reg().Get<local_object>(g_slots[slot]);
}
