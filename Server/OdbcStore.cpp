// OdbcStore - see OdbcStore.h. Compiled only when DSO_ENABLE_ODBC is ON (the
// Windows soak build); otherwise this is an empty translation unit so CI needs no
// ODBC driver. Runtime correctness is validated by the manual soak, not by CI.

#include "pch.h"

#include "OdbcStore.h"

#ifdef DSO_ENABLE_ODBC

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#pragma comment(lib, "odbc32.lib")

namespace DSOServer
{
  using namespace Neuron::Persist;

  namespace
  {
    [[nodiscard]] bool Ok(SQLRETURN _r) { return SQL_SUCCEEDED(_r); }

    // Diagnostic dump so a soak run can see why a statement failed.
    void Diag(const char* _what, SQLSMALLINT _type, SQLHANDLE _h)
    {
      SQLCHAR state[6] = {};
      SQLINTEGER native = 0;
      SQLCHAR text[512] = {};
      SQLSMALLINT len = 0;
      if (SQL_SUCCEEDED(SQLGetDiagRecA(_type, _h, 1, state, &native, text, sizeof(text), &len)))
        std::fprintf(stderr, "[odbc] %s failed: %s (%ld) %s\n", _what, state, static_cast<long>(native), text);
      else
        std::fprintf(stderr, "[odbc] %s failed (no diagnostic)\n", _what);
    }

    // A prepared statement scoped to one execution: alloc on construct, free on
    // destruct, so every helper is exception/early-return safe.
    class Stmt
    {
    public:
      explicit Stmt(SQLHDBC _dbc) { SQLAllocHandle(SQL_HANDLE_STMT, _dbc, &m_h); }
      ~Stmt() { if (m_h != SQL_NULL_HANDLE) SQLFreeHandle(SQL_HANDLE_STMT, m_h); }
      Stmt(const Stmt&) = delete;
      Stmt& operator=(const Stmt&) = delete;
      [[nodiscard]] SQLHSTMT Handle() const { return m_h; }
    private:
      SQLHSTMT m_h = SQL_NULL_HANDLE;
    };

