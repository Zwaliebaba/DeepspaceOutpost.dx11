#pragma once

// Universe - the de-globalised game world (A2).
//
// Wraps an ECS::Registry and is the single owner of the live game entities that
// today live in the global `local_objects[]` array (plus the player). The game
// code migrates from indexing that array to spawning/iterating entities here,
// file by file, behind the golden-run tests. Header-only for now so both the
// client and the Tests can use it; moves into GameLogic at A4.

#include "ECS.h"
#include "GameComponents.h"

#include <cstddef>

namespace Neuron
{
  class Universe
  {
  public:
    [[nodiscard]] ECS::Registry& Reg() { return m_reg; }
    [[nodiscard]] const ECS::Registry& Reg() const { return m_reg; }

    // The player's own ship entity (invalid until SetPlayer).
    [[nodiscard]] ECS::EntityId Player() const { return m_player; }
    void SetPlayer(ECS::EntityId _player) { m_player = _player; }

    // Spawn a universe object with the components every ship needs (type +
    // transform). Callers add Motion/Combat/Ai/Explosion as appropriate.
    ECS::EntityId Spawn(int _shipType, const Game::Transform& _transform)
    {
      const ECS::EntityId e = m_reg.Create();
      m_reg.Add<Game::ShipType>(e, Game::ShipType{ _shipType });
      m_reg.Add<Game::Transform>(e, _transform);
      return e;
    }

    void Destroy(ECS::EntityId _entity) { m_reg.Destroy(_entity); }

    // Tear down all entities (called when a new game starts).
    void Reset()
    {
      m_reg = ECS::Registry{};
      m_player = ECS::EntityId{};
    }

    [[nodiscard]] std::size_t ObjectCount() const { return m_reg.AliveCount(); }

  private:
    ECS::Registry m_reg;
    ECS::EntityId m_player{};
  };
}
