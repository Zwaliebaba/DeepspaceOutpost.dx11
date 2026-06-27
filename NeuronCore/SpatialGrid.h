#pragma once

// SpatialGrid - uniform cell index over the int64 world (A3).
//
// Partitions the continuous int64^3 world into fixed-size cells and indexes
// entity ids by cell, giving O(local) neighbour queries. This is the invisible
// partition under the seamless world: it drives interest management / Area-of-
// Interest (Phase D) and is the unit a zone/shard is later carved along. The
// world stays seamless to players; the grid is purely an internal acceleration
// structure.
//
// Header-only, std-only - usable from any module and trivially testable.

#include "Vector3i64.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Neuron::Spatial
{
  // Integer cell coordinate (one index per axis).
  struct Cell
  {
    int64_t cx = 0;
    int64_t cy = 0;
    int64_t cz = 0;

    [[nodiscard]] bool operator==(const Cell& _o) const { return cx == _o.cx && cy == _o.cy && cz == _o.cz; }
  };

  struct CellHash
  {
    [[nodiscard]] std::size_t operator()(const Cell& _c) const noexcept
    {
      auto mix = [](uint64_t _h, uint64_t _v) noexcept
      {
        _h ^= _v + 0x9E3779B97F4A7C15ull + (_h << 6) + (_h >> 2);
        return _h;
      };
      uint64_t h = mix(0, static_cast<uint64_t>(_c.cx));
      h = mix(h, static_cast<uint64_t>(_c.cy));
      h = mix(h, static_cast<uint64_t>(_c.cz));
      return static_cast<std::size_t>(h);
    }
  };

  class Grid
  {
  public:
    explicit Grid(int64_t _cellSize) : m_cellSize(_cellSize > 0 ? _cellSize : 1) {}

    [[nodiscard]] int64_t CellSize() const { return m_cellSize; }

    [[nodiscard]] Cell CellOf(const Math::Vector3i64& _p) const
    {
      return { FloorDiv(_p.x), FloorDiv(_p.y), FloorDiv(_p.z) };
    }

    void Insert(uint64_t _id, const Math::Vector3i64& _pos)
    {
      m_cells[CellOf(_pos)].insert(_id);
    }

    void Remove(uint64_t _id, const Math::Vector3i64& _pos)
    {
      const auto it = m_cells.find(CellOf(_pos));
      if (it == m_cells.end())
        return;
      it->second.erase(_id);
      if (it->second.empty())
        m_cells.erase(it);
    }

    // Re-bucket an entity that moved. No-op if it stayed in the same cell.
    void Move(uint64_t _id, const Math::Vector3i64& _from, const Math::Vector3i64& _to)
    {
      if (CellOf(_from) == CellOf(_to))
        return;
      Remove(_id, _from);
      Insert(_id, _to);
    }

    // Append every id within +/- _radiusCells (a cube neighbourhood) of the cell
    // containing _pos. Caller can filter to an exact radius afterwards.
    void QueryNear(const Math::Vector3i64& _pos, int _radiusCells, std::vector<uint64_t>& _out) const
    {
      const Cell c = CellOf(_pos);
      for (int dz = -_radiusCells; dz <= _radiusCells; ++dz)
        for (int dy = -_radiusCells; dy <= _radiusCells; ++dy)
          for (int dx = -_radiusCells; dx <= _radiusCells; ++dx)
          {
            const auto it = m_cells.find(Cell{ c.cx + dx, c.cy + dy, c.cz + dz });
            if (it != m_cells.end())
              _out.insert(_out.end(), it->second.begin(), it->second.end());
          }
    }

    [[nodiscard]] std::size_t OccupiedCellCount() const { return m_cells.size(); }

  private:
    // Floor division so negative coordinates map to the correct cell
    // (e.g. -1 with cell size 100 is cell -1, not 0).
    [[nodiscard]] int64_t FloorDiv(int64_t _a) const
    {
      int64_t q = _a / m_cellSize;
      const int64_t r = _a % m_cellSize;
      if (r != 0 && (r < 0))
        --q;
      return q;
    }

    int64_t m_cellSize;
    std::unordered_map<Cell, std::unordered_set<uint64_t>, CellHash> m_cells;
  };
}