    class OdbcStore final : public IPersistenceStore
    {
    public:
      [[nodiscard]] bool Connect(const std::string& _conn)
      {
        if (!Ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_env)))
          return false;
        SQLSetEnvAttr(m_env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
        if (!Ok(SQLAllocHandle(SQL_HANDLE_DBC, m_env, &m_dbc)))
          return false;

        std::vector<SQLCHAR> conn(_conn.begin(), _conn.end());
        conn.push_back(0);
        const SQLRETURN r = SQLDriverConnectA(m_dbc, nullptr, conn.data(), SQL_NTS,
                                              nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
        if (!Ok(r))
        {
          Diag("SQLDriverConnect", SQL_HANDLE_DBC, m_dbc);
          return false;
        }
        return true;
      }

      ~OdbcStore() override
      {
        if (m_dbc != SQL_NULL_HANDLE) { SQLDisconnect(m_dbc); SQLFreeHandle(SQL_HANDLE_DBC, m_dbc); }
        if (m_env != SQL_NULL_HANDLE) SQLFreeHandle(SQL_HANDLE_ENV, m_env);
      }

      void UpsertPlayer(const PlayerPersistState& _s) override
      {
        const std::string name(_s.commanderName);

        // 1. Ensure the account + its (v1: single) empire exist.
        Exec(
          "MERGE dbo.accounts AS t USING (SELECT ? AS n) AS s ON t.commander_name = s.n "
          "WHEN MATCHED THEN UPDATE SET last_seen_utc = SYSUTCDATETIME() "
          "WHEN NOT MATCHED THEN INSERT (commander_name, created_utc, last_seen_utc) "
          "VALUES (s.n, SYSUTCDATETIME(), SYSUTCDATETIME());",
          [&](SQLHSTMT h) { BindText(h, 1, name); });

        const int accountId = ScalarInt(
          "SELECT account_id FROM dbo.accounts WHERE commander_name = ?;",
          [&](SQLHSTMT h) { BindText(h, 1, name); });
        if (accountId < 0)
          return;

        int empireId = ScalarInt(
          "SELECT TOP 1 empire_id FROM dbo.empires WHERE account_id = ?;",
          [&](SQLHSTMT h) { BindInt(h, 1, accountId); });
        if (empireId < 0)
        {
          Exec("INSERT INTO dbo.empires (account_id) VALUES (?);",
               [&](SQLHSTMT h) { BindInt(h, 1, accountId); });
          empireId = ScalarInt("SELECT TOP 1 empire_id FROM dbo.empires WHERE account_id = ?;",
                               [&](SQLHSTMT h) { BindInt(h, 1, accountId); });
        }
        if (empireId < 0)
          return;

        // 2. Upsert the player row for that empire.
        Exec(
          "MERGE dbo.players AS t USING (SELECT ? AS e) AS s ON t.empire_id = s.e "
          "WHEN MATCHED THEN UPDATE SET credits=?, fuel_tenths=?, wanted_level=?, score=?, "
          "  hold_capacity=?, missiles=?, equip_flags=?, last_system_id=?, in_witchspace=?, "
          "  updated_tick=?, updated_utc=SYSUTCDATETIME() "
          "WHEN NOT MATCHED THEN INSERT (empire_id, credits, fuel_tenths, wanted_level, score, "
          "  hold_capacity, missiles, equip_flags, last_system_id, in_witchspace, updated_tick, updated_utc) "
          "VALUES (s.e, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, SYSUTCDATETIME());",
          [&](SQLHSTMT h)
          {
            SQLSMALLINT p = 1;
            BindInt(h, p++, empireId);
            for (int pass = 0; pass < 2; ++pass)   // UPDATE set-list then INSERT values (same order)
            {
              BindInt(h, p++, _s.credits);
              BindInt(h, p++, _s.fuelTenths);
              BindInt(h, p++, _s.wantedLevel);
              BindInt(h, p++, _s.score);
              BindInt(h, p++, _s.holdCapacity);
              BindInt(h, p++, _s.missiles);
              BindInt(h, p++, static_cast<int>(_s.equipFlags));
              BindInt(h, p++, _s.lastSystemId);
              BindInt(h, p++, _s.inWitchspace ? 1 : 0);
              BindBigInt(h, p++, static_cast<long long>(_s.updatedTick));
            }
          });

        const int playerId = ScalarInt(
          "SELECT TOP 1 player_id FROM dbo.players WHERE empire_id = ?;",
          [&](SQLHSTMT h) { BindInt(h, 1, empireId); });
        if (playerId < 0)
          return;

        // 3. Replace the cargo rows (non-zero stacks only).
        Exec("DELETE FROM dbo.player_cargo WHERE player_id = ?;",
             [&](SQLHSTMT h) { BindInt(h, 1, playerId); });
        for (std::size_t i = 0; i < _s.cargo.size(); ++i)
        {
          if (_s.cargo[i] == 0)
            continue;
          Exec("INSERT INTO dbo.player_cargo (player_id, commodity, units) VALUES (?, ?, ?);",
               [&](SQLHSTMT h)
               {
                 BindInt(h, 1, playerId);
                 BindInt(h, 2, static_cast<int>(i));
                 BindInt(h, 3, _s.cargo[i]);
               });
        }
      }

      std::optional<PlayerPersistState> LoadPlayer(const std::string& _name) override
      {
        Stmt stmt(m_dbc);
        const SQLHSTMT h = stmt.Handle();
        BindText(h, 1, _name);
        const char* sql =
          "SELECT p.player_id, p.credits, p.fuel_tenths, p.wanted_level, p.score, "
          "  p.hold_capacity, p.missiles, p.equip_flags, p.last_system_id, p.in_witchspace, p.updated_tick "
          "FROM dbo.players p JOIN dbo.empires e ON p.empire_id = e.empire_id "
          "JOIN dbo.accounts a ON e.account_id = a.account_id WHERE a.commander_name = ?;";
        if (!Ok(SQLExecDirectA(h, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)), SQL_NTS)))
        {
          Diag("LoadPlayer select", SQL_HANDLE_STMT, h);
          return std::nullopt;
        }
        if (!Ok(SQLFetch(h)))
          return std::nullopt;   // unknown commander

