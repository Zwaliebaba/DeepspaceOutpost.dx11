#pragma once

// OdbcStore - the production SQL Server persistence backend (Server).
//
// Implements IPersistenceStore over raw ODBC (native-first, no ORM). Windows-only
// and validated by a manual soak against a real SQL Server, NOT by CI - so the
// whole implementation is compiled only when the CMake option DSO_ENABLE_ODBC is
// ON (it is OFF by default, keeping CI free of a database/driver dependency). When
// it is OFF this header still declares the type but the factory falls back to the
// in-memory store.
//
// Apply NeuronServer/schema.sql to the target database first; point DSO_DB at an
// ODBC connection string, e.g.
//   Driver={ODBC Driver 17 for SQL Server};Server=localhost;Database=dso;Trusted_Connection=yes

#include <memory>
#include <string>

#include "PersistenceStore.h"   // Neuron::Persist::IPersistenceStore

namespace DSOServer
{
#ifdef DSO_ENABLE_ODBC
  // Open a store on `_connectionString` (an ODBC connection string). Returns null
  // if the connection fails, so the caller can decide how to degrade.
  std::unique_ptr<Neuron::Persist::IPersistenceStore> MakeOdbcStore(const std::string& _connectionString);
#endif
}