        PlayerPersistState s;
        s.commanderName = _name;
        int playerId = 0;
        SQLLEN ind = 0;
        SQLGetData(h, 1, SQL_C_LONG, &playerId, 0, &ind);
        s.credits      = GetInt(h, 2);
        s.fuelTenths   = GetInt(h, 3);
        s.wantedLevel  = GetInt(h, 4);
        s.score        = GetInt(h, 5);
        s.holdCapacity = GetInt(h, 6);
        s.missiles     = GetInt(h, 7);
        s.equipFlags   = static_cast<uint32_t>(GetInt(h, 8));
        s.lastSystemId = GetInt(h, 9);
        s.inWitchspace = GetInt(h, 10) != 0;
        {
          long long tick = 0;
          SQLGetData(h, 11, SQL_C_SBIGINT, &tick, 0, &ind);
          s.updatedTick = static_cast<uint64_t>(tick);
        }

        // Cargo rows.
        Stmt cargoStmt(m_dbc);
        const SQLHSTMT ch = cargoStmt.Handle();
        BindInt(ch, 1, playerId);
        const char* csql = "SELECT commodity, units FROM dbo.player_cargo WHERE player_id = ?;";
        if (Ok(SQLExecDirectA(ch, reinterpret_cast<SQLCHAR*>(const_cast<char*>(csql)), SQL_NTS)))
        {
          while (Ok(SQLFetch(ch)))
          {
            const int commodity = GetInt(ch, 1);
            const int units = GetInt(ch, 2);
            if (commodity >= 0 && static_cast<std::size_t>(commodity) < s.cargo.size())
              s.cargo[static_cast<std::size_t>(commodity)] = units;
          }
        }
        return s;
      }

      void AppendCommands(const std::vector<CommandLogEntry>& _batch) override
      {
        for (const CommandLogEntry& e : _batch)
          Exec("INSERT INTO dbo.command_log (world_tick, player_id, message_id, payload, logged_utc) "
               "VALUES (?, ?, ?, ?, SYSUTCDATETIME());",
               [&](SQLHSTMT h)
               {
                 BindBigInt(h, 1, static_cast<long long>(e.worldTick));
                 BindInt(h, 2, e.playerId);
                 BindInt(h, 3, e.messageId);
                 BindBinary(h, 4, e.payload);
               });
      }

      void UpsertMarketRows(const std::vector<MarketRow>& _batch) override
      {
        for (const MarketRow& r : _batch)
          Exec("MERGE dbo.station_markets AS t USING (SELECT ? AS sys, ? AS com) AS s "
               "ON t.system_id = s.sys AND t.commodity = s.com "
               "WHEN MATCHED THEN UPDATE SET stock=?, price=?, updated_tick=? "
               "WHEN NOT MATCHED THEN INSERT (system_id, commodity, stock, price, updated_tick) "
               "VALUES (s.sys, s.com, ?, ?, ?);",
               [&](SQLHSTMT h)
               {
                 SQLSMALLINT p = 1;
                 BindInt(h, p++, r.systemId);
                 BindInt(h, p++, r.commodity);
                 for (int pass = 0; pass < 2; ++pass)
                 {
                   BindInt(h, p++, r.stock);
                   BindInt(h, p++, r.price);
                   BindBigInt(h, p++, static_cast<long long>(r.updatedTick));
                 }
               });
      }

      std::optional<std::string> ReadMeta(const std::string& _key) override
      {
        Stmt stmt(m_dbc);
        const SQLHSTMT h = stmt.Handle();
        BindText(h, 1, _key);
        const char* sql = "SELECT meta_value FROM dbo.world_meta WHERE meta_key = ?;";
        if (!Ok(SQLExecDirectA(h, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)), SQL_NTS)))
          return std::nullopt;
        if (!Ok(SQLFetch(h)))
          return std::nullopt;
        char buf[256] = {};
        SQLLEN ind = 0;
        SQLGetData(h, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        return std::string(buf);
      }

      void WriteMeta(const std::string& _key, const std::string& _value) override
      {
        Exec("MERGE dbo.world_meta AS t USING (SELECT ? AS k) AS s ON t.meta_key = s.k "
             "WHEN MATCHED THEN UPDATE SET meta_value = ? "
             "WHEN NOT MATCHED THEN INSERT (meta_key, meta_value) VALUES (s.k, ?);",
             [&](SQLHSTMT h) { BindText(h, 1, _key); BindText(h, 2, _value); BindText(h, 3, _value); });
      }

    private:
      // --- tiny binding helpers (persist the bound storage until execute) --------
      // Bound buffers must outlive SQLExecute; Exec() keeps them in a per-call arena.
      struct Arena
      {
        std::vector<std::unique_ptr<std::string>> strings;
        std::vector<std::unique_ptr<int>> ints;
        std::vector<std::unique_ptr<long long>> bigints;
        std::vector<std::unique_ptr<std::vector<uint8_t>>> blobs;
        std::vector<SQLLEN> lens;
      };
      Arena* m_arena = nullptr;   // set for the duration of one Exec

      void BindText(SQLHSTMT _h, SQLSMALLINT _i, const std::string& _v)
      {
        auto owned = std::make_unique<std::string>(_v);
        std::string* p = owned.get();
        m_arena->strings.push_back(std::move(owned));
        SQLBindParameter(_h, _i, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                         p->size(), 0, const_cast<char*>(p->c_str()),
                         static_cast<SQLLEN>(p->size()), nullptr);
      }
      void BindInt(SQLHSTMT _h, SQLSMALLINT _i, int _v)
      {
        auto owned = std::make_unique<int>(_v);
        int* p = owned.get();
        m_arena->ints.push_back(std::move(owned));
        SQLBindParameter(_h, _i, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, p, 0, nullptr);
      }
      void BindBigInt(SQLHSTMT _h, SQLSMALLINT _i, long long _v)
      {
        auto owned = std::make_unique<long long>(_v);
        long long* p = owned.get();
        m_arena->bigints.push_back(std::move(owned));
        SQLBindParameter(_h, _i, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, p, 0, nullptr);
      }
      void BindBinary(SQLHSTMT _h, SQLSMALLINT _i, const std::vector<uint8_t>& _v)
      {
        auto owned = std::make_unique<std::vector<uint8_t>>(_v);
        std::vector<uint8_t>* p = owned.get();
        m_arena->blobs.push_back(std::move(owned));
        m_arena->lens.push_back(static_cast<SQLLEN>(p->size()));
        SQLBindParameter(_h, _i, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                         p->size(), 0, p->data(), static_cast<SQLLEN>(p->size()),
                         &m_arena->lens.back());
      }

      [[nodiscard]] static int GetInt(SQLHSTMT _h, SQLUSMALLINT _col)
      {
        int v = 0;
        SQLLEN ind = 0;
        SQLGetData(_h, _col, SQL_C_LONG, &v, 0, &ind);
        return (ind == SQL_NULL_DATA) ? 0 : v;
      }

      // Run a parameterized statement (no result rows).
      template <typename Bind>
      void Exec(const char* _sql, Bind&& _bind)
      {
        Stmt stmt(m_dbc);
        const SQLHSTMT h = stmt.Handle();
        Arena arena;
        arena.lens.reserve(8);
        m_arena = &arena;
        _bind(h);
        if (!Ok(SQLExecDirectA(h, reinterpret_cast<SQLCHAR*>(const_cast<char*>(_sql)), SQL_NTS)))
          Diag("Exec", SQL_HANDLE_STMT, h);
        m_arena = nullptr;
      }

      // Run a parameterized query returning a single integer (or -1 if no row).
      template <typename Bind>
      [[nodiscard]] int ScalarInt(const char* _sql, Bind&& _bind)
      {
        Stmt stmt(m_dbc);
        const SQLHSTMT h = stmt.Handle();
        Arena arena;
        arena.lens.reserve(4);
        m_arena = &arena;
        _bind(h);
        int result = -1;
        if (Ok(SQLExecDirectA(h, reinterpret_cast<SQLCHAR*>(const_cast<char*>(_sql)), SQL_NTS)))
          if (Ok(SQLFetch(h)))
            result = GetInt(h, 1);
        m_arena = nullptr;
        return result;
      }

      SQLHENV m_env = SQL_NULL_HANDLE;
      SQLHDBC m_dbc = SQL_NULL_HANDLE;
    };
  }

  std::unique_ptr<IPersistenceStore> MakeOdbcStore(const std::string& _connectionString)
  {
    auto store = std::make_unique<OdbcStore>();
    if (!store->Connect(_connectionString))
      return nullptr;
    return store;
  }
}

#endif   // DSO_ENABLE_ODBC
